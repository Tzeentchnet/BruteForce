"""Reviewable movement-frame classification for the benchmark.

The frame-statistics CSV alone has no world/HUD or fade labels.  An optional
local JSON manifest can instead describe individually reviewed images:

    {
      "region": [left, top, right, bottom],
      "exclude": [[left, top, right, bottom]],
      "tolerance": 0,
      "review": {
        "kind": "real",
        "reviewer": "name",
        "method": "how each label was established",
        "region_validation": "why this region excludes HUD",
        "alignment_validation": "how images were matched to flip IDs and times",
        "presentation_validation": "why these images represent presented frames",
        "complete": true
      },
      "frames": [
        {"flip": 1, "t": 0.0, "image": "frame0.bmp", "label": "world"},
        {"flip": 2, "t": 0.1, "image": "frame1.bmp", "label": "hud_only"}
      ]
    }

Rectangles use exclusive right/bottom coordinates. Labels are assertions by
the reviewer, not inferred from pixel intensity. "world" starts a reviewed
baseline; later world frames need world_changed, world_static, or hud_only.
Other labels are intentional_fade, intentional_solid, known_black, invalid,
dark_unknown, and unknown. A solid or black image is not necessarily an
intentional transition. Image paths are relative to the manifest.
Flip-ID-tagged benchmark dumps can be aligned with the frame-statistics CSV;
legacy numbered dumps still need independent alignment. Neither establishes
the actual presented image.
"""

import json
import math
from pathlib import Path


KNOWN_BLACK_FRAME_HASHES = frozenset(("f22163d4abcd6aa4", "c1b9dd37072f3e56"))
FLIP_ID_ALIGNMENT = "flip_id_filename_v1"

LABELS = frozenset((
    "world", "world_changed", "world_static", "hud_only", "known_black",
    "intentional_fade", "intentional_solid", "invalid", "dark_unknown", "unknown",
))
WORLD_CATEGORIES = frozenset(("world_baseline", "world_changed", "world_static", "hud_only"))
CATEGORIES = (
    "world_baseline", "world_changed", "world_static", "hud_only",
    "known_black", "intentional_fade", "intentional_solid", "invalid",
    "dark_unknown", "solid_unknown", "unchanged_unknown", "unknown",
)
UNCLASSIFIED = frozenset((
    "known_black", "dark_unknown", "solid_unknown", "unchanged_unknown", "unknown",
))
DARK_MAX_CHANNEL = 16
MAX_INTERVALS = 64


class WorldLabelError(ValueError):
    pass


def unreviewed_metrics(rows):
    """Hashes identify some black and unchanged images, never their cause."""
    details = []
    previous = None
    for row in rows:
        hashed = row["distinct"] >= 0
        signature = row["hash"].lower() if hashed else None
        consecutive = (previous is not None and
                       (row.get("flip") is None or previous.get("flip") is None or
                        row["flip"] == previous["flip"] + 1))
        if signature in KNOWN_BLACK_FRAME_HASHES:
            category = "known_black"
        elif (consecutive and row["distinct"] == 0 and
              previous["distinct"] >= 0 and signature == previous["hash"].lower()):
            category = "unchanged_unknown"
        else:
            category = "unknown"
        details.append({"classification": category, "flip": row.get("flip"), "t": row.get("t")})
        previous = row
    black = sum(frame["classification"] == "known_black" for frame in details)
    return {
        "status": "inconclusive",
        "source": "whole-frame hash metadata; no reviewed movement images",
        "reason": ("Known-black and unchanged signatures do not establish an intentional "
                   "fade, valid world, missing character, or presented pixels."),
        "frames": len(rows),
        "known_black_frames": black,
        "intentional_fade_frames": None,
        "intentional_solid_frames": None,
        "invalid_frames": None,
        "unknown_frames": len(rows),
        "unchanged_unknown_frames": sum(
            frame["classification"] == "unchanged_unknown" for frame in details),
        "unclassified_intervals": _unclassified_intervals(details),
        "reviewed_world_frames": 0,
        "reviewed_world_changes": 0,
        "reviewed_world_changes_per_second": None,
        "reviewed_world_low_1pct_fps": None,
        "reviewed_world_cadence": None,
    }


