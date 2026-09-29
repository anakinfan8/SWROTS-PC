"""Static call-graph inspection for the retail SWROTS XBE.

For each requested function (address or SDK symbol name), walks the code by
recursive descent and reports what it reaches: other SDK functions, kernel
imports, NV2A register accesses (0xFD......) and privileged instructions.
Used to decide which XDK functions are pure CPU code that can run natively and
which touch hardware and need a native replacement.

Usage: python tools/xbe_callgraph.py <default.xbe> <name-or-0xaddr> [...] [--depth N]
"""
import re
import struct
import sys

import capstone

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])

SYMBOLS_INC = __file__.replace("tools", "src\\game").replace("xbe_callgraph.py", "sdk_symbols.inc")
KERNEL_NAMES = __file__.replace("tools", "src\\kernel").replace("xbe_callgraph.py", "kernel_names.inc")
PRIVILEGED = {"cli", "sti", "hlt", "in", "out", "wbinvd", "invlpg", "lgdt", "sgdt", "lidt", "rdmsr", "wrmsr"}


class Xbe:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        u32 = lambda o: struct.unpack_from("<I", self.d, o)[0]
        base = u32(0x104)
        self.secs = []
        for i in range(u32(0x11C)):
            fl, va, vs, ra, rs, na = struct.unpack_from("<6I", self.d, u32(0x120) - base + i * 0x38)
            self.secs.append((va, vs, ra, rs))
        self.thunk = u32(0x158) ^ 0x5B6D40B6
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def raw(self, a):
        for va, vs, ra, rs in self.secs:
            if va <= a < va + rs:
                return ra + a - va
        return None

    def u32(self, a):
        r = self.raw(a)
        return struct.unpack_from("<I", self.d, r)[0] if r is not None else None


def load_symbols():
    syms = {}
    for addr, name, kind in re.findall(r'SDK_SYMBOL\(0x([0-9A-F]+), "(\w+)", SymbolKind::(\w+)\)', open(SYMBOLS_INC).read()):
        syms[int(addr, 16)] = (name, kind)
    kn = {int(o): n for o, n in re.findall(r'KERNEL_NAME\((\d+), "(\w+)"\)', open(KERNEL_NAMES).read())}
    return syms, kn


def walk(xbe, entry, syms, kn, max_insns=4000):
    """Recursive descent over one function; returns (calls, kernel, nv2a, privileged)."""
    todo, seen = [entry], set()
    calls, kernel, nv2a, priv = set(), set(), set(), set()
    count = 0
    while todo and count < max_insns:
        a = todo.pop()
        while a not in seen and count < max_insns:
            r = xbe.raw(a)
            if r is None:
                break
            ins = next(xbe.md.disasm(xbe.d[r:r + 16], a, count=1), None)
            if ins is None:
                break
            seen.add(a)
            count += 1
            op = ins.op_str
            if ins.mnemonic in PRIVILEGED:
                priv.add(f"{ins.mnemonic}@{a:X}")
            for m in re.findall(r"0x(fd[0-9a-f]{6})\b", op):
                nv2a.add(f"{int(m, 16):X}@{a:X}")
            m = re.search(r"dword ptr \[0x([0-9a-f]+)\]", op)
            if m:
                t = int(m.group(1), 16)
                if xbe.thunk <= t < xbe.thunk + 0x400:
                    ordinal = xbe.u32(t)
                    if ordinal is not None:
                        kernel.add(kn.get(ordinal & 0x7FFFFFFF, str(ordinal & 0x7FFFFFFF)))
            if ins.mnemonic == "call":
                if op.startswith("0x"):
                    calls.add(int(op, 16))
            elif ins.mnemonic == "jmp":
                if op.startswith("0x"):
                    t = int(op, 16)
                    if t in syms and t != entry:
                        calls.add(t)  # tail call
                    else:
                        todo.append(t)
                break
            elif ins.mnemonic.startswith("j") and op.startswith("0x"):
                todo.append(int(op, 16))
            elif ins.mnemonic in ("ret", "retf", "int3"):
                break
            a = ins.address + ins.size
    return calls, kernel, nv2a, priv


def describe(xbe, syms, kn, target, depth, indent=0, visited=None):
    visited = visited if visited is not None else set()
    name = syms.get(target, (f"sub_{target:X}", "?"))[0]
    if target in visited:
        print("  " * indent + f"{name} (seen)")
        return
    visited.add(target)
    calls, kernel, nv2a, priv = walk(xbe, target, syms, kn)
    flags = []
    if kernel:
        flags.append("kernel: " + ", ".join(sorted(kernel)))
    if nv2a:
        flags.append("NV2A: " + ", ".join(sorted(nv2a))[:80])
    if priv:
        flags.append("PRIV: " + ", ".join(sorted(priv)))
    print("  " * indent + f"{name} @{target:X}" + ("  [" + "; ".join(flags) + "]" if flags else ""))
    if depth > 0:
        for c in sorted(calls):
            describe(xbe, syms, kn, c, depth - 1, indent + 1, visited)


def main():
    args = sys.argv[1:]
    depth = 3
    if "--depth" in args:
        i = args.index("--depth")
        depth = int(args[i + 1])
        del args[i:i + 2]
    xbe = Xbe(args[0])
    syms, kn = load_symbols()
    by_name = {v[0]: k for k, v in syms.items()}
    for t in args[1:]:
        addr = int(t, 16) if t.startswith("0x") else by_name[t]
        describe(xbe, syms, kn, addr, depth)
        print()


if __name__ == "__main__":
    main()
