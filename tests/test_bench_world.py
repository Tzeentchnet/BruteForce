import json
import sys
from pathlib import Path

import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bench_world  # noqa: E402


def write_manifest(tmp_path, images, labels, *, review=None, flips=None, exclude=None):
    frames = []
    for index, (image, label) in enumerate(zip(images, labels)):
        item = {"flip": flips[index] if flips else index + 1, "t": index / 10}
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
        "known_black": 1, "intentional_fade": 1, "invalid": 1,
        "dark_unknown": 1, "unknown": 2,
    }
    assert report["reviewed_world_frames"] == 4
    assert report["reviewed_world_changes"] == 1
    assert report["unknown_frames"] == 3 and report["invalid_frames"] == 1
    assert report["frame_details"][2]["region_pixels_over_tolerance"] == 0
    assert report["frame_details"][3]["region_pixels_over_tolerance"] == 1
    assert report["status"] == "inconclusive"
    assert report["reviewed_world_changes_per_second"] is None


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
    assert report["counts"]["unknown"] == 1
    assert report["known_black_frames"] == 1
    assert report["intentional_fade_frames"] == 1
    assert report["observed_solid_black_frames"] == 2


def test_fully_reviewed_black_and_fade_only_cannot_yield_world_rate(tmp_path):
    report = bench_world.analyze_manifest(write_manifest(
        tmp_path, [Image.new("RGB", (8, 8), 0), Image.new("RGB", (8, 8), (2, 2, 2))],
        ["known_black", "intentional_fade"], review=review_metadata()))
    assert report["unknown_frames"] == report["invalid_frames"] == 0
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
    assert report["unknown_frames"] == 2
    assert report["invalid_frames"] is None and report["intentional_fade_frames"] is None
    assert report["reviewed_world_frames"] == report["reviewed_world_changes"] == 0
    assert report["reviewed_world_changes_per_second"] is None
