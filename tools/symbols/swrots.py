"""Stage 2: official facts from the game's own executable (trust level 1).

  - XDK library functions (src/game/sdk_symbols.inc).
  - Class vtables: slot 3 of every engine class vtable is GetTypeName, a function
    returning the class name ("ICharacter"). The name is the developers' own; the
    vtable and its methods belong to that class.
  - Class::Method strings: log/profiler text naming the function that uses it
    ("ICharacter::Teleport"), when exactly one function uses it.
  - Source-file paths (__FILE__ in assertions/logging): which source file a
    function was compiled from.
"""
import re
import struct

import binary

METHOD_TEXT = re.compile(r"^\[?([A-Z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)+)(?:\(\))?")
SOURCE_PATH = re.compile(r"([A-Za-z]:)?[\\/.]*[\w\\/. ]+\.(?:cpp|c|h|inl)$", re.I)
CODE_SECTIONS = (".text", "D3D", "D3DX", "XGRPH", "DSOUND", "WMADEC", "XPP")


class Entry:
    """One symbol: a name for an address, with how sure we are and why."""
    __slots__ = ("addr", "name", "level", "evidence", "cls", "file", "size", "kind")

    def __init__(self, addr, name, level, evidence, kind="function"):
        self.addr, self.name, self.level, self.evidence, self.kind = addr, name, level, [evidence], kind
        self.cls = self.file = None
        self.size = 0


class Game:
    def __init__(self, xbe_path, sdk_inc):
        self.b = binary.load_xbe(xbe_path, CODE_SECTIONS)
        self.sdk = {}
        for addr, name, kind in re.findall(r'SDK_SYMBOL\(0x([0-9A-F]+), "(\w+)", SymbolKind::(\w+)\)',
                                           open(sdk_inc).read()):
            self.sdk[int(addr, 16)] = (name, kind)
        starts = set()
        for s in self.b.sections:
            if s[4]:
                starts |= realign_starts(s)
        starts |= {a for a, (n, k) in self.sdk.items() if self.b.is_code(a)}
        starts |= self.vtable_slot_starts(starts)
        if self.b.entry:
            starts.add(self.b.entry)
        self.funcs = binary.analyze(self.b, starts)
        calls = {t for f in self.funcs.values() for t in f.calls if self.b.is_code(t)} - starts
        if calls:
            self.funcs = binary.analyze(self.b, starts | calls)
        self.entries = {}
        self.vtables = {}  # va -> (class, [functions])
        self.files = {}  # function -> set of source files


def _vtable_slot_starts(self, starts):
    """Functions that vtables point to (the first slots of a class vtable, around its slot-3
    name getter) but that no padding marks as a function start."""
    funcs = binary.analyze(self.b, starts)
    getters = {a for a, f in funcs.items() if f.getter and binary.CLASS_NAME.match(f.getter.encode("latin1"))}
    extra = set()
    for name, va, size, data, is_code in self.b.sections:
        if is_code:
            continue
        for off in range(12, size - 3, 4):
            if struct.unpack_from("<I", data, off)[0] in getters:
                for k in range(-3, 0):
                    p = struct.unpack_from("<I", data, off + 4 * k)[0]
                    if self.b.is_code(p) and p not in starts:
                        extra.add(p)
    return extra


Game.vtable_slot_starts = _vtable_slot_starts


def realign_starts(section):
    """16-byte aligned addresses right after padding or a return (MSVC function layout)."""
    name, lo, size, data, is_code = section
    out = set()
    for off in range(16, len(data), 16):
        p = data[off - 1]
        if data[off] in (0x00, 0x90, 0xCC):
            continue
        if p in (0x90, 0xCC, 0xC3) or (data[off - 3] == 0xC2 and p == 0x00):
            out.add(lo + off)
    return out


def add(g, addr, name, level, evidence, kind="function"):
    """Adds or strengthens an entry; a stronger (lower) level wins, conflicts are recorded."""
    e = g.entries.get(addr)
    if e is None:
        g.entries[addr] = Entry(addr, name, level, evidence, kind)
        return
    placeholder = lambda n: "::vfunc_" in n or n.startswith("sub_")
    if placeholder(name) and not placeholder(e.name):
        return
    if placeholder(e.name) and not placeholder(name) and level <= e.level + 0:
        e.evidence.append(evidence)
        e.name, e.level = name, level
        return
    if e.name == name:
        e.evidence.append(evidence)
        e.level = min(e.level, level)
    elif level < e.level:
        e.evidence.append(f"replaces '{e.name}' (level {e.level})")
        e.name, e.level = name, level
        e.evidence.append(evidence)
    else:
        e.evidence.append(f"conflict: '{name}' (level {level}, {evidence})")


