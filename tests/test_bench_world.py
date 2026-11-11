import json
import sys
from pathlib import Path

import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bench_world  # noqa: E402


def write_manifest(tmp_path, images, labels, *, review=None, flips=None, exclude=None, times=None):
    frames = []
    for index, (image, label) in enumerate(zip(images, labels)):
        item = {
            "flip": flips[index] if flips else index + 1,
            "t": times[index] if times else index / 10,
        }
        if image is not None:
            path = tmp_path / f"frame{index}.bmp"
            if isinstance(image, bytes):
                path.write_bytes(image)
            else:
                image.save(path)
            item["image"] = path.name
        if label is not None:
            item["label"] = label
        frames.append(item)
    manifest = {
        "region": [0, 0, 8, 8], "exclude": exclude or [], "frames": frames,
        "review": review or {"kind": "synthetic"},
    }
    path = tmp_path / "labels.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return path


def sample_world():
    image = Image.new("RGB", (8, 8), (80, 90, 100))
    image.putpixel((0, 0), (255, 0, 0))
    return image


def review_metadata():
    return {
        "kind": "real", "reviewer": "test reviewer",
        "method": "test labels independently assigned to each frame",
        "region_validation": "synthetic HUD is at the excluded pixel",
        "alignment_validation": "synthetic flip IDs and times are generated together",
        "presentation_validation": "synthetic source images are the test frames",
        "complete": True,
    }


def test_labeled_synthetic_frames_separate_world_hud_black_fade_and_unknown(tmp_path):
    base = sample_world()
    hud = base.copy()
    hud.putpixel((0, 0), (0, 255, 0))
    motion = hud.copy()
    motion.putpixel((4, 4), (160, 90, 100))
    images = [
        base, base.copy(), hud, motion, Image.new("RGB", (8, 8), 0),
        Image.new("RGB", (8, 8), (8, 8, 8)),
        Image.new("RGB", (8, 8), (5, 5, 5)), b"not a BMP", None, motion,
    ]
    labels = [
        "world", "world_static", "hud_only", "world_changed", "known_black",
        "intentional_fade", "dark_unknown", "unknown", "unknown", None,
    ]
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, images, labels, exclude=[[0, 0, 2, 2]]))
    assert report["counts"] == {
        "world_baseline": 1, "world_changed": 1, "world_static": 1, "hud_only": 1,
        "known_black": 1, "intentional_fade": 1, "intentional_solid": 0, "invalid": 1,
        "dark_unknown": 1, "solid_unknown": 0, "unchanged_unknown": 0, "unknown": 2,
    }
    assert report["reviewed_world_frames"] == 4
    assert report["reviewed_world_changes"] == 1
    assert report["unknown_frames"] == 4 and report["invalid_frames"] == 1
    assert report["frame_details"][2]["region_pixels_over_tolerance"] == 0
    assert report["frame_details"][3]["region_pixels_over_tolerance"] == 1
    assert report["status"] == "inconclusive"
    assert report["reviewed_world_changes_per_second"] is None
    assert report["reviewed_world_low_1pct_fps"] is None
    assert report["reviewed_world_cadence"] is None


def test_unlabeled_roi_changes_are_diagnostic_not_valid_world_frames(tmp_path):
    base = sample_world()
    changed = base.copy()
    changed.putpixel((4, 4), (1, 2, 3))
    report = bench_world.analyze_manifest(write_manifest(tmp_path, [base, changed], [None, None]))
    assert report["unknown_frames"] == 2
    assert report["reviewed_world_frames"] == report["reviewed_world_changes"] == 0
    assert report["reviewed_world_changes_per_second"] is None
    assert report["raw_region_pixel_changes"] == {
        "changed_transitions": 1, "compared_transitions": 1, "changes_per_second": 10.0,
        "diagnostic_only": True, "not_a_world_frame_measure": True,
    }
    assert "distinct_world_fps" not in report


def test_darkness_does_not_infer_fade_but_exact_black_is_identified(tmp_path):
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path,
        [Image.new("RGB", (8, 8), (1, 1, 1)), Image.new("RGB", (8, 8), 0),
         Image.new("RGB", (8, 8), 0)],
        [None, None, "intentional_fade"],
    ))
    assert report["counts"]["dark_unknown"] == 1
    assert report["known_black_frames"] == 1
    assert report["intentional_fade_frames"] == 1
    assert report["observed_solid_black_frames"] == 2


