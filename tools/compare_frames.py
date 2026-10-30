#!/usr/bin/env python3
"""Compare two captured frames (BMP) for the renderer-equivalence gate (G4).

Reports how many pixels differ beyond a tolerance, a histogram of the
largest per-channel difference, and the differing regions on a coarse grid,
so each difference can be listed and explained. Optionally writes an image
of the differences (white where the pictures differ beyond the tolerance).

Captures stay local: they are game frames.
"""

import argparse
import json
import sys

from PIL import Image, ImageChops


def compare(path_a, path_b, tolerance=8, cell=16, diff_out=None):
    a = Image.open(path_a).convert("RGB")
    b = Image.open(path_b).convert("RGB")
    if a.size != b.size:
        raise ValueError(f"sizes differ: {a.size} vs {b.size}")
    w, h = a.size
    diff = ImageChops.difference(a, b)
    pixels = diff.load()
    histogram = {"0": 0, "1-2": 0, "3-8": 0, "9-32": 0, "33-255": 0}
    over = 0
    cells = {}
    mask = Image.new("L", a.size, 0) if diff_out else None
    mask_px = mask.load() if mask else None
    for y in range(h):
        for x in range(w):
            d = max(pixels[x, y])
            if d == 0:
                histogram["0"] += 1
            elif d <= 2:
                histogram["1-2"] += 1
            elif d <= 8:
                histogram["3-8"] += 1
            elif d <= 32:
                histogram["9-32"] += 1
            else:
                histogram["33-255"] += 1
            if d > tolerance:
                over += 1
                key = (x // cell, y // cell)
                cells[key] = cells.get(key, 0) + 1
                if mask_px is not None:
                    mask_px[x, y] = 255
    if mask is not None:
        mask.save(diff_out)
    regions = sorted(({"x": cx * cell, "y": cy * cell, "pixels": n}
                      for (cx, cy), n in cells.items()), key=lambda r: -r["pixels"])
    return {
        "size": [w, h],
        "tolerance": tolerance,
        "pixels_over_tolerance": over,
        "fraction_over_tolerance": round(over / (w * h), 6),
        "histogram": histogram,
        "cells_over_tolerance": len(cells),
        "largest_regions": regions[:12],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", help="CPU-path capture")
    parser.add_argument("candidate", help="GPU-path capture")
    parser.add_argument("--tolerance", type=int, default=8)
    parser.add_argument("--cell", type=int, default=16)
    parser.add_argument("--diff", help="write a difference mask image here")
    args = parser.parse_args(argv)
    try:
        result = compare(args.reference, args.candidate, args.tolerance, args.cell, args.diff)
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
