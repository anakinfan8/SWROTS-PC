"""Builds the project's symbol tables from the player's own files (see tools/symbols/README.md).

    python tools/symbols/build_symbols.py indy [--indy <Indiana Jones folder>]
    python tools/symbols/build_symbols.py game [--xbe build/bin/GameData/default.xbe]
                                               [--ghidra-hints <tsv>] [--reclass-hints <file>]

`indy` (stage 1): re-aligns Indiana Jones and the Emperor's Tomb's 1.0 .map files
onto its installed 1.01 binaries (one verified map per binary, plus a report).
`game` (stages 2-4): the game's own official facts, names carried over from the
Indy maps, and the comparison with earlier hand research; writes swrots.map and
report.md. Output goes to build/bin/symbols and is never committed: it is
derived from LucasArts' files.
"""
import collections
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hints  # noqa: E402
import indy_match  # noqa: E402
import realign  # noqa: E402
import swrots  # noqa: E402

DEFAULT_INDY = r"C:\Program Files (x86)\Steam\steamapps\common\Indiana Jones and the Emperors Tomb"
INDY_IMAGES = [
    ("GCore.dll", "GCore.map", "engine core"),
    ("G_Indy.sgl", "G_Indy.map", "game code"),
    ("GScript_Indy.dll", "GScript_Indy.map", "script bindings"),
    ("R_D3D.dll", "R_D3D.map", "Direct3D renderer"),
    ("indy.exe", "indy.map", "host program"),
]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def stage_indy(indy_dir, out_dir):
    bin_dir = os.path.join(indy_dir, "GameData", "bin")
    os.makedirs(os.path.join(out_dir, "indy"), exist_ok=True)
    lines = ["# Indiana Jones 1.01 symbol re-alignment", "",
             "Names come from the 1.0 `.map` files shipped with the game; addresses are for the installed 1.01",
             "binaries. `verified`: a check against the 1.01 binary passed (GetTypeName returns the class name,",
             "or a class vtable points to a virtual method). `consistent`: placed by the address-shift alignment,",
             "no check applies. `contradicted`: a check failed, name dropped. `unplaced`: no safe position.", "",
             "| Binary | Role | Map functions | Placed | Verified | Consistent | Contradicted | Unplaced |",
             "|---|---|---|---|---|---|---|---|"]
    for image, mapname, role in INDY_IMAGES:
        path = os.path.join(bin_dir, image)
        if not os.path.exists(path):
            lines.append(f"| {image} | {role} | missing | | | | | |")
            continue
        b = realign.binary.load_pe(path)
        if any(s[0] == ".bind" for s in b.sections):
            # SteamStub DRM: the code section is encrypted on disk.
            lines.append(f"| {image} | {role} | (encrypted by Steam DRM, skipped) | | | | | |")
            continue
        t = time.time()
        _, funcs, result, st = realign.realign(path, os.path.join(bin_dir, mapname))
        contradicted = st.pop("contradicted list")
        realign.write_map(os.path.join(out_dir, "indy", image + ".1.01.map"), image, result)
        with open(os.path.join(out_dir, "indy", image + ".contradicted.txt"), "w", encoding="utf-8") as f:
            for addr, old, names, getter in contradicted:
                f.write(f"{addr:08X}\t1.0 {old:08X}\tgetter={getter!r}\t{' / '.join(names)}\n")
        placed = st["placed"] - st["contradicted"]
        lines.append(f"| {image} | {role} | {st['map functions']} | {placed} | {st['verified']} | {st['consistent']} | "
                     f"{st['contradicted']} | {st['unplaced']} |")
        print(f"{image}: {st} ({time.time() - t:.0f} s)")
    with open(os.path.join(out_dir, "indy", "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("Wrote", os.path.join(out_dir, "indy"))


def stage_game(xbe, indy_dir, out_dir, ghidra_tsv, reclass):
    t = time.time()
    g = swrots.Game(xbe, os.path.join(ROOT, "src", "game", "sdk_symbols.inc"))
    swrots.stage2(g)
    level1 = collections.Counter(e.level for e in g.entries.values())
    indy_maps = os.path.join(out_dir, "indy")
    if not os.path.exists(os.path.join(indy_maps, "GCore.dll.1.01.map")):
        stage_indy(indy_dir, out_dir)
    indy = indy_match.Indy(os.path.join(indy_dir, "GameData", "bin"), indy_maps)
    st3, vt_report = indy_match.stage3(g, indy)
    swrots.write(g, os.path.join(out_dir, "swrots.map"))

    entries = list(g.entries.values())
    real = [e for e in entries if e.kind == "function" and not e.name.startswith("sub_") and "::vfunc_" not in e.name]
    by_level = collections.Counter(e.level for e in real)
    conflicts = [e for e in entries if any(x.startswith("conflict") for x in e.evidence)]
    lines = ["# swrots.map report", "",
             f"Game executable: `{xbe}`. {len(g.funcs)} functions found, {len(g.vtables)} class vtables.", "",
             "## Named functions by trust level", "",
             "| Level | Meaning | Functions |", "|---|---|---|",
             f"| 1 | official, from the game's own executable | {by_level.get(1, 0)} |",
             f"| 2 | official name, strongly matched from Indiana Jones | {by_level.get(2, 0)} |",
             f"| 3 | weakly matched or inferred (tentative) | {by_level.get(3, 0)} |", "",
             f"Also: {sum(1 for e in entries if '::vfunc_' in e.name)} virtual methods known only by class and slot "
             f"(`X::vfunc_N`), {sum(1 for e in entries if e.name.startswith('sub_'))} unnamed functions with a known "
             f"source file, {sum(1 for e in entries if e.kind == 'vtable')} vtables.", "",
             "## How names were carried over from Indiana Jones", "",
             "| Method | Functions |", "|---|---|"]
    for k in ("vtable slots", "strings", "rare features", "identical code", "call graph", "overrides"):
        lines.append(f"| {k} | {st3[k]} |")
    lines += ["", f"Vtables compared with Indy: {st3['vtables compared']}, accepted: {st3['vtables accepted']}.", "",
              f"## Conflicts ({len(conflicts)})", "", "Kept the stronger name; the other candidate is listed.", ""]
    for e in conflicts:
        other = next(x for x in e.evidence if x.startswith("conflict"))
        lines.append(f"- `{e.addr:08X}` {e.name} (level {e.level}) -- {other}")
    if ghidra_tsv and os.path.exists(ghidra_tsv):
        rows = hints.compare_ghidra(g, ghidra_tsv)
        c = collections.Counter(r[3] for r in rows)
        lines += ["", "## Earlier Ghidra research (hints only, never used for names)", "",
                  f"{len(rows)} hand-named symbols: {c.get('agrees', 0)} agree, {c.get('differs', 0)} differ, "
                  f"{c.get('not covered', 0)} not covered.", "", "| Address | Hint | Tentative | Verdict | swrots.map |",
                  "|---|---|---|---|---|"]
        for addr, name, tentative, verdict, ours, level in sorted(rows):
            lines.append(f"| `{addr:08X}` | {name} | {'yes' if tentative else ''} | {verdict} | "
                         f"{ours}{f' (L{level})' if level != '' else ''} |")
    if reclass and os.path.exists(reclass):
        rows = hints.compare_reclass(g, reclass)
        lines += ["", "## Earlier ReClass research (hints only)", ""]
        lines += [f"- {c}: {v}" for c, v in rows]
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"swrots.map: {len(real)} named functions {dict(by_level)}; {len(conflicts)} conflicts "
          f"({time.time() - t:.0f} s). Wrote {out_dir}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=["indy", "game"])
    ap.add_argument("--xbe", default=os.path.join(ROOT, "build", "bin", "GameData", "default.xbe"))
    ap.add_argument("--ghidra-hints", help="TSV from tools/symbols/ghidra/ExportUserSymbols.java")
    ap.add_argument("--reclass-hints", help="a ReClass .reclass project")
    ap.add_argument("--indy", default=DEFAULT_INDY, help="Indiana Jones and the Emperor's Tomb install folder")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "bin", "symbols"))
    a = ap.parse_args()
    if a.stage == "indy":
        stage_indy(a.indy, a.out)
    else:
        stage_game(a.xbe, a.indy, a.out, a.ghidra_hints, a.reclass_hints)


if __name__ == "__main__":
    main()
