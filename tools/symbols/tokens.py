"""Matching by rare shared features (compiler-independent): distinctive strings, floating-point
constants and unusual integer constants a function uses, plus calls to functions that are
already matched. Each shared feature weighs more the rarer it is in both games; a pair is
accepted only when each function is the other's clear best candidate.
"""
import math
import re

STRING_WEIGHT = 2.0
FLOAT_WEIGHT = 1.0
INT_WEIGHT = 0.5
CALL_WEIGHT = 1.0
MAX_DF = 6


def distinctive(text):
    if len(text) < 6 or not re.search(r"[A-Za-z]{3}", text):
        return False
    return len(re.sub(r"%[-+ #0-9.]*[a-zA-Z]", "", text).strip()) >= 5


def features(f, key_of_call=None):
    out = set()
    for s in f.strings:
        if distinctive(s):
            out.add(("s", s))
    for c in f.consts:
        out.add(("f" if c.startswith("f:") else "i", c))
    if key_of_call:
        for t in f.calls:
            k = key_of_call(t)
            if k is not None:
                out.add(("c", k))
    return out


def match(ours, theirs, similar, known=None, margin=0.65, min_score=3.0):
    """ours: {addr: Function}; theirs: {key: Function}; known: {our addr: their key} (pairs
    already established; used as call features and excluded from results).
    Returns {addr: (key, score, [shared features])}."""
    known = known or {}
    taken = set(known.values())
    their_addr = {}
    for key in theirs:
        their_addr.setdefault(key[1], []).append(key)
    fo = {a: features(f, lambda t: ("pair", known[t]) if t in known else None) for a, f in ours.items() if a not in known}
    ft = {}
    back = {v: k for k, v in known.items()}
    for key, f in theirs.items():
        if key in taken:
            continue
        ft[key] = features(f, lambda t, img=key[0]: ("pair", (img, t)) if (img, t) in back else None)
    df_o, df_t = {}, {}
    for fs in fo.values():
        for x in fs:
            df_o[x] = df_o.get(x, 0) + 1
    index = {}
    for key, fs in ft.items():
        for x in fs:
            df_t[x] = df_t.get(x, 0) + 1
            index.setdefault(x, []).append(key)
    n_o, n_t = max(1, len(fo)), max(1, len(ft))

    def weight(x):
        base = {"s": STRING_WEIGHT, "f": FLOAT_WEIGHT, "i": INT_WEIGHT, "c": CALL_WEIGHT}[x[0]]
        return base * (math.log(n_o / df_o[x]) + math.log(n_t / df_t[x])) / 10.0

    def candidates(fs):
        scores = {}
        for x in fs:
            if x not in df_t or df_o.get(x, 0) > MAX_DF or df_t[x] > MAX_DF:
                continue
            w = weight(x)
            for key in index[x]:
                s = scores.setdefault(key, [0.0, []])
                s[0] += w
                s[1].append(x)
        return scores

    best_o = {}
    for a, fs in fo.items():
        sc = candidates(fs)
        if not sc:
            continue
        ranked = sorted(sc.items(), key=lambda kv: -kv[1][0])
        top_key, (top, shared) = ranked[0]
        second = ranked[1][1][0] if len(ranked) > 1 else 0.0
        if top >= min_score and second <= margin * top:
            best_o[a] = (top_key, top, shared)
    # Mutual best: the chosen Indy function must not prefer another of ours.
    best_t = {}
    for a, (key, score, shared) in best_o.items():
        if key not in best_t or score > best_t[key][1]:
            best_t[key] = (a, score)
    out = {}
    for a, (key, score, shared) in best_o.items():
        if best_t[key][0] == a and similar(ours[a], theirs[key]) is not None:
            out[a] = (key, score, shared)
    return out
