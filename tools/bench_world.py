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
Other labels are known_black, intentional_fade, invalid, dark_unknown, and
unknown. Image paths are relative to the manifest. Flip-ID-tagged benchmark
dumps can be aligned with the frame-statistics CSV; legacy numbered dumps still
need independent alignment. Neither establishes the actual presented image.
"""

import json
import math
from pathlib import Path


KNOWN_BLACK_FRAME_HASHES = frozenset(("f22163d4abcd6aa4", "c1b9dd37072f3e56"))
FLIP_ID_ALIGNMENT = "flip_id_filename_v1"

LABELS = frozenset((
    "world", "world_changed", "world_static", "hud_only", "known_black",
    "intentional_fade", "invalid", "dark_unknown", "unknown",
))
WORLD_CATEGORIES = frozenset(("world_baseline", "world_changed", "world_static", "hud_only"))
CATEGORIES = (
    "world_baseline", "world_changed", "world_static", "hud_only",
    "known_black", "intentional_fade", "invalid", "dark_unknown", "unknown",
)


class WorldLabelError(ValueError):
    pass


def unreviewed_metrics(rows):
    """Only exact known black hashes are identified without reviewed images."""
    black = sum(row["distinct"] >= 0 and
                row["hash"].lower() in KNOWN_BLACK_FRAME_HASHES for row in rows)
    return {
        "status": "inconclusive",
        "source": "whole-frame hash metadata; no reviewed movement images",
        "reason": "other frames, fades, invalid images, HUD, and world motion are unclassified",
        "frames": len(rows),
        "known_black_frames": black,
        "intentional_fade_frames": None,
        "invalid_frames": None,
        "unknown_frames": len(rows) - black,
        "reviewed_world_frames": 0,
        "reviewed_world_changes": 0,
        "reviewed_world_changes_per_second": None,
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
        is_black = image is not None and all(low == high == 0 for low, high in image.getextrema())
        solid_black += is_black

        if label == "invalid":
            category = "invalid"
        elif error:
            category = "unknown" if error in ("image missing", "image not supplied") else "invalid"
        elif not consecutive:
            category, error = "unknown", "flip gap: the preceding frame is not available"
        elif label == "intentional_fade":
            category = "intentional_fade"
        elif is_black:
            if label in ("world", "world_changed", "world_static", "hud_only"):
                category, error = "invalid", "world label conflicts with a solid-black image"
            else:
                category = "known_black"
        elif label == "known_black":
            category, error = "invalid", "known-black label conflicts with image pixels"
        elif label in ("unknown", "dark_unknown"):
            category = label
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
    classified = not (counts["invalid"] or counts["unknown"] or counts["dark_unknown"])
    duration = frames[-1]["t"] - frames[0]["t"]
    world_transitions = counts["world_changed"] + counts["world_static"] + counts["hud_only"]
    eligible = review_complete and classified and aligned and duration > 0 and world_transitions > 0
    raw_complete = len(frames) > 1 and raw_compared == len(frames) - 1 and aligned
    return {
        "status": "reviewer_asserted" if eligible else "inconclusive",
        "reason": ("Individual labels and alignment are reviewer assertions, not an automated "
                   "proof of world FPS." if eligible else
                   "Complete real-image labels and validated region, flip alignment, and "
                   "presented-image source are required."),
        "review": review,
        "region": list(region), "exclude": [list(rect) for rect in exclusions],
        "image_dimensions": list(size) if size else None,
        "tolerance": tolerance, "frames": len(frames), "counts": counts,
        "observed_solid_black_frames": solid_black,
        "known_black_frames": counts["known_black"],
        "intentional_fade_frames": counts["intentional_fade"],
        "invalid_frames": counts["invalid"],
        "unknown_frames": counts["unknown"] + counts["dark_unknown"],
        "reviewed_world_frames": sum(counts[category] for category in WORLD_CATEGORIES),
        "reviewed_world_changes": counts["world_changed"],
        "reviewed_world_changes_per_second": (
            round(counts["world_changed"] / duration, 3) if eligible else None),
        "raw_region_pixel_changes": {
            "changed_transitions": raw_changed, "compared_transitions": raw_compared,
            "changes_per_second": round(raw_changed / duration, 3) if raw_complete and duration > 0 else None,
            "diagnostic_only": True, "not_a_world_frame_measure": True,
        },
        "consecutive_flip_ids": aligned,
        "frame_details": details,
    }