def test_fully_reviewed_black_and_fade_only_cannot_yield_world_rate(tmp_path):
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, [Image.new("RGB", (8, 8), 0), Image.new("RGB", (8, 8), (2, 2, 2))],
        ["known_black", "intentional_fade"], review=review_metadata()))
    assert report["unknown_frames"] == 1 and report["invalid_frames"] == 0
    assert report["reviewed_world_frames"] == 0
    assert report["reviewed_world_changes_per_second"] is None
    assert report["status"] == "inconclusive"


def test_hud_animation_mislabeled_as_world_motion_is_invalid(tmp_path):
    base = sample_world()
    hud = base.copy()
    hud.putpixel((0, 0), (0, 255, 0))
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, [base, hud], ["world", "world_changed"], review=review_metadata(),
        exclude=[[0, 0, 2, 2]]))
    assert report["invalid_frames"] == 1
    assert report["reviewed_world_changes"] == 0
    assert report["reviewed_world_changes_per_second"] is None
    assert report["status"] == "inconclusive"


def test_rate_requires_complete_real_review_and_consecutive_alignment(tmp_path):
    base = sample_world()
    hud = base.copy()
    hud.putpixel((0, 0), (0, 255, 0))
    motion = hud.copy()
    motion.putpixel((4, 4), (1, 2, 3))
    images = [base, base.copy(), hud, motion]
    labels = ["world", "world_static", "hud_only", "world_changed"]
    path = write_manifest(tmp_path, images, labels, review=review_metadata(),
                          exclude=[[0, 0, 2, 2]])
    report = bench_world.analyze_manifest(path)
    assert report["status"] == "reviewer_asserted"
    assert report["reviewed_world_changes_per_second"] == 3.333
    assert report["reviewed_world_low_1pct_fps"] == 3.333
    assert report["reviewed_world_cadence"]["frames"] == 1
    assert "distinct_world_fps" not in report
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["frames"][2]["flip"] = 4
    manifest["frames"][3]["flip"] = 5
    path.write_text(json.dumps(manifest), encoding="utf-8")
    report = bench_world.analyze_manifest(path)
    assert not report["consecutive_flip_ids"]
    assert report["unknown_frames"] >= 1
    assert report["reviewed_world_changes_per_second"] is None
    assert report["raw_region_pixel_changes"]["changes_per_second"] is None


def test_reviewed_world_cadence_reports_interval_percentiles_and_low(tmp_path):
    images = []
    for index in range(5):
        image = sample_world()
        image.putpixel((4, 4), (80 + index, 90, 100))
        images.append(image)
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, images,
        ["world", "world_changed", "world_changed", "world_changed", "world_changed"],
        review=review_metadata(), times=[0, 0.01, 0.02, 0.03, 1.03],
    ))
    assert report["status"] == "reviewer_asserted"
    assert report["reviewed_world_low_1pct_fps"] == 1.0
    assert report["reviewed_world_cadence"] == {
        "scope": ("reviewer-labeled baseline-to-change and change-to-change intervals; "
                  "trailing incomplete intervals are excluded"),
        "frames": 4,
        "segments": 1,
        "seconds": 1.03,
        "frame_ms_p50": 10.0,
        "frame_ms_p90": 1000.0,
        "frame_ms_p99": 1000.0,
        "low_1pct_fps": 1.0,
    }


def test_unknown_previous_frame_never_becomes_a_valid_motion_pair(tmp_path):
    base = sample_world()
    motion = base.copy()
    motion.putpixel((4, 4), (1, 2, 3))
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, [base, motion], ["unknown", "world_changed"], review=review_metadata()))
    assert report["unknown_frames"] == 2
    assert report["reviewed_world_frames"] == 0
    assert report["reviewed_world_changes"] == 0


def test_invalid_region_and_black_world_label_are_rejected(tmp_path):
    path = write_manifest(tmp_path, [sample_world()], ["world"], exclude=[[0, 0, 8, 8]])
    with pytest.raises(bench_world.WorldLabelError, match="entire selected region"):
        bench_world.analyze_manifest(path)
    path = write_manifest(tmp_path, [Image.new("RGB", (8, 8), 0)], ["world"])
    report = bench_world.analyze_manifest(path)
    assert report["invalid_frames"] == 1
    assert report["reviewed_world_frames"] == 0


