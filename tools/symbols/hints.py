"""Stage 4: compares earlier hand research against the results. Hints never name anything:
each is reported as agreeing, differing (for review) or not covered.

Ghidra: a TSV exported with ghidra/ExportUserSymbols.java (user-defined names only).
Names like "___EDIT_ICharacter::Teleport_FUN_00152e60" are reduced to "ICharacter::Teleport";
"UNK_" marks names their author considered tentative.
ReClass: class names from a .reclass project.
"""
import re


def ghidra_name(raw):
    n = raw.split("::", 1)[-1] if raw.startswith("xboxkrnl.exe::") else raw
    n = re.sub(r"^_*EDIT_", "", n)
    n = re.sub(r"_?(FUN|DAT|LAB|PTR_LAB|PTR)_[0-9a-fA-F]{6,8}$", "", n)
    n = re.sub(r"_[0-9a-fA-F]{8}$", "", n)
    tentative = "UNK_" in n
    n = n.replace("UNK_", "")
    return n.strip("_"), tentative


def method_of(name):
    return name.rsplit("::", 1)[-1].lower()


def compare_ghidra(g, tsv_path):
    rows = []
    for line in open(tsv_path, encoding="utf-8"):
        parts = line.rstrip("\n").split("\t")
        if parts[0] != "SYM" or parts[3] != "USER_DEFINED":
            continue
        try:
            addr = int(parts[1], 16)
        except ValueError:
            continue
        name, tentative = ghidra_name(parts[4])
        if not name:
            continue
        e = g.entries.get(addr)
        if e is None or e.name.startswith("sub_") or "::vfunc_" in e.name:
            verdict = "not covered"
        elif method_of(e.name) == method_of(name) or e.name.lower() == name.lower():
            verdict = "agrees"
        else:
            verdict = "differs"
        rows.append((addr, name, tentative, verdict, e.name if e else "", e.level if e else ""))
    return rows


def compare_reclass(g, path):
    text = open(path, encoding="utf-8", errors="replace").read()
    classes = sorted(set(re.findall(r'<Class Name="([IT][A-Za-z0-9_]+)"', text)))
    known = {cls for cls, slots in g.vtables.values()}
    return [(c, "a class vtable in the game" if c in known else "not found as a class") for c in classes]
