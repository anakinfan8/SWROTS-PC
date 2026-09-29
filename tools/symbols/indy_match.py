"""Stage 3: carries official names from Indiana Jones and the Emperor's Tomb (same engine
and developer) over to the game, using the re-aligned 1.01 maps from stage 1.

Methods, strongest first:
  A. Vtables: a class present in both games. Our slot 3 and Indy's GetTypeName slot
     pin the two vtables to each other; slot pairs are compared by code.
  B. Shared distinctive strings used by exactly one function on each side.
  C. Identical code (ignoring addresses), unique on both sides.
  D. Call graph: callees of matched pairs, by position, when both call lists line up.
A name is level 2 when the code comparison or several strings back it, else level 3.
"""
import os
import re
import struct

import binary
import mapfile
import realign
import swrots

IMAGES = ("GCore.dll", "G_Indy.sgl", "GScript_Indy.dll", "R_D3D.dll")
COMPILER_NAMES = ("??_E", "??_G", "_$E", "??_H", "??_I", "??_L", "??_M", "??__E", "??__F")


class Indy:
    """All Indy binaries: functions keyed by (image, address), with their 1.01 names."""

    def __init__(self, bin_dir, map_dir):
        self.funcs = {}
        self.names = {}  # key -> (mangled names, status)
        self.images = {}
        for image in IMAGES:
            b = binary.load_pe(os.path.join(bin_dir, image))
            self.images[image] = b
            text = next(s for s in b.sections if s[0] == ".text")
            for addr, f in binary.analyze(b, realign.function_starts(b, text)).items():
                self.funcs[(image, addr)] = f
            path = os.path.join(map_dir, image + ".1.01.map")
            if not os.path.exists(path):
                continue
            for line in open(path, encoding="utf-8"):
                if line.startswith("#"):
                    continue
                addr, status, old, name, dem = line.rstrip("\n").split("\t")
                key = (image, int(addr, 16))
                names, st = self.names.get(key, ([], status))
                names.append(name)
                self.names[key] = (names, status)

    def best_name(self, key, cls=None):
        """(short name, full signature, all aliases) of an Indy function, or None. Identical
        functions were merged by the linker under several names; prefer one of class `cls`."""
        if key not in self.names:
            return None
        names, status = self.names[key]
        preferred = [n for n in names if not n.startswith(COMPILER_NAMES)] or names
        if cls:
            own = [n for n in preferred if mapfile.short_name(n).rsplit("::", 1)[0] == cls]
            preferred = own or preferred
        n = preferred[0]
        return mapfile.short_name(n), mapfile.demangle(n), [mapfile.short_name(x) for x in names]


