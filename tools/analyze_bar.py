# -*- coding: utf-8 -*-
"""
analyze_bar.py — locate the strength-bar row in a screenshot and print its
horizontal color run-length encoding, to detect fill discontinuity.

Usage: py tools/analyze_bar.py <image.png>
Output: the bar row's y, and runs like [(blue, 640), (bg, 220)] — more than
one colored run (e.g. blue|bg|blue|bg) means a discontinuity/gap.
"""
import sys
from PIL import Image

TIER_COLORS = {
    "blue":  (0, 120, 215),    # #0078D7  strong
    "green": (16, 124, 16),    # #107C10  very strong
    "red":   (232, 17, 35),    # #E81123  weak
    "orange": (255, 140, 0),   # #FF8C00  medium
}
BG = (233, 233, 233)           # #E9E9E9


def close(a, b, tol=28):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def classify(px):
    for name, c in TIER_COLORS.items():
        if close(px, c):
            return name
    if close(px, BG):
        return "bg"
    return "?"


def main(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()

    # 1) find the bar row: scan lower 60% for a row containing a long colored run
    best_y, best_len = -1, 0
    for y in range(int(h * 0.4), h):
        run, longest = 0, 0
        for x in range(w):
            c = classify(px[x, y])
            if c in TIER_COLORS:
                run += 1
                longest = max(longest, run)
            else:
                run = 0
        if longest > best_len:
            best_len, best_y = longest, y
    if best_y < 0 or best_len == 0:
        print("no bar found")
        return 1

    # 2) run-length encode that row (ignore short noise runs)
    runs = []
    cur, n = None, 0
    for x in range(w):
        c = classify(px[x, best_y])
        if c == cur:
            n += 1
        else:
            if cur is not None and n > 0:
                runs.append((cur, n))
            cur, n = c, 1
    runs.append((cur, n))
    # merge tiny noise runs (<6 px) into neighbors
    merged = []
    for c, n in runs:
        if merged and n < 6:
            pc, pn = merged[-1]
            merged[-1] = (pc, pn + n)
        else:
            merged.append((c, n))

    colored = [r for r in merged if r[0] in TIER_COLORS]
    print(f"image={path} size={w}x{h} bar_row_y={best_y}")
    print("runs:", merged)
    print(f"colored_runs={len(colored)} -> "
          + ("DISCONTINUOUS (gap in fill)" if len(colored) > 1 else "continuous"))
    return 0 if len(colored) == 1 else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
