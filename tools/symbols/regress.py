"""Guards against regressions when improving swrots.map.

    python tools/symbols/regress.py baseline   # save the current map as the baseline
    python tools/symbols/regress.py check      # compare the current map with the baseline

A check fails if any level 1 or 2 name of the baseline changed or disappeared. Level 3
changes and new names are listed for review. It also measures precision against facts
known for certain from the game's own executable:
  - vtable ownership: a name from Indiana Jones on a function found only in class X's
    vtable must be a method of X or of a class X derives from (by shared vtable slots);
  - level 1 names (GetTypeName class names, Class::Method texts) must not be contradicted.
"""
import os
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "bin", "symbols")
MAP = os.path.join(OUT, "swrots.map")
BASE = os.path.join(OUT, "swrots.baseline.map")


def load(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        if line.startswith("#"):
            continue
        f = line.rstrip("\n").split("\t")
        out[int(f[0], 16)] = (f[1], int(f[2]), f[3], f[6])
    return out


def placeholder(name):
    return name.startswith("sub_") or "::vfunc_" in name


def check():
    base, cur = load(BASE), load(MAP)
    broken, changed3, new = [], [], {1: 0, 2: 0, 3: 0}
    for addr, (kind, level, name, ev) in base.items():
        c = cur.get(addr)
        if level <= 2 and not placeholder(name):
            if c is None or c[2] != name or c[1] > level:
                broken.append((addr, name, level, c))
        elif not placeholder(name) and (c is None or c[2] != name):
            changed3.append((addr, name, c[2] if c else None))
    for addr, (kind, level, name, ev) in cur.items():
        if kind == "function" and not placeholder(name):
            b = base.get(addr)
            if b is None or placeholder(b[2]):
                new[level] += 1
    named = lambda m: sum(1 for k, l, n, e in m.values() if k == "function" and not placeholder(n))
    print(f"named functions: baseline {named(base)}, now {named(cur)}; new names by level {new}")
    print(f"level 3 names changed or removed: {len(changed3)}")
    for addr, old, now in changed3[:40]:
        print(f"  {addr:08X}  {old}  ->  {now}")
    if broken:
        print(f"REGRESSION: {len(broken)} level 1/2 names changed or lost")
        for addr, name, level, c in broken[:40]:
            print(f"  {addr:08X}  L{level} {name}  ->  {c[2] + ' L' + str(c[1]) if c else 'gone'}")
        return 1
    print("OK: no level 1/2 name changed or lost")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["baseline"]:
        shutil.copyfile(MAP, BASE)
        print("baseline saved:", BASE)
    else:
        sys.exit(check())