def find_class_vtables(g):
    """Vtables located through their slot-3 GetTypeName getter."""
    b = g.b
    getters = {a: f.getter for a, f in g.funcs.items()
               if f.getter and binary.CLASS_NAME.match(f.getter.encode("latin1"))}
    # Every data location holding a pointer to a getter.
    refs = {}
    for name, va, size, data, is_code in b.sections:
        if is_code:
            continue
        for off in range(0, size - 3, 4):
            p = struct.unpack_from("<I", data, off)[0]
            if p in getters:
                refs.setdefault(p, []).append(va + off)
    starts = {}
    for getter, locs in refs.items():
        locs.sort()
        skip = set()
        for loc in locs:
            if loc in skip:
                continue
            # The getter is also in slot 16 of many vtables: slot 3 comes first, 52 bytes earlier.
            if loc + 52 in locs:
                skip.add(loc + 52)
            starts[loc - 12] = getters[getter]
    # Length: code pointers from the start, up to the next vtable.
    ordered = sorted(starts)
    for i, va in enumerate(ordered):
        limit = ordered[i + 1] if i + 1 < len(ordered) else va + 4 * 512
        slots = []
        p = va
        while p < limit:
            t = b.u32(p)
            if t is None or t not in g.funcs:
                break
            slots.append(t)
            p += 4
        if len(slots) > 3:
            g.vtables[va] = (starts[va], slots)


def stage2(g):
    b = g.b
    for addr, (name, kind) in g.sdk.items():
        add(g, addr, name, 1, f"XDK library ({kind})")

    find_class_vtables(g)
    owners = {}
    for va, (cls, slots) in g.vtables.items():
        add(g, va, f"{cls}::`vftable'", 1, f"vtable of {cls} (slot 3 returns \"{cls}\")", kind="vtable")
        for i, fn in enumerate(slots):
            owners.setdefault(fn, set()).add((cls, i))
    for va, (cls, slots) in g.vtables.items():
        add(g, slots[3], f"{cls}::GetTypeName", 1, f"returns \"{cls}\" (vtable slot 3)")
    for fn, users in owners.items():
        classes = {c for c, i in users}
        if len(classes) == 1:
            cls, i = next(iter(users))
            add(g, fn, f"{cls}::vfunc_{i}", 3, f"only in {cls}'s vtable (slot {i}); method name unknown")
            g.entries[fn].cls = cls

    index = binary.string_index(g.funcs)
    for text, users in index.items():
        m = METHOD_TEXT.match(text)
        if m and len(users) == 1:
            add(g, users[0], m.group(1), 1, f"uses the text \"{text[:60]}\"")
        if SOURCE_PATH.search(text) and ("\\" in text or "/" in text):
            path = text.split("\\\\")[-1] if "\\\\" in text else text
            for u in users:
                g.files.setdefault(u, set()).add(path)
    for addr, files in g.files.items():
        if addr not in g.entries:
            add(g, addr, f"sub_{addr:08X}", 3, "unnamed; source file from its own text")
        g.entries[addr].file = "; ".join(sorted(files))
    for addr, e in g.entries.items():
        f = g.funcs.get(addr)
        e.size = f.size if f else 0


def write(g, path):
    with open(path, "w", encoding="utf-8") as out:
        out.write("# Star Wars Episode III: Revenge of the Sith (Xbox, retail) -- reconstructed symbols (tools/symbols)\n")
        out.write("# level: 1 = official, from this executable; 2 = official name, strongly matched from Indiana Jones;\n")
        out.write("#        3 = weakly matched / structural. Every entry lists its evidence.\n")
        out.write("# address\tkind\tlevel\tname\tsize\tsource file\tevidence\n")
        for addr in sorted(g.entries):
            e = g.entries[addr]
            clean = lambda x: re.sub(r"[\t\r\n]+", " ", x)
            out.write(f"{addr:08X}\t{e.kind}\t{e.level}\t{clean(e.name)}\t{e.size}\t{clean(e.file or '')}\t"
                      f"{clean(' | '.join(e.evidence))}\n")