def _unclassified_intervals(details):
    """Return bounded consecutive unclassified spans; times cover observed endpoints only."""
    intervals = []
    current = None
    total = 0
    for frame in details:
        category, flip, timestamp = frame["classification"], frame.get("flip"), frame.get("t")
        adjacent = (current is not None and category == current["classification"] and
                    (flip is None or current["end_flip"] is None or
                     flip == current["end_flip"] + 1))
        if category not in UNCLASSIFIED:
            current = None
            continue
        if not adjacent:
            total += 1
            current = {
                "classification": category, "start_flip": flip, "end_flip": flip,
                "start_t": timestamp, "end_t": timestamp, "frames": 0,
            }
            if len(intervals) < MAX_INTERVALS:
                intervals.append(current)
        current["frames"] += 1
        current["end_flip"] = flip
        current["end_t"] = timestamp
    for interval in intervals:
        start, end = interval["start_t"], interval["end_t"]
        interval["observed_seconds"] = (
            round(end - start, 3) if isinstance(start, (int, float)) and
            isinstance(end, (int, float)) and math.isfinite(start) and math.isfinite(end)
            and end >= start else None)
    return {
        "scope": "unclassified observed flips; endpoint spans exclude the trailing interval",
        "intervals": intervals, "total": total, "omitted": total - len(intervals),
    }


def _rectangle(value, name):
    if (not isinstance(value, list) or len(value) != 4 or
            any(type(n) is not int for n in value)):
        raise WorldLabelError(f"{name} must be [left, top, right, bottom] integers")
    left, top, right, bottom = value
    if left < 0 or top < 0 or left >= right or top >= bottom:
        raise WorldLabelError(f"{name} is empty or has negative coordinates")
    return tuple(value)


def _mask(size, region, exclusions):
    from PIL import Image, ImageDraw

    width, height = size
    if region[2] > width or region[3] > height:
        raise WorldLabelError("region is outside the captured image")
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    draw.rectangle((region[0], region[1], region[2] - 1, region[3] - 1), fill=255)
    for rect in exclusions:
        if (rect[0] < region[0] or rect[1] < region[1] or
                rect[2] > region[2] or rect[3] > region[3]):
            raise WorldLabelError("an exclusion is outside the selected region")
        draw.rectangle((rect[0], rect[1], rect[2] - 1, rect[3] - 1), fill=0)
    if not mask.getbbox():
        raise WorldLabelError("exclusions remove the entire selected region")
    return mask


def _pixel_changes(before, after, mask, tolerance):
    from PIL import ImageChops

    diff = ImageChops.difference(before, after)
    red, green, blue = diff.split()
    maximum = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    selected = maximum.point(lambda value: 255 if value > tolerance else 0)
    selected = ImageChops.multiply(selected, mask)
    return selected.histogram()[255], diff.getbbox() is not None


def _percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def _world_cadence(details):
    intervals = []
    previous_world_time = None
    segments = 0
    for frame in details:
        category = frame["classification"]
        if category == "world_baseline":
            previous_world_time = frame["t"]
            segments += 1
        elif category == "world_changed" and previous_world_time is not None:
            intervals.append((frame["t"] - previous_world_time) * 1000)
            previous_world_time = frame["t"]
        elif category not in ("world_static", "hud_only"):
            previous_world_time = None
    if not intervals:
        return None
    p99 = _percentile(intervals, 0.99)
    return {
        "scope": ("reviewer-labeled baseline-to-change and change-to-change intervals; "
                  "trailing incomplete intervals are excluded"),
        "frames": len(intervals),
        "segments": segments,
        "seconds": round(sum(intervals) / 1000, 3),
        "frame_ms_p50": round(_percentile(intervals, 0.5), 2),
        "frame_ms_p90": round(_percentile(intervals, 0.9), 2),
        "frame_ms_p99": round(p99, 2),
        "low_1pct_fps": round(1000 / p99, 3) if p99 else None,
    }


