"""Lists the engine's console variables and commands, as registered by the game's own code.

    python tools/engine_vars.py [build/bin/GameData/default.xbe]

The options initializer (0x2409A0, the counterpart of Indiana Jones' TGameOptions::InitConsole)
registers every variable through the variable registry's virtual functions: name, the field
in the options object (`lea ecx, [esi + offset]`), a description and limits. This parses those
calls and prints one row per variable.
"""
import os
import re
import sys

import capstone

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "symbols"))
import binary  # noqa: E402

INIT = 0x2409A0


REGISTRY_CALL = bytes.fromhex("FF92A0000000")  # call dword ptr [edx + 0xA0]: get the variable registry


def registering_functions(b):
    """Starts of functions that fetch the variable registry (they register variables)."""
    text = next(s for s in b.sections if s[0] == ".text")
    lo, data = text[1], text[3]
    starts = sorted(a for a in binary_starts(b))
    found = set()
    pos = data.find(REGISTRY_CALL)
    import bisect
    while pos >= 0:
        a = lo + pos
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0:
            found.add(starts[i])
        pos = data.find(REGISTRY_CALL, pos + 1)
    return sorted(found)


def binary_starts(b):
    import realign
    text = next(s for s in b.sections if s[0] == ".text")
    return realign.function_starts(b, text)


def parse_all(xbe_path):
    b = binary.load_xbe(xbe_path, (".text",))
    rows = []
    for fn in registering_functions(b):
        for r in parse_function(b, fn):
            rows.append(r + (fn,))
    return rows


def parse(xbe_path, init=INIT):
    return parse_function(binary.load_xbe(xbe_path, (".text",)), init)


def parse_function(b, init, size=0x4000):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rows, pushes, field = [], [], None
    for ins in md.disasm(b.read(init, size), init):
        if ins.mnemonic == "push":
            pushes.append(ins.op_str)
        elif ins.mnemonic == "lea" and "[esi" in ins.op_str:
            m = re.search(r"\[esi(?: \+ (0x[0-9a-f]+|\d+))?\]", ins.op_str)
            field = int(m.group(1), 0) if m and m.group(1) else 0
        elif ins.mnemonic == "call" and "[edx +" in ins.op_str and pushes:
            slot = int(re.search(r"\+ (0x[0-9a-f]+|\d+)", ins.op_str).group(1), 0)
            strings = []
            values = []
            for p in pushes:
                if p.startswith("0x") or p.isdigit():
                    v = int(p, 0)
                    s = b.cstring(v) if b.is_data(v) else None
                    (strings if s else values).append(s if s else v)
                elif p == "esi":
                    field = 0 if field is None else field
            if strings:
                # Arguments are pushed last-first: the name is the last string pushed.
                name = strings[-1]
                desc = strings[0] if len(strings) > 1 else ""
                rows.append((name, slot, field, desc, values))
            pushes, field = [], None
        elif ins.mnemonic == "ret":
            break
        elif ins.mnemonic == "call":
            pushes = []
    return rows


def _main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    path = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else os.path.join(root, "build", "bin", "GameData", "default.xbe")
    kinds = {0x24: "integer", 0x28: "decimal", 0x30: "on/off"}
    kinds[0x04] = "command"
    ident = __import__("re").compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
    rows = [r for r in parse_all(path) if r[1] in kinds and ident.match(r[0])]
    print("| Variable | Type | Description | Registered in |")
    print("|---|---|---|---|")
    for name, slot, field, desc, values, fn in rows:
        print(f"| `{name}` | {kinds.get(slot, hex(slot))} | {desc} | `{fn:08X}` |")
    if "--args" in sys.argv:
        print()
        types = {0x0: "integer", 0x4: "decimal", 0x8: "text", 0xC: "on/off"}
        print("| Launch argument | Type | Field in [[0x66F7A4]+4] | Read in |")
        print("|---|---|---|---|")
        for name, field, slot, fn in sorted(set(launch_arguments(path)), key=lambda r: r[1]):
            if slot in types:  # registry registrations (+0x24/+0x28/+0x30) are listed above
                print(f"| `{name}` | {types[slot]} | `+0x{field:X}` | `{fn:08X}` |")


def launch_arguments(xbe_path):
    """Launch arguments (Default_Xbox.cfg): code that asks the argument list for a value by name,
    passing the address of a field in the settings object ([[0x66F7A4]+4]) to fill:
        add ecx, FIELD / push ecx / push "name" / mov ecx, eax / call [edx + SLOT]
    Returns [(name, field, slot, function)]."""
    import realign
    b = binary.load_xbe(xbe_path, (".text",))
    text = next(s for s in b.sections if s[0] == ".text")
    funcs = binary.analyze(b, realign.function_starts(b, text))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out = []
    for a, f in funcs.items():
        if b"\xa4\xf7\x66\x00" not in (b.read(a, f.size) or b""):
            continue  # does not use the settings object pointer 0x66F7A4
        ins = list(md.disasm(b.read(a, f.size), a))
        for i in range(3, len(ins) - 2):
            p, c = ins[i], ins[i + 2]
            if p.mnemonic != "push" or not p.op_str.startswith("0x") or c.mnemonic != "call" or "[edx" not in c.op_str:
                continue
            name = b.cstring(int(p.op_str, 16)) if b.is_data(int(p.op_str, 16)) else None
            if not name or ins[i - 1].op_str != "ecx" or ins[i - 1].mnemonic != "push":
                continue
            prev = ins[i - 2]
            m = re.match(r"(?:add ecx, (0x[0-9a-f]+|\d+))", f"{prev.mnemonic} {prev.op_str}")
            if not m:
                continue
            slot = re.search(r"\+ (0x[0-9a-f]+|\d+)\]", c.op_str)
            out.append((name, int(m.group(1), 0), int(slot.group(1), 0) if slot else 0, a))
    return out


if __name__ == "__main__":
    _main()
