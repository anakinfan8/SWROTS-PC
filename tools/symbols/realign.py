"""Stage 1: re-align an MSVC .map from an earlier build onto a later build of the same binary.

The Indiana Jones .map files are from the 1.0 release; the installed DLLs are
the 1.01 patch. The linker kept the function order, but every changed function
shifts everything after it, so the 1.0 -> 1.01 address shift is piecewise
constant. We walk the map in order, keeping the current shift, and place a name
only where "1.0 address + shift" is a function start in 1.01. Where that fails,
we look nearby for the shift that makes the following map functions land on
function starts as well.

Every placement is then checked against the 1.01 binary itself:
  - X::GetTypeName must be a function returning the string "X";
  - every function a vtable points to must have a virtual (or compiler-generated
    vtable) name.
Each placed name is 'verified' (a check passed), 'consistent' (placed, no check
applies) or 'contradicted' (a check failed; the name is dropped).
"""
import bisect

import binary
import mapfile

VTABLE_HELPERS = ("??_E", "??_G", "__purecall", "_purecall")
LOOKAHEAD = 16
NEED = 13
WINDOW = 0x3000


def function_starts(b, text):
    """16-byte aligned addresses right after padding or a return: where MSVC places functions."""
    name, lo, size, data, is_code = text
    starts = set()
    for off in range(16, len(data), 16):
        p = data[off - 1]
        if p in (0x90, 0xCC) or p == 0xC3 or (data[off - 3] == 0xC2 and p == 0x00):
            if data[off] not in (0x00, 0x90, 0xCC):
                starts.add(lo + off)
    return starts


def realign(bin_path, map_path):
    b = binary.load_pe(bin_path)
    m = mapfile.MapFile(map_path)
    text = next(s for s in b.sections if s[0] == ".text")
    starts = function_starts(b, text)
    starts_sorted = sorted(starts)
    funcs = binary.analyze(b, starts | {e for e in [b.entry] if e})

    map_funcs = m.functions()
    map_addrs = sorted(a for a in map_funcs if a % 16 == 0)

    # Fixed points: X::GetTypeName where exactly one 1.01 function returns "X"
    # and the 1.0 map has exactly one GetTypeName for X.
    getters_by_string = {}
    for f in funcs.values():
        if f.getter:
            getters_by_string.setdefault(f.getter, []).append(f.addr)
    map_getters = {}
    for old, names in map_funcs.items():
        for n in names:
            if n.startswith("?GetTypeName@"):
                map_getters.setdefault(mapfile.short_name(n).rsplit("::", 1)[0], []).append(old)
    anchors = {}
    for cls, olds in map_getters.items():
        new_addrs = getters_by_string.get(cls, [])
        if len(olds) == 1 and len(new_addrs) == 1:
            anchors[olds[0]] = new_addrs[0]

    def score(i, delta):
        return sum(1 for k in range(i, min(i + LOOKAHEAD, len(map_addrs))) if map_addrs[k] + delta in starts)

    placed = {}
    delta = 0
    unplaced = 0
    for i, old in enumerate(map_addrs):
        if old in anchors:
            delta = anchors[old] - old
        elif old + delta not in starts or score(i, delta) < NEED:
            # The smallest change of shift that explains the following functions.
            best = None
            lo = bisect.bisect_left(starts_sorted, old + delta - WINDOW)
            hi = bisect.bisect_right(starts_sorted, old + delta + WINDOW)
            for s in sorted(starts_sorted[lo:hi], key=lambda s: abs(s - old - delta)):
                if score(i, s - old) >= NEED:
                    best = s - old
                    break
            if best is None:
                unplaced += 1
                continue
            delta = best
        if old + delta not in placed:
            placed[old + delta] = old

    # Gaps: a name left unplaced between two placed neighbours is tried at their shifts.
    by_old = sorted((o, n) for n, o in placed.items())
    olds = [o for o, n in by_old]
    filled = 0
    for old in map_addrs:
        k = bisect.bisect_left(olds, old)
        if k < len(olds) and olds[k] == old:
            continue
        for j in (k - 1, k):
            if 0 <= j < len(by_old):
                cand = old + by_old[j][1] - by_old[j][0]
                if cand in starts and cand not in placed:
                    placed[cand] = old
                    filled += 1
                    break

    # Checks against the 1.01 binary. Class vtables: runs of function pointers
    # that include a name getter (GetTypeName).
    class_vtable_funcs = set()
    for va, run in binary.find_vtables(b, set(funcs)):
        if any(funcs[p].getter for p in run if p in funcs):
            class_vtable_funcs.update(run)
    result = {}
    contradicted = []
    stats = {"map functions": len(map_addrs), "anchors": len(anchors), "placed": 0, "verified": 0,
             "consistent": 0, "contradicted": 0, "unplaced": unplaced - filled}
    for addr, old in placed.items():
        names = map_funcs[old]
        f = funcs.get(addr)
        status = "consistent"
        getters = [n for n in names if n.startswith("?GetTypeName@")]
        if getters and f is not None:
            # Class name without namespace (GUI::TControl -> TControl).
            classes = {mapfile.short_name(n).rsplit("::", 1)[0].rsplit("::", 1)[-1] for n in getters}
            if f.getter in classes:
                status = "verified"
            elif f.getter is None or binary.CLASS_NAME.match(f.getter.encode("latin1")):
                status = "contradicted"  # not a getter, or returns another class's name
        elif addr in class_vtable_funcs:
            ok = any(n.startswith(VTABLE_HELPERS) or "virtual" in mapfile.demangle(n) for n in names)
            status = "verified" if ok else "contradicted"
        stats["placed"] += 1
        stats[status] += 1
        if status != "contradicted":
            result[addr] = (names, status, old)
        else:
            contradicted.append((addr, old, names, f.getter if f else None))
    stats["contradicted list"] = contradicted
    return b, funcs, result, stats


def write_map(path, image_name, result):
    with open(path, "w", encoding="utf-8") as out:
        out.write(f"# {image_name} 1.01: names from the 1.0 .map, re-aligned and checked (tools/symbols)\n")
        out.write("# address\tstatus\t1.0 address\tname\tdemangled\n")
        for addr in sorted(result):
            names, status, old = result[addr]
            for n in names:
                out.write(f"{addr:08X}\t{status}\t{old:08X}\t{n}\t{mapfile.demangle(n)}\n")