def analyze_manifest(path):
    """Classify a local manifest without treating unreviewed pixels as world motion."""
    try:
        from PIL import Image, UnidentifiedImageError
    except ImportError as exc:
        raise WorldLabelError("reviewed image analysis requires Pillow") from exc

    path = Path(path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise WorldLabelError(f"cannot read world-label manifest {path}: {exc}") from exc
    if not isinstance(manifest, dict):
        raise WorldLabelError("world-label manifest must be an object")
    region = _rectangle(manifest.get("region"), "region")
    exclusions = manifest.get("exclude", [])
    if not isinstance(exclusions, list):
        raise WorldLabelError("exclude must be a list of rectangles")
    exclusions = [_rectangle(value, "exclusion") for value in exclusions]
    tolerance = manifest.get("tolerance", 0)
    if type(tolerance) is not int or not 0 <= tolerance <= 255:
        raise WorldLabelError("tolerance must be an integer from 0 to 255")
    frames = manifest.get("frames")
    if not isinstance(frames, list) or not frames:
        raise WorldLabelError("frames must be a nonempty list")
    review = manifest.get("review", {})
    if not isinstance(review, dict):
        raise WorldLabelError("review must be an object")

    counts = dict.fromkeys(CATEGORIES, 0)
    details = []
    previous = None
    previous_category = None
    previous_flip = previous_t = None
    size = mask = None
    raw_compared = raw_changed = solid_black = 0
    aligned = True
    for item in frames:
        if not isinstance(item, dict):
            raise WorldLabelError("each frame must be an object")
        flip, timestamp = item.get("flip"), item.get("t")
        if type(flip) is not int or flip < 0:
            raise WorldLabelError("each frame needs a nonnegative integer flip ID")
        if (type(timestamp) not in (int, float) or not math.isfinite(timestamp) or
                (previous_t is not None and timestamp <= previous_t)):
            raise WorldLabelError("frame times must be finite and strictly increasing")
        if previous_flip is not None and flip <= previous_flip:
            raise WorldLabelError("flip IDs must be strictly increasing")
        consecutive = previous_flip is None or flip == previous_flip + 1
        aligned &= consecutive
        label = item.get("label", "unknown")
        if not isinstance(label, str) or label not in LABELS:
            raise WorldLabelError(f"unrecognized frame label: {label!r}")

        image = None
        error = None
        image_path = item.get("image")
        if image_path is not None:
            if not isinstance(image_path, str) or not image_path:
                raise WorldLabelError("image must be a nonempty path string")
            try:
                with Image.open(path.parent / image_path) as source:
                    image = source.convert("RGB")
            except FileNotFoundError:
                error = "image missing"
            except (OSError, UnidentifiedImageError, ValueError):
                error = "image invalid or unreadable"
        else:
            error = "image not supplied"
        if image is not None:
            if size is None:
                size = image.size
                mask = _mask(size, region, exclusions)
            elif image.size != size:
                image = None
                error = "image dimensions differ"

        region_pixels = None
        whole_changed = None
        if image is not None and previous is not None and consecutive:
            region_pixels, whole_changed = _pixel_changes(previous, image, mask, tolerance)
            raw_compared += 1
            raw_changed += region_pixels > 0
        extrema = image.getextrema() if image is not None else None
        is_solid = extrema is not None and all(low == high for low, high in extrema)
        is_black = is_solid and all(low == 0 for low, _ in extrema)
        is_dark = extrema is not None and max(high for _, high in extrema) <= DARK_MAX_CHANNEL
        solid_black += is_black

        if label == "invalid":
            category = "invalid"
        elif error:
            category = "unknown" if error in ("image missing", "image not supplied") else "invalid"
        elif not consecutive:
            category, error = "unknown", "flip gap: the preceding frame is not available"
        elif label == "intentional_fade":
            category = "intentional_fade"
        elif label == "intentional_solid":
            if is_solid:
                category = "intentional_solid"
            else:
                category, error = "invalid", "intentional-solid label conflicts with image pixels"
        elif is_black:
            if label in ("world", "world_changed", "world_static", "hud_only"):
                category, error = "invalid", "world label conflicts with a solid-black image"
            else:
                category = "known_black"
        elif label == "known_black":
            category, error = "invalid", "known-black label conflicts with image pixels"
        elif is_solid and label in ("world", "world_changed", "world_static", "hud_only"):
            category, error = "invalid", "world label conflicts with a solid image"
        elif label == "dark_unknown":
            category = "dark_unknown"
        elif label == "unknown":
            category = ("dark_unknown" if is_dark else "solid_unknown" if is_solid
                        else "unchanged_unknown" if whole_changed is False else "unknown")
        elif label == "world":
            category = "world_baseline" if previous_category not in WORLD_CATEGORIES else "unknown"
            if category == "unknown":
                error = "world transition was not labeled"
        elif previous_category not in WORLD_CATEGORIES or region_pixels is None:
            category, error = "unknown", "world transition has no reviewed preceding world frame"
        elif label == "world_changed":
            if region_pixels:
                category = "world_changed"
            else:
                category, error = "invalid", "world-change label has no change in the selected region"
        elif label == "world_static":
            if not region_pixels:
                category = "world_static"
            else:
                category, error = "invalid", "static-world label conflicts with region pixel changes"
        else:
            if not region_pixels and whole_changed:
                category = "hud_only"
            else:
                category, error = "invalid", "HUD-only label conflicts with the selected region or image"

        counts[category] += 1
        details.append({
            "flip": flip, "t": timestamp, "image": image_path, "label": label,
            "classification": category,
            "region_pixels_over_tolerance": region_pixels, "solid_black_pixels": is_black,
            "solid_pixels": is_solid, "dark_pixels": is_dark,
            "reason": error,
        })
        previous, previous_category = image, category
        previous_flip, previous_t = flip, timestamp

    review_complete = (
        review.get("kind") == "real" and review.get("complete") is True and
        all(isinstance(review.get(key), str) and review[key].strip() for key in
            ("reviewer", "method", "region_validation", "alignment_validation",
             "presentation_validation"))
    )
    classified = not (counts["invalid"] or any(counts[category] for category in UNCLASSIFIED))
    duration = frames[-1]["t"] - frames[0]["t"]
    world_transitions = counts["world_changed"] + counts["world_static"] + counts["hud_only"]
    eligible = (review_complete and classified and aligned and duration > 0 and
                world_transitions > 0 and counts["world_changed"] > 0)
    cadence = _world_cadence(details) if eligible else None
    raw_complete = len(frames) > 1 and raw_compared == len(frames) - 1 and aligned
    return {
        "status": "reviewer_asserted" if eligible else "inconclusive",
        "reason": ("Individual labels and alignment are reviewer assertions, not an automated "
                   "proof of world FPS." if eligible else
                   "Complete real-image labels, at least one reviewed world change, and "
                   "validated region, flip alignment, and presented-image source are required. "
                   "Unexplained black, dark, solid, or unchanged frames remain inconclusive."),
        "review": review,
        "region": list(region), "exclude": [list(rect) for rect in exclusions],
        "image_dimensions": list(size) if size else None,
        "tolerance": tolerance, "frames": len(frames), "counts": counts,
        "observed_solid_black_frames": solid_black,
        "known_black_frames": counts["known_black"],
        "intentional_fade_frames": counts["intentional_fade"],
        "intentional_solid_frames": counts["intentional_solid"],
        "invalid_frames": counts["invalid"],
        "unknown_frames": sum(counts[category] for category in UNCLASSIFIED),
        "unclassified_intervals": _unclassified_intervals(details),
        "dark_heuristic": f"all RGB channels <= {DARK_MAX_CHANNEL}; not a fade or world classifier",
        "reviewed_world_frames": sum(counts[category] for category in WORLD_CATEGORIES),
        "reviewed_world_changes": counts["world_changed"],
        "reviewed_world_changes_per_second": (
            round(counts["world_changed"] / duration, 3) if eligible else None),
        "reviewed_world_low_1pct_fps": cadence["low_1pct_fps"] if cadence else None,
        "reviewed_world_cadence": cadence,
        "raw_region_pixel_changes": {
            "changed_transitions": raw_changed, "compared_transitions": raw_compared,
            "changes_per_second": round(raw_changed / duration, 3) if raw_complete and duration > 0 else None,
            "diagnostic_only": True, "not_a_world_frame_measure": True,
        },
        "consecutive_flip_ids": aligned,
        "frame_details": details,
    }