def test_invalid_label_format_is_not_treated_as_a_world_frame(tmp_path):
    path = write_manifest(tmp_path, [sample_world()], [["world"]])
    with pytest.raises(bench_world.WorldLabelError, match="unrecognized frame label"):
        bench_world.analyze_manifest(path)


def test_hash_only_benchmark_marks_nonblack_frames_unknown():
    rows = [
        {"hash": "f22163d4abcd6aa4", "distinct": 1},
        {"hash": "1234567890abcdef", "distinct": 1},
        {"hash": "f22163d4abcd6aa4", "distinct": -1},
    ]
    report = bench_world.unreviewed_metrics(rows)
    assert report["known_black_frames"] == 1
    assert report["unknown_frames"] == 3
    assert report["invalid_frames"] is None and report["intentional_fade_frames"] is None
    assert report["reviewed_world_frames"] == report["reviewed_world_changes"] == 0
    assert report["reviewed_world_changes_per_second"] is None
    assert report["reviewed_world_low_1pct_fps"] is None
    assert report["reviewed_world_cadence"] is None


def test_black_requires_an_explicit_intentional_label_even_with_world_motion(tmp_path):
    base = sample_world()
    changed = base.copy()
    changed.putpixel((4, 4), (160, 90, 100))
    black = Image.new("RGB", (8, 8), 0)
    images = [base, changed, black, base, changed]
    labels = ["world", "world_changed", "known_black", "world", "world_changed"]
    path = write_manifest(tmp_path, images, labels, review=review_metadata())
    report = bench_world.analyze_manifest(path)
    assert report["status"] == "inconclusive"
    assert report["known_black_frames"] == report["unknown_frames"] == 1
    assert report["reviewed_world_changes_per_second"] is None

    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["frames"][2]["label"] = "intentional_solid"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    report = bench_world.analyze_manifest(path)
    assert report["status"] == "reviewer_asserted"
    assert report["intentional_solid_frames"] == 1
    assert report["known_black_frames"] == report["unknown_frames"] == 0
    assert report["reviewed_world_cadence"]["segments"] == 2


def test_unclassified_solid_dark_and_unchanged_intervals_are_separate(tmp_path):
    world = sample_world()
    dark = Image.new("RGB", (8, 8), (1, 2, 3))
    dark.putpixel((1, 1), (2, 3, 4))
    solid = Image.new("RGB", (8, 8), (40, 60, 80))
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, [world, world.copy(), dark, solid, solid.copy()], [None] * 5))
    assert [frame["classification"] for frame in report["frame_details"]] == [
        "unknown", "unchanged_unknown", "dark_unknown", "solid_unknown", "solid_unknown",
    ]
    intervals = report["unclassified_intervals"]
    assert intervals["total"] == 4 and intervals["omitted"] == 0
    assert intervals["intervals"][-1] == {
        "classification": "solid_unknown", "start_flip": 4, "end_flip": 5,
        "start_t": 0.3, "end_t": 0.4, "frames": 2, "observed_seconds": 0.1,
    }


def test_intentional_solid_must_be_uniform_and_static_world_is_inconclusive(tmp_path):
    base = sample_world()
    path = write_manifest(tmp_path, [base, base.copy()], ["world", "world_static"],
                          review=review_metadata())
    report = bench_world.analyze_manifest(path)
    assert report["unknown_frames"] == report["invalid_frames"] == 0
    assert report["status"] == "inconclusive"
    assert report["reviewed_world_changes_per_second"] is None

    path = write_manifest(tmp_path, [base], ["intentional_solid"], review=review_metadata())
    assert bench_world.analyze_manifest(path)["invalid_frames"] == 1


def test_hash_intervals_are_bounded_and_do_not_treat_black_as_intentional():
    rows = []
    for index in range(150):
        rows.append({
            "flip": index + 1, "t": index / 10,
            "distinct": 1, "hash": "f22163d4abcd6aa4" if index % 2 == 0 else "1",
        })
    report = bench_world.unreviewed_metrics(rows)
    assert report["known_black_frames"] == 75
    assert report["unknown_frames"] == 150
    assert report["unclassified_intervals"]["total"] == 150
    assert report["unclassified_intervals"]["omitted"] == 86
