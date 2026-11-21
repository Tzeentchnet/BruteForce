#!/usr/bin/env python3
"""Compare two equally sized frames without assuming either is correct.

Inputs, including PNG and BMP, are converted to 8-bit RGB; alpha is ignored.
No resizing, alignment, or color correction is performed. Reference and
candidate are neutral roles and can be labeled, for example, xemu and GPU.

Absolute-difference sums and means cover all RGB channel samples, including
unchanged pixels. The maximum is the largest individual channel difference.
"Over 8" counts pixels with any channel differing by more than 8/255,
independently of the selected tolerance. Regional differences cover every
non-identical grid cell, including partial cells at image edges; omitted cells
are identical. Over-8 components use four-neighbour connectivity and report
the twelve largest coherent areas. The optional mask is white above the
selected tolerance.
"""

import argparse
import json
import math
import operator
import os
import sys

from PIL import Image, ImageChops


def _is_path(value):
    return isinstance(value, (str, bytes, os.PathLike))


def _validate_inputs(path_a, path_b, tolerance, cell, diff_out, reference_label,
                     candidate_label):
    try:
        valid_tolerance = (not isinstance(tolerance, bool) and
                           math.isfinite(tolerance) and 0 <= tolerance <= 255)
    except (TypeError, ValueError, OverflowError):
        valid_tolerance = False
    if not valid_tolerance:
        raise ValueError("tolerance must be a finite number between 0 and 255")

    try:
        if isinstance(cell, bool):
            raise TypeError
        cell = operator.index(cell)
    except TypeError:
        raise ValueError("cell must be a positive integer") from None
    if cell <= 0:
        raise ValueError("cell must be a positive integer")

    for role, label in (("reference", reference_label), ("candidate", candidate_label)):
        if not isinstance(label, str) or not label.strip():
            raise ValueError(f"{role} label must be a non-empty string")
    for role, path in (("reference", path_a), ("candidate", path_b)):
        if not _is_path(path) and not callable(getattr(path, "read", None)):
            raise ValueError(f"{role} image must be a path or readable binary file")

    if diff_out is not None:
        if not _is_path(diff_out) and not callable(getattr(diff_out, "write", None)):
            raise ValueError("diff output must be a path or writable binary file")
        for path in (path_a, path_b):
            same_file = diff_out is path
            if _is_path(diff_out) and _is_path(path):
                try:
                    same_file = os.path.samefile(diff_out, path)
                except OSError:
                    same_file = (os.path.normcase(os.path.abspath(os.fsdecode(diff_out))) ==
                                 os.path.normcase(os.path.abspath(os.fsdecode(path))))
            if same_file:
                raise ValueError("diff output must not overwrite either input image")
    return cell


def _absolute_difference(total, maximum, pixel_count):
    return {
        "sum": total,
        "mean_per_channel": round(total / (3 * pixel_count), 6),
        "max_channel": maximum,
    }


def _over_8_components(mask, width, height):
    active = {}
    previous = []
    complete = []
    next_id = 0

    for y in range(height):
        runs = []
        x = 0
        row = y * width
        while x < width:
            while x < width and not mask[row + x]:
                x += 1
            if x == width:
                break
            start = x
            while x < width and mask[row + x]:
                x += 1
            runs.append((start, x))

        parent = {}

        def root(component):
            while component in parent:
                parent[component] = parent.get(parent[component], parent[component])
                component = parent[component]
            return component

        current = []
        previous_index = 0
        for start, end in runs:
            while (previous_index < len(previous)
                   and previous[previous_index][1] <= start):
                previous_index += 1
            overlaps = []
            scan = previous_index
            while scan < len(previous) and previous[scan][0] < end:
                component = root(previous[scan][2])
                if component not in overlaps:
                    overlaps.append(component)
                scan += 1

            if not overlaps:
                component = next_id
                next_id += 1
                active[component] = {
                    "x": start, "y": y, "right": end, "bottom": y + 1,
                    "pixels": 0,
                }
            else:
                component = overlaps[0]
                for other in overlaps[1:]:
                    other = root(other)
                    component = root(component)
                    if other == component:
                        continue
                    target = active[component]
                    merged = active.pop(other)
                    target["x"] = min(target["x"], merged["x"])
                    target["y"] = min(target["y"], merged["y"])
                    target["right"] = max(target["right"], merged["right"])
                    target["bottom"] = max(target["bottom"], merged["bottom"])
                    target["pixels"] += merged["pixels"]
                    parent[other] = component

            stats = active[root(component)]
            stats["x"] = min(stats["x"], start)
            stats["right"] = max(stats["right"], end)
            stats["bottom"] = y + 1
            stats["pixels"] += end - start
            current.append((start, end, root(component)))

        current = [(start, end, root(component))
                   for start, end, component in current]
        current_ids = {component for _, _, component in current}
        for component in list(active):
            if component not in current_ids:
                complete.append(active.pop(component))
        previous = current

    complete.extend(active.values())
    components = [{
        "x": stats["x"],
        "y": stats["y"],
        "width": stats["right"] - stats["x"],
        "height": stats["bottom"] - stats["y"],
        "pixels": stats["pixels"],
    } for stats in complete]
    components.sort(key=lambda item: (
        -item["pixels"], item["y"], item["x"], item["height"], item["width"]))
    return len(components), components[:12]


