#!/usr/bin/env python3
"""Pairs of typing mistakes for spelltune, from a text as typed and the same
text corrected: "typed<TAB>right<TAB>kind" for each word that differs, and a
count of the kinds (missing diacritic, letter missing or too many and where,
swapped letters, neighbouring key, Shift, ...).

Usage: pairs.py typed.txt corrected.txt pairs.tsv
The words are paired along the longest common sequence; runs of a different
length (a space missing or added, a word added) are only counted.
"""

import collections
import difflib
import re
import sys
import unicodedata

WORD = re.compile(r"[^\W\d_]+(?:['’][^\W\d_]+)*")
ROWS = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
OFFSETS = [0, 0.25, 0.75]
FIRST_RIGHT = [5, 5.25, 5.75]


def words(path):
    with open(path, encoding="utf-8-sig") as f:
        return WORD.findall(f.read())


def fold(text):
    """Without diacritics or strokes, lower case."""
    out = []
    for c in text.lower():
        c = {"ł": "l", "ø": "o", "đ": "d", "ħ": "h", "ŧ": "t", "ı": "i"}.get(c, c)
        out.append(unicodedata.normalize("NFD", c)[0])
    return "".join(out)


def marked(c):
    return fold(c) != c.lower()


def key(c):
    c = fold(c)
    for row, letters in enumerate(ROWS):
        if c in letters:
            return row, OFFSETS[row] + letters.index(c)
    return None


def touch(a, b):
    p, q = key(a), key(b)
    if not p or not q or abs(p[0] - q[0]) > 1:
        return False
    return abs(p[1] - q[1]) < (1.1 if p[0] == q[0] else 0.9)


def hand(c):
    p = key(c)
    return -1 if not p else (0 if p[1] < FIRST_RIGHT[p[0]] else 1)


def where(i, n):
    return "start" if i == 0 else ("end" if i >= n - 1 else "middle")


def kind(typed, right):
    if typed.lower() == right.lower():
        return "case: first letter" if typed[1:] == right[1:] else "case: inner (Shift held, caps)"
    t, r = typed.lower(), right.lower()
    n, m = len(t), len(r)
    if n == m and fold(t) == fold(r):
        kinds = []
        for i, (a, b) in enumerate(zip(t, r)):
            if a == b:
                continue
            if not marked(a) and marked(b):
                kinds.append("diacritic missing")
            elif marked(a) and not marked(b):
                held = i > 0 and marked(t[i - 1])
                kinds.append("diacritic typed after a diacritic (AltGr held)" if held else "diacritic typed")
            else:
                kinds.append("diacritic wrong")
        return kinds[0] if len(kinds) == 1 else "diacritics: several"
    if n == m:
        diff = [i for i in range(n) if t[i] != r[i]]
        if len(diff) == 1:
            i = diff[0]
            if marked(t[i]) and fold(t[i]) != fold(r[i]):
                return "another letter with a diacritic (AltGr held?)"
            return "another letter: neighbouring key" if touch(t[i], r[i]) else "another letter: other key"
        if len(diff) == 2 and diff[1] == diff[0] + 1 and t[diff[0]] == r[diff[1]] and t[diff[1]] == r[diff[0]]:
            a, b = t[diff[0]], t[diff[1]]
            return "swap: two hands" if hand(a) >= 0 and hand(a) != hand(b) else "swap: one hand"
        if len(diff) >= 2:
            i, j = diff[0], diff[-1]
            if t[i + 1:j + 1] == r[i:j] or t[i:j] == r[i + 1:j + 1]:
                return f"letter moved by {j - i}"
    if n == m + 1:
        for i in range(n):
            if t[:i] + t[i + 1:] == r:
                x = t[i]
                around = ([r[i - 1]] if i > 0 else []) + ([r[i]] if i < m else [])
                if any(fold(x) == fold(y) for y in around):
                    how = "repeats a neighbour"
                else:
                    touching = [touch(x, y) for y in around]
                    how = ("touches both" if len(touching) == 2 and all(touching)
                           else "touches one" if any(touching) else "touches none")
                return f"letter too many at the {where(i, n)}: {how}"
    if n + 1 == m:
        for i in range(m):
            if r[:i] + r[i + 1:] == t:
                double = (i > 0 and r[i - 1] == r[i]) or (i + 1 < m and r[i + 1] == r[i])
                return f"letter missing at the {where(i, m)}" + (" (of a double letter)" if double else "")
    return "two or more edits"


def main():
    typed_path, right_path, out_path = sys.argv[1:4]
    typed, right = words(typed_path), words(right_path)
    pairs, other = [], 0
    matcher = difflib.SequenceMatcher(None, right, typed, autojunk=False)
    for tag, a1, a2, b1, b2 in matcher.get_opcodes():
        if tag == "replace" and a2 - a1 == b2 - b1:
            pairs += list(zip(typed[b1:b2], right[a1:a2]))
        elif tag != "equal":
            other += 1
    with open(out_path, "w", encoding="utf-8") as out:
        for t, r in pairs:
            out.write(f"{t}\t{r}\t{kind(t, r)}\n")
    counts = collections.Counter(kind(t, r) for t, r in pairs)
    print(f"{len(typed)} words typed, {len(pairs)} changed, {other} other changes (spaces, words added)")
    for name, count in counts.most_common():
        print(f"{count:6d} {100 * count / max(len(pairs), 1):5.1f}%  {name}")


if __name__ == "__main__":
    main()
