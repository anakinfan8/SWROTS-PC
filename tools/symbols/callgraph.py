"""Call-graph propagation: from matched function pairs (ours <-> Indy) to their callees and callers.

  - Callees: the two call lists are lined up at calls whose targets are already matched
    (anchors); between two anchors, gaps of equal length are paired call by call.
  - Callers: when both functions of a pair have exactly one unmatched caller, those callers pair.
Every proposed pair must pass the code similarity test, and a mapping is only kept when all
proposals for it agree. `support` counts how many different matched neighbours proposed it.
"""


def reverse_calls(funcs, key_of):
    callers = {}
    for k, f in funcs.items():
        for t in set(f.calls):
            callers.setdefault(key_of(k, t), set()).add(k)
    return callers


def align(A, B, pairs, ours, theirs, similar, limit=160):
    """Global alignment of two call lists (Needleman-Wunsch). Known pairs score 3, similar
    functions 1, gaps -0.4. Returns aligned index pairs of calls that are not known yet."""
    if not A or not B or len(A) > limit or len(B) > limit:
        return []
    n, m = len(A), len(B)
    def sc(i, j):
        if pairs.get(A[i]) == B[j]:
            return 3.0
        fa, fb = ours.get(A[i]), theirs.get(B[j])
        if A[i] in pairs or fa is None or fb is None:
            return -1.0
        return 1.0 if similar(fa, fb) else -1.0
    gap = -0.4
    S = [[0.0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        S[i][0] = i * gap
    for j in range(1, m + 1):
        S[0][j] = j * gap
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            S[i][j] = max(S[i - 1][j - 1] + sc(i - 1, j - 1), S[i - 1][j] + gap, S[i][j - 1] + gap)
    out = []
    i, j = n, m
    while i > 0 and j > 0:
        if S[i][j] == S[i - 1][j - 1] + sc(i - 1, j - 1):
            if sc(i - 1, j - 1) == 1.0:
                out.append((i - 1, j - 1))
            i, j = i - 1, j - 1
        elif S[i][j] == S[i - 1][j] + gap:
            i -= 1
        else:
            j -= 1
    return out


def propagate(ours, theirs, seeds, similar, rounds=30):
    """ours: {addr: Function}; theirs: {(image, addr): Function}; seeds: {addr: key}.
    Returns {addr: (key, support, first reason)} for new pairs (seeds excluded)."""
    pairs = dict(seeds)
    taken = {v: k for k, v in pairs.items()}
    our_callers = reverse_calls(ours, lambda k, t: t)
    their_callers = reverse_calls(theirs, lambda k, t: (k[0], t))
    found = {}

    def propose(a, kb, why, proposals):
        if a not in ours or kb not in theirs or a in pairs:
            return
        if similar(ours[a], theirs[kb]) is None:
            return
        proposals.setdefault(a, []).append((kb, why))

    for _ in range(rounds):
        proposals = {}
        for fa, kb in list(pairs.items()):
            fa_f, kb_f = ours.get(fa), theirs.get(kb)
            if not fa_f or not kb_f:
                continue
            image = kb[0]
            A, B = fa_f.calls, [(image, t) for t in kb_f.calls]
            for i, j in align(A, B, pairs, ours, theirs, similar):
                propose(A[i], B[j], f"callee of {fa:08X}", proposals)
            # Callers.
            uc = [c for c in our_callers.get(fa, ()) if c not in pairs]
            tc = [c for c in their_callers.get(kb, ()) if c not in taken]
            if len(uc) == 1 and len(tc) == 1:
                propose(uc[0], tc[0], f"only caller of {fa:08X}", proposals)
        added = 0
        for a, props in proposals.items():
            keys = {kb for kb, why in props}
            if len(keys) != 1:
                continue  # neighbours disagree
            kb = next(iter(keys))
            if kb in taken:
                continue
            support = len({why for kb2, why in props})
            pairs[a] = kb
            taken[kb] = a
            found[a] = (kb, support, props[0][1])
            added += 1
        if not added:
            break
    return found
