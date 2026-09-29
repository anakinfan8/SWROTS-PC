"""Measures a matching method's accuracy by hiding some trusted pairs and checking whether the
method re-derives them correctly.

    python tools/symbols/validate.py callgraph
"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import callgraph  # noqa: E402
import indy_match  # noqa: E402
import swrots  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
INDY = r"C:\Program Files (x86)\Steam\steamapps\common\Indiana Jones and the Emperors Tomb\GameData\bin"


def trusted_pairs(g, indy):
    """Level 2 pairs from vtables (with similar code) and strong string matches."""
    pairs = {}
    indy_match.match_vtables(g, indy, pairs)
    indy_match.match_strings(g, indy, pairs)
    return {a: key for a, (key, level) in pairs.items() if level == 2}


def main(method):
    g = swrots.Game(os.path.join(ROOT, "build", "bin", "GameData", "default.xbe"),
                    os.path.join(ROOT, "src", "game", "sdk_symbols.inc"))
    swrots.stage2(g)
    indy = indy_match.Indy(INDY, os.path.join(ROOT, "build", "bin", "symbols", "indy"))
    trusted = trusted_pairs(g, indy)
    print("trusted pairs:", len(trusted))
    totals = {"right": 0, "wrong": 0, "missed": 0}
    by_support = {}
    for trial in range(5):
        rnd = random.Random(trial)
        keys = sorted(trusted)
        rnd.shuffle(keys)
        hidden = set(keys[: len(keys) // 5])
        seeds = {a: trusted[a] for a in keys if a not in hidden}
        found = callgraph.propagate(g.funcs, indy.funcs, seeds, indy_match.similar)
        for a in hidden:
            if a not in found:
                totals["missed"] += 1
                continue
            ok = found[a][0] == trusted[a]
            totals["right" if ok else "wrong"] += 1
            s = min(found[a][1], 3)
            by_support.setdefault(s, [0, 0])[0 if ok else 1] += 1
    print("hidden trusted pairs re-derived:", totals)
    for s in sorted(by_support):
        r, w = by_support[s]
        print(f"  support {s}{'+' if s == 3 else ''}: right {r}, wrong {w}, precision {r / max(1, r + w):.1%}")
    found = callgraph.propagate(g.funcs, indy.funcs, trusted, indy_match.similar)
    print("new pairs from all trusted seeds:", len(found))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "callgraph")