def compare(path_a, path_b, tolerance=8, cell=16, diff_out=None, *,
            reference_label="reference", candidate_label="candidate"):
    cell = _validate_inputs(path_a, path_b, tolerance, cell, diff_out,
                            reference_label, candidate_label)
    with Image.open(path_a) as source_a:
        a = source_a.convert("RGB")
    with Image.open(path_b) as source_b:
        b = source_b.convert("RGB")
    if a.size != b.size:
        raise ValueError(f"sizes differ: {a.size} vs {b.size}; inputs must match")
    w, h = a.size
    if w <= 0 or h <= 0:
        raise ValueError("images must have positive dimensions")
    diff = ImageChops.difference(a, b)
    pixels = diff.load()
    histogram = {"0": 0, "1-2": 0, "3-8": 0, "9-32": 0, "33-255": 0}
    over = 0
    over_8 = 0
    differing = 0
    total_difference = 0
    maximum_difference = 0
    cells = {}
    regional = {}
    over_8_mask = bytearray(w * h)
    mask = Image.new("L", a.size, 0) if diff_out is not None else None
    mask_px = mask.load() if mask else None
    for y in range(h):
        for x in range(w):
            channels = pixels[x, y]
            d = max(channels)
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

            if d:
                differing += 1
                absolute_sum = sum(channels)
                total_difference += absolute_sum
                maximum_difference = max(maximum_difference, d)
                key = (x // cell, y // cell)
                region = regional.get(key)
                if region is None:
                    region = regional[key] = {
                        "sum": 0, "max_channel": 0, "pixels_differing": 0,
                        "pixels_over_8": 0, "pixels_over_tolerance": 0,
                    }
                region["sum"] += absolute_sum
                region["max_channel"] = max(region["max_channel"], d)
                region["pixels_differing"] += 1
                region["pixels_over_8"] += d > 8
                region["pixels_over_tolerance"] += d > tolerance
            if d > 8:
                over_8 += 1
                over_8_mask[y * w + x] = 1
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
    regional_differences = []
    for (cx, cy), stats in sorted(regional.items(), key=lambda item: (item[0][1], item[0][0])):
        x, y = cx * cell, cy * cell
        width, height = min(cell, w - x), min(cell, h - y)
        regional_differences.append({
            "x": x, "y": y, "width": width, "height": height,
            "pixels_differing": stats["pixels_differing"],
            "pixels_over_8": stats["pixels_over_8"],
            "pixels_over_tolerance": stats["pixels_over_tolerance"],
            "absolute_difference": _absolute_difference(
                stats["sum"], stats["max_channel"], width * height),
        })
    pixel_count = w * h
    component_count, largest_components = _over_8_components(
        over_8_mask, w, h)
    return {
        "size": [w, h],
        "tolerance": tolerance,
        "pixels_over_tolerance": over,
        "fraction_over_tolerance": round(over / pixel_count, 6),
        "histogram": histogram,
        "cells_over_tolerance": len(cells),
        "largest_regions": regions[:12],
        "reference_label": reference_label,
        "candidate_label": candidate_label,
        "cell_size": cell,
        "pixels_differing": differing,
        "fraction_differing": round(differing / pixel_count, 6),
        "pixels_over_8": over_8,
        "fraction_over_8": round(over_8 / pixel_count, 6),
        "over_8_component_count": component_count,
        "largest_over_8_components": largest_components,
        "absolute_difference": _absolute_difference(
            total_difference, maximum_difference, pixel_count),
        "regional_differences": regional_differences,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference", help="first image, e.g. xemu or project output")
    parser.add_argument("candidate", help="second image, e.g. project output or xemu")
    parser.add_argument("--reference-label", default="reference",
                        help="name for the first image (default: reference)")
    parser.add_argument("--candidate-label", default="candidate",
                        help="name for the second image (default: candidate)")
    parser.add_argument("--tolerance", type=int, default=8,
                        help="per-channel threshold from 0 to 255 (default: 8)")
    parser.add_argument("--cell", type=int, default=16,
                        help="positive grid cell width and height in pixels (default: 16)")
    parser.add_argument("--diff", help="write a mask of pixels above the tolerance")
    args = parser.parse_args(argv)
    try:
        result = compare(args.reference, args.candidate, args.tolerance, args.cell, args.diff,
                         reference_label=args.reference_label,
                         candidate_label=args.candidate_label)
    except (OSError, ValueError, SyntaxError, Image.DecompressionBombError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