def similar(a, b):
    """How alike two functions' code is: 'same' (identical bytes but addresses), 'shape'
    (same instruction sequence), 'size' (similar size and calls) or None."""
    if a is None or b is None:
        return None
    if a.fingerprint == b.fingerprint:
        return "same"
    if a.shape == b.shape:
        return "shape"
    # The Xbox build's code is typically 20-40% more compact than the PC build's.
    if a.ninsn and b.ninsn and 0.5 <= a.ninsn / b.ninsn <= 2.0:
        ca, cb = len(a.calls), len(b.calls)
        if abs(ca - cb) <= max(1, max(ca, cb) // 4):
            return "size"
    return None


def indy_class_getters(indy):
    """{class name: [(image, getter address, [data locations pointing to it])]}"""
    out = {}
    for (image, addr), f in indy.funcs.items():
        if f.getter and binary.CLASS_NAME.match(f.getter.encode("latin1")):
            out.setdefault(f.getter, []).append((image, addr))
    locs = {}
    for image, b in indy.images.items():
        wanted = {addr for (img, addr), f in indy.funcs.items() if img == image and f.getter}
        for name, va, size, data, is_code in b.sections:
            if is_code:
                continue
            for off in range(0, size - 3, 4):
                p = struct.unpack_from("<I", data, off)[0]
                if p in wanted:
                    locs.setdefault((image, p), []).append(va + off)
    return {cls: [(img, a, locs.get((img, a), [])) for img, a in keys] for cls, keys in out.items()}


def name_entry(g, addr, indy, key, level, evidence, cls=None):
    best = indy.best_name(key, cls)
    if not best:
        return False
    short, full, aliases = best
    methods = {a.rsplit("::", 1)[-1] for a in aliases}
    if len(methods) > 1:
        level = max(level, 3)  # merged functions with different names: which one is ours is a guess
        evidence += f" (Indy merged {len(aliases)} identical functions here)"
    swrots.add(g, addr, short, level, evidence + f" [Indy {key[0]} {key[1]:08X}: {full}]")
    return True


def shared_prefix(sims, stop=3):
    """Slots up to the first run of `stop` mismatches: where the two vtables stop agreeing
    (derived classes add their own virtual methods after the shared ones)."""
    miss = 0
    for i, s in enumerate(sims):
        miss = 0 if s else miss + 1
        if miss >= stop:
            return i - stop + 1
    return len(sims)


def match_vtables(g, indy, pairs):
    getters = indy_class_getters(indy)
    report = []
    for va, (cls, slots) in g.vtables.items():
        if cls not in getters:
            continue
        best = None
        for image, gaddr, locs in getters[cls]:
            b = indy.images[image]
            for loc in locs:
                # Indy vtable pinned so that its getter slot lines up with our slot 3.
                base = loc - 12
                cand = []
                for i, fn in enumerate(slots):
                    p = b.u32(base + 4 * i)
                    cand.append((image, p) if p is not None and (image, p) in indy.funcs else None)
                sims = [similar(g.funcs.get(fn), indy.funcs.get(k)) if k else None for fn, k in zip(slots, cand)]
                # Slots 0-3 are tiny type-info helpers; judge on the slots after them.
                n = shared_prefix(sims[4:]) + 4
                score = sum(1 for x in sims[4:n] if x)
                if best is None or score > best[0]:
                    best = (score, n, cand, sims, image)
        if not best:
            continue
        score, n, cand, sims, image = best
        accepted = score >= 4
        report.append((cls, len(slots), n, score, accepted))
        if not accepted:
            continue
        for i in range(min(n, len(slots))):
            fn, k, s = slots[i], cand[i], sims[i]
            if not k or i == 3:
                continue
            level = 2 if s else 3
            if name_entry(g, fn, indy, k, level, f"{cls} vtable slot {i} (code: {s or 'different'})", cls):
                pairs[fn] = (k, level)
    return report


def distinctive(text):
    if len(text) < 8 or not re.search(r"[A-Za-z]{3}", text):
        return False
    stripped = re.sub(r"%[-+ #0-9.]*[a-zA-Z]", "", text)
    return len(stripped.strip()) >= 6


def match_strings(g, indy, pairs):
    ours = binary.string_index(g.funcs)
    theirs = {}
    for key, f in indy.funcs.items():
        for s in set(f.strings):
            theirs.setdefault(s, []).append(key)
    votes = {}
    for text, users in ours.items():
        if len(users) != 1 or not distinctive(text):
            continue
        other = theirs.get(text)
        if not other or len(other) != 1:
            continue
        votes.setdefault(users[0], {}).setdefault(other[0], []).append(text)
    # Mutual best pairs.
    back = {}
    for fn, cands in votes.items():
        for key, texts in cands.items():
            w = sum(min(len(t), 40) for t in texts)
            if w > back.get(key, (0, None))[0]:
                back[key] = (w, fn)
    named = 0
    for fn, cands in votes.items():
        key, texts = max(cands.items(), key=lambda kv: sum(min(len(t), 40) for t in kv[1]))
        if back.get(key, (0, None))[1] != fn:
            continue
        strong = len(texts) >= 2 or (len(texts[0]) >= 20)
        s = similar(g.funcs.get(fn), indy.funcs.get(key))
        level = 2 if strong or s in ("same", "shape") else 3
        quoted = "; ".join(f'"{t[:40]}"' for t in texts[:3])
        if name_entry(g, fn, indy, key, level, f"shares {len(texts)} distinctive string(s): {quoted}"):
            pairs.setdefault(fn, (key, level))
            named += 1
    return named


def match_tokens(g, indy, pairs, rounds=6):
    """Rare shared features (strings, constants, calls to matched functions); iterated so each
    round's matches become features for the next. Validated at ~97% on independent vtable pairs,
    100% at score >= 6 (level 2); weaker scores are level 3."""
    import tokens
    known = {a: k for a, (k, level) in pairs.items()}
    named = 0
    for _ in range(rounds):
        found = tokens.match(g.funcs, indy.funcs, similar, known)
        new = {a: v for a, v in found.items() if a not in known}
        if not new:
            break
        for a, (key, score, shared) in new.items():
            known[a] = key
            level = 2 if score >= 6 else 3
            kinds = sorted({x[0] for x in shared})
            what = {"s": "strings", "f": "float constants", "i": "integer constants", "c": "calls to matched functions"}
            if name_entry(g, a, indy, key, level,
                          f"shares {len(shared)} rare features ({', '.join(what[k] for k in kinds)}; score {score:.1f})"):
                pairs.setdefault(a, (key, level))
                named += 1
    return named


def match_code(g, indy, pairs):
    def unique(funcs, keyfn):
        seen, dup = {}, set()
        for k, f in funcs.items():
            if f.size < 48:
                continue
            h = keyfn(f)
            if h in seen:
                dup.add(h)
            seen[h] = k
        return {h: k for h, k in seen.items() if h not in dup}
    ours = unique(g.funcs, lambda f: f.fingerprint)
    theirs = unique(indy.funcs, lambda f: f.fingerprint)
    named = 0
    for h, fn in ours.items():
        if h in theirs and fn not in pairs:
            if name_entry(g, fn, indy, theirs[h], 2, "identical code (ignoring addresses), unique in both games"):
                pairs[fn] = (theirs[h], 2)
                named += 1
    return named


def match_calls(g, indy, pairs, rounds=3):
    named = 0
    for _ in range(rounds):
        new = {}
        for fn, (key, level) in list(pairs.items()):
            a, b = g.funcs.get(fn), indy.funcs.get(key)
            if not a or not b or len(a.calls) != len(b.calls) or not a.calls:
                continue
            for ca, cb in zip(a.calls, b.calls):
                kb = (key[0], cb)
                if ca in pairs or ca in new or kb not in indy.funcs or ca not in g.funcs:
                    continue
                s = similar(g.funcs[ca], indy.funcs[kb])
                if s is None:
                    continue
                new[ca] = (kb, 3, f"called at the same position by {g.entries.get(fn).name if fn in g.entries else hex(fn)} "
                                  f"(code: {s})")
        if not new:
            break
        for ca, (kb, level, why) in new.items():
            if name_entry(g, ca, indy, kb, level, why):
                pairs[ca] = (kb, level)
                named += 1
    return named


def stage3(g, indy):
    pairs = {}
    report = match_vtables(g, indy, pairs)
    n_vt = len(pairs)
    n_str = match_strings(g, indy, pairs)
    n_tok = match_tokens(g, indy, pairs)
    n_code = match_code(g, indy, pairs)
    n_calls = match_calls(g, indy, pairs)
    n_over = propagate_overrides(g)
    return {"vtable slots": n_vt, "strings": n_str, "rare features": n_tok, "identical code": n_code,
            "call graph": n_calls,
            "overrides": n_over,
            "vtables compared": len(report), "vtables accepted": sum(1 for r in report if r[4])}, report


def propagate_overrides(g):
    """Names class-specific virtual methods after the method their slot holds in a related
    class: if X's slot i is its own function, and in a class Y sharing X's base (several
    identical inherited slots) slot i is a named method M, X's function is X::M."""
    named = {}
    vt = list(g.vtables.values())
    use = slot_usage(g)
    # Shared ancestry is shown by identical non-stub slots: a generic stub ('return 0') used at
    # several slot positions appears in unrelated hierarchies.
    real = lambda fn: len(use.get(fn, ())) == 1 and fn in g.funcs and g.funcs[fn].ninsn > 4
    for cls, slots in vt:
        own = set(slots)
        for i, fn in enumerate(slots):
            e = g.entries.get(fn)
            if i < 4 or (e and e.level <= 2):
                continue
            method = None
            for other_cls, other in vt:
                if other_cls == cls or i >= len(other):
                    continue
                oe = g.entries.get(other[i])
                if not oe or oe.level > 2 or "::" not in oe.name:
                    continue
                shared = sum(1 for k in range(4, min(i, len(other))) if other[k] == slots[k] and real(slots[k]))
                if shared >= 3:
                    method = (oe.name.rsplit("::", 1)[1], other_cls, oe.name)
                    break
            if method and fn not in named:
                named[fn] = (f"{cls}::{method[0]}", f"overrides {method[2]} (slot {i}, shared base with {method[1]})")
    for fn, (name, why) in named.items():
        swrots.add(g, fn, name, 3, why)
        g.entries[fn].cls = name.rsplit("::", 1)[0]
    return len(named)


def slot_usage(g):
    """{function: set of vtable slot indices it appears at}: a function at several different
    indices is a generic stub (e.g. 'return 0'), not one specific method."""
    use = {}
    for cls, slots in g.vtables.values():
        for i, fn in enumerate(slots):
            use.setdefault(fn, set()).add(i)
    return use


def match_vtables_whole(g, indy, pairs, shifts=(-3, -2, -1, 1, 2, 3)):
    """Whole-vtable alignment with a chance test: the alignment pinned at the name getter must
    clearly beat the same vtable shifted by a few slots. Slots whose code was compiled out on
    the Xbox (stubs) don't count and aren't named."""
    getters = indy_class_getters(indy)
    use = slot_usage(g)
    stub = lambda fn: len(use.get(fn, ())) > 1 or g.funcs.get(fn) is None or g.funcs[fn].ninsn <= 4
    report = []
    for va, (cls, slots) in g.vtables.items():
        if cls not in getters:
            continue
        best = None
        for image, gaddr, locs in getters[cls]:
            b = indy.images[image]
            for loc in locs:
                base = loc - 12

                def key_at(i):
                    p = b.u32(base + 4 * i)
                    k = (image, p)
                    return k if p is not None and k in indy.funcs else None

                def score(d):
                    n = 0
                    for i in range(4, len(slots)):
                        if stub(slots[i]):
                            continue
                        k = key_at(i + d)
                        if k and similar(g.funcs[slots[i]], indy.funcs[k]):
                            n += 1
                    return n

                s0 = score(0)
                null = max(score(d) for d in shifts)
                if best is None or s0 - null > best[0] - best[1]:
                    best = (s0, null, image, [key_at(i) for i in range(len(slots))])
        if not best:
            continue
        s0, null, image, cand = best
        accepted = s0 >= 4 and s0 >= 2 * null + 2
        report.append((cls, len(slots), s0, null, accepted))
        if not accepted:
            continue
        # Classes of Indy's names at agreeing slots: the Indy class hierarchy of this vtable.
        family = {cls}
        for i in range(4, len(slots)):
            k = cand[i]
            if k and not stub(slots[i]) and similar(g.funcs[slots[i]], indy.funcs[k]):
                n = indy.best_name(k, cls)
                if n:
                    family.add(n[0].rsplit("::", 1)[0])
        for i in range(4, len(slots)):
            fn, k = slots[i], cand[i]
            if not k or stub(fn):
                continue
            n = indy.best_name(k, cls)
            if not n or n[0].rsplit("::", 1)[0] not in family:
                continue
            s = similar(g.funcs[fn], indy.funcs[k])
            level = 2 if s else 3
            if name_entry(g, fn, indy, k, level,
                          f"{cls} vtable slot {i}, whole-vtable alignment {s0} vs {null} by chance (code: {s or 'different'})",
                          cls):
                pairs.setdefault(fn, (k, level))
    return report


def independent_anchors(g, indy):
    """Pairs found without any vtable information: shared strings and rare constants."""
    import tokens
    tok = tokens.match(g.funcs, indy.funcs, similar)
    return {a: key for a, (key, score, shared) in tok.items()}


def vtable_layouts(g, indy, anchors):
    """For each of our class vtables with an Indy counterpart: the alignment and the slot range
    confirmed by agreeing anchors. Returns [(cls, slots, cand, lo, hi, agree, disagree)]."""
    getters = indy_class_getters(indy)
    out = []
    for va, (cls, slots) in g.vtables.items():
        if cls not in getters:
            continue
        best = None
        for image, gaddr, locs in getters[cls]:
            b = indy.images[image]
            for loc in locs:
                base = loc - 12
                cand = []
                for i in range(len(slots)):
                    p = b.u32(base + 4 * i)
                    cand.append((image, p) if p is not None and (image, p) in indy.funcs else None)
                agree = [i for i in range(4, len(slots)) if slots[i] in anchors and anchors[slots[i]] == cand[i]]
                disagree = [i for i in range(4, len(slots)) if slots[i] in anchors and cand[i]
                            and anchors[slots[i]] != cand[i] and anchors[slots[i]][0] == image]
                if best is None or len(agree) - len(disagree) > len(best[3]) - len(best[4]):
                    best = (cand, image, loc, agree, disagree)
        if not best:
            continue
        cand, image, loc, agree, disagree = best
        # Confirmed: from the getter (slot 3) to the last agreeing anchor, with no contradiction inside.
        hi = 3
        for i in agree:
            if any(d < i for d in disagree):
                break
            hi = i
        out.append((cls, slots, cand, 3, hi, agree, disagree))
    return out


def match_vtables_anchored(g, indy, pairs, anchors=None):
    anchors = anchors if anchors is not None else independent_anchors(g, indy)
    use = slot_usage(g)
    stub = lambda fn: len(use.get(fn, ())) > 1 or g.funcs.get(fn) is None or g.funcs[fn].ninsn <= 4
    report = []
    for cls, slots, cand, lo, hi, agree, disagree in vtable_layouts(g, indy, anchors):
        report.append((cls, len(slots), hi, len(agree), len(disagree), hi > 3))
        for i in range(4, hi + 1):
            fn, k = slots[i], cand[i]
            if not k or stub(fn) or fn in anchors:
                continue
            level = 2 if len([a for a in agree if a >= i]) >= 1 and len([a for a in agree if a <= i]) >= 1 else 3
            if name_entry(g, fn, indy, k, level,
                          f"{cls} vtable slot {i}; layout confirmed by {len(agree)} independent matches up to slot {hi}",
                          cls):
                pairs.setdefault(fn, (k, level))
    return report
