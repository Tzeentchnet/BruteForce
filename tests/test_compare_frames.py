import io
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import compare_frames  # noqa: E402


@pytest.fixture
def synthetic_frames(tmp_path):
    reference = Image.new("RGB", (4, 3), (20, 20, 20))
    reference.putpixel((3, 2), (0, 20, 20))
    candidate = reference.copy()
    for position, color in {
        (1, 0): (18, 20, 20),
        (0, 1): (28, 20, 20),
        (2, 0): (29, 11, 20),
        (3, 1): (20, 20, 52),
        (2, 2): (53, 19, 20),
        (3, 2): (255, 20, 20),
    }.items():
        candidate.putpixel(position, color)
    reference_path = tmp_path / "reference.bmp"
    candidate_path = tmp_path / "candidate.png"
    reference.save(reference_path)
    candidate.save(candidate_path)
    return reference_path, candidate_path


@pytest.fixture
def legacy_expected():
    return {
        "size": [4, 3],
        "tolerance": 8,
        "pixels_over_tolerance": 4,
        "fraction_over_tolerance": round(4 / 12, 6),
        "histogram": {"0": 6, "1-2": 1, "3-8": 1, "9-32": 2, "33-255": 2},
        "cells_over_tolerance": 2,
        "largest_regions": [
            {"x": 2, "y": 0, "pixels": 2},
            {"x": 2, "y": 2, "pixels": 2},
        ],
    }


def run_cli(*arguments):
    return subprocess.run(
        [sys.executable, "-B", str(ROOT / "tools" / "compare_frames.py"),
         *(str(argument) for argument in arguments)],
        cwd=ROOT, capture_output=True, text=True, check=False, timeout=30,
    )


def test_whole_image_and_all_changed_regions(synthetic_frames):
    reference, candidate = synthetic_frames
    result = compare_frames.compare(reference, candidate, tolerance=32, cell=2)

    assert result["size"] == [4, 3]
    assert result["cell_size"] == 2
    assert result["pixels_differing"] == 6
    assert result["fraction_differing"] == 0.5
    assert result["pixels_over_8"] == 4
    assert result["fraction_over_8"] == round(4 / 12, 6)
    assert result["pixels_over_tolerance"] == 2
    assert result["fraction_over_tolerance"] == round(2 / 12, 6)
    assert result["histogram"] == {
        "0": 6, "1-2": 1, "3-8": 1, "9-32": 2, "33-255": 2,
    }
    assert result["absolute_difference"] == {
        "sum": 349, "mean_per_channel": round(349 / 36, 6), "max_channel": 255,
    }
    assert result["largest_regions"] == [{"x": 2, "y": 2, "pixels": 2}]
    assert result["regional_differences"] == [
        {
            "x": 0, "y": 0, "width": 2, "height": 2,
            "pixels_differing": 2, "pixels_over_8": 0, "pixels_over_tolerance": 0,
            "absolute_difference": {
                "sum": 10, "mean_per_channel": round(10 / 12, 6), "max_channel": 8,
            },
        },
        {
            "x": 2, "y": 0, "width": 2, "height": 2,
            "pixels_differing": 2, "pixels_over_8": 2, "pixels_over_tolerance": 0,
            "absolute_difference": {
                "sum": 50, "mean_per_channel": round(50 / 12, 6), "max_channel": 32,
            },
        },
        {
            "x": 2, "y": 2, "width": 2, "height": 1,
            "pixels_differing": 2, "pixels_over_8": 2, "pixels_over_tolerance": 2,
            "absolute_difference": {
                "sum": 289, "mean_per_channel": round(289 / 6, 6), "max_channel": 255,
            },
        },
    ]
    assert sum(region["absolute_difference"]["sum"]
               for region in result["regional_differences"]) == 349
    assert sum(region["pixels_over_8"] for region in result["regional_differences"]) == 4


@pytest.mark.parametrize(("tolerance", "expected"), [
    (0, 6), (2, 5), (8, 4), (8.5, 4), (9, 3), (32, 2), (33, 1), (255, 0),
])
def test_threshold_boundaries_do_not_change_fixed_over_8_count(
        synthetic_frames, tolerance, expected):
    result = compare_frames.compare(*synthetic_frames, tolerance=tolerance, cell=2)
    assert result["pixels_over_tolerance"] == expected
    assert result["pixels_over_8"] == 4
    assert sum(region["pixels_over_tolerance"]
               for region in result["regional_differences"]) == expected
    assert result["absolute_difference"]["sum"] == 349


def test_swapped_inputs_are_symmetric_and_labels_are_neutral(synthetic_frames, tmp_path):
    reference, candidate = synthetic_frames
    forward_mask = tmp_path / "forward.png"
    reverse_mask = tmp_path / "reverse.png"
    forward = compare_frames.compare(
        reference, candidate, 8, 2, forward_mask,
        reference_label="xemu", candidate_label="project GPU",
    )
    reverse = compare_frames.compare(
        candidate, reference, 8, 2, reverse_mask,
        reference_label="project GPU", candidate_label="xemu",
    )
    assert (forward["reference_label"], forward["candidate_label"]) == ("xemu", "project GPU")
    assert (reverse["reference_label"], reverse["candidate_label"]) == ("project GPU", "xemu")
    labels = {"reference_label", "candidate_label"}
    assert {key: value for key, value in forward.items() if key not in labels} == {
        key: value for key, value in reverse.items() if key not in labels
    }
    with Image.open(forward_mask) as first, Image.open(reverse_mask) as second:
        assert first.mode == second.mode == "L"
        assert first.tobytes() == second.tobytes()
        assert first.getpixel((0, 1)) == 0
        assert first.getpixel((2, 0)) == 255
        assert first.tobytes().count(255) == 4


def test_identical_png_and_bmp_ignore_alpha(tmp_path):
    bmp = tmp_path / "frame.bmp"
    png = tmp_path / "frame.png"
    Image.new("RGB", (3, 2), (7, 8, 9)).save(bmp)
    Image.new("RGBA", (3, 2), (7, 8, 9, 0)).save(png)
    result = compare_frames.compare(bmp, png, tolerance=0, cell=2)
    assert result["histogram"] == {
        "0": 6, "1-2": 0, "3-8": 0, "9-32": 0, "33-255": 0,
    }
    assert result["pixels_differing"] == result["pixels_over_8"] == 0
    assert result["absolute_difference"] == {
        "sum": 0, "mean_per_channel": 0.0, "max_channel": 0,
    }
    assert result["regional_differences"] == []


def test_regional_details_are_not_limited_to_legacy_top_twelve(tmp_path):
    reference = tmp_path / "reference.bmp"
    candidate = tmp_path / "candidate.png"
    Image.new("RGB", (17, 1), (0, 0, 0)).save(reference)
    Image.new("RGB", (17, 1), (9, 0, 0)).save(candidate)
    result = compare_frames.compare(reference, candidate, cell=1)
    assert result["cells_over_tolerance"] == 17
    assert len(result["largest_regions"]) == 12
    assert len(result["regional_differences"]) == 17
    assert [region["x"] for region in result["regional_differences"]] == list(range(17))
    assert result["pixels_over_8"] == 17
    assert result["absolute_difference"]["sum"] == 17 * 9


def test_whole_image_and_region_sums_exceed_uint32(tmp_path):
    size = (3840, 2160)
    black = tmp_path / "black.png"
    white = tmp_path / "white.png"
    for path, color in ((black, (0, 0, 0)), (white, (255, 255, 255))):
        with Image.new("RGB", size, color) as image:
            image.save(path, compress_level=1)

    result = compare_frames.compare(black, white, cell=max(size))
    expected_pixels = 8_294_400
    expected_difference = {
        "sum": 6_345_216_000, "mean_per_channel": 255.0, "max_channel": 255,
    }
    assert expected_difference["sum"] > 0xFFFFFFFF
    assert result["size"] == list(size)
    assert result["pixels_differing"] == expected_pixels
    assert result["pixels_over_8"] == expected_pixels
    assert result["pixels_over_tolerance"] == expected_pixels
    assert result["histogram"]["33-255"] == expected_pixels
    assert result["absolute_difference"] == expected_difference
    assert result["regional_differences"] == [{
        "x": 0, "y": 0, "width": size[0], "height": size[1],
        "pixels_differing": expected_pixels, "pixels_over_8": expected_pixels,
        "pixels_over_tolerance": expected_pixels,
        "absolute_difference": expected_difference,
    }]


def test_legacy_api_fields_and_positional_arguments(synthetic_frames, tmp_path, legacy_expected):
    reference, candidate = synthetic_frames
    mask = tmp_path / "legacy-diff.png"
    result = compare_frames.compare(reference, candidate, 8, 2, mask)
    assert {key: result[key] for key in legacy_expected} == legacy_expected
    assert result == compare_frames.compare(
        path_a=reference, path_b=candidate, tolerance=8, cell=2, diff_out=None,
    )
    assert mask.is_file()
    default = compare_frames.compare(reference, candidate)
    default_expected = {**legacy_expected, "cells_over_tolerance": 1,
                        "largest_regions": [{"x": 0, "y": 0, "pixels": 4}]}
    assert {key: default[key] for key in default_expected} == default_expected


def test_legacy_region_ties_follow_first_over_tolerance_pixel(tmp_path):
    reference = tmp_path / "reference.bmp"
    candidate = tmp_path / "candidate.png"
    a = Image.new("RGB", (4, 2), (0, 0, 0))
    b = a.copy()
    b.putpixel((0, 0), (1, 0, 0))
    b.putpixel((2, 0), (9, 0, 0))
    b.putpixel((0, 1), (9, 0, 0))
    a.save(reference)
    b.save(candidate)
    result = compare_frames.compare(reference, candidate, cell=2)
    assert result["largest_regions"] == [
        {"x": 2, "y": 0, "pixels": 1},
        {"x": 0, "y": 0, "pixels": 1},
    ]


def test_legacy_file_like_inputs_and_output(synthetic_frames, tmp_path):
    reference, candidate = synthetic_frames
    first = io.BytesIO(reference.read_bytes())
    second = io.BytesIO(candidate.read_bytes())
    mask = tmp_path / "stream-mask.png"
    with mask.open("wb") as stream:
        result = compare_frames.compare(first, second, 8, 2, stream)
    assert result["pixels_over_tolerance"] == 4
    assert not first.closed and not second.closed
    with Image.open(mask) as image:
        assert image.getpixel((2, 0)) == 255


def test_cli_help_describes_neutral_roles():
    process = run_cli("--help")
    assert process.returncode == 0
    assert "without assuming either is correct" in process.stdout
    assert "--reference-label" in process.stdout
    assert "--candidate-label" in process.stdout
    assert "CPU-path capture" not in process.stdout
    assert "GPU-path capture" not in process.stdout


def test_cli_accepts_neutral_labels_in_either_order(synthetic_frames, tmp_path):
    reference, candidate = synthetic_frames
    mask = tmp_path / "cli-mask.png"
    process = run_cli(candidate, reference, "--reference-label", "project GPU",
                      "--candidate-label", "xemu", "--tolerance", "32",
                      "--cell", "2", "--diff", mask)
    assert process.returncode == 0, process.stderr
    assert process.stderr == ""
    result = json.loads(process.stdout)
    assert result["reference_label"] == "project GPU"
    assert result["candidate_label"] == "xemu"
    assert result["pixels_over_8"] == 4
    assert result["pixels_over_tolerance"] == 2
    assert result["absolute_difference"]["sum"] == 349
    with Image.open(mask) as image:
        assert image.getpixel((3, 1)) == 0
        assert image.getpixel((2, 2)) == 255


def test_legacy_cli_options_and_json_fields(synthetic_frames, tmp_path, legacy_expected):
    reference, candidate = synthetic_frames
    mask = tmp_path / "legacy-cli-diff.png"
    process = run_cli(reference, candidate, "--tolerance", 8, "--cell", 2, "--diff", mask)
    assert process.returncode == 0, process.stderr
    assert process.stderr == ""
    data = json.loads(process.stdout)
    assert {key: data[key] for key in legacy_expected} == legacy_expected
    assert data["reference_label"] == "reference"
    assert data["candidate_label"] == "candidate"
    with Image.open(mask) as image:
        assert image.getpixel((0, 1)) == 0
        assert image.getpixel((2, 0)) == 255

    default = run_cli(reference, candidate)
    assert default.returncode == 0, default.stderr
    default_data = json.loads(default.stdout)
    default_expected = {**legacy_expected, "cells_over_tolerance": 1,
                        "largest_regions": [{"x": 0, "y": 0, "pixels": 4}]}
    assert {key: default_data[key] for key in default_expected} == default_expected


def test_unequal_dimensions_fail_without_resizing_or_writing_mask(synthetic_frames, tmp_path):
    reference, _ = synthetic_frames
    mismatch = tmp_path / "mismatch.png"
    mask = tmp_path / "should-not-exist.png"
    Image.new("RGB", (3, 4), (20, 20, 20)).save(mismatch)
    with pytest.raises(ValueError, match=r"sizes differ: \(4, 3\) vs \(3, 4\)"):
        compare_frames.compare(reference, mismatch, diff_out=mask)
    assert not mask.exists()
    process = run_cli(reference, mismatch, "--diff", mask)
    assert process.returncode == 1
    assert "sizes differ: (4, 3) vs (3, 4)" in process.stderr
    assert process.stdout == ""
    assert not mask.exists()


@pytest.mark.parametrize("tolerance", [-1, 256, float("nan"), float("inf"),
                                        -float("inf"), "8", None, True])
def test_invalid_tolerance_is_explicit(synthetic_frames, tolerance):
    with pytest.raises(ValueError, match="tolerance must be a finite number"):
        compare_frames.compare(*synthetic_frames, tolerance=tolerance)


@pytest.mark.parametrize("cell", [0, -1, 1.5, "2", None, True])
def test_invalid_cell_is_explicit(synthetic_frames, cell):
    with pytest.raises(ValueError, match="cell must be a positive integer"):
        compare_frames.compare(*synthetic_frames, cell=cell)


@pytest.mark.parametrize("labels", [
    {"reference_label": ""}, {"reference_label": "   "},
    {"candidate_label": ""}, {"candidate_label": None},
])
def test_invalid_labels_are_explicit(synthetic_frames, labels):
    with pytest.raises(ValueError, match="label must be a non-empty string"):
        compare_frames.compare(*synthetic_frames, **labels)


@pytest.mark.parametrize(("options", "error"), [
    (["--tolerance", "-1"], "tolerance must be"),
    (["--tolerance", "256"], "tolerance must be"),
    (["--tolerance", "nan"], "invalid int value"),
    (["--cell", "0"], "cell must be"),
    (["--cell", "-2"], "cell must be"),
    (["--reference-label", ""], "reference label must be"),
    (["--candidate-label", " "], "candidate label must be"),
    (["--diff", ""], "unknown file extension"),
])
def test_cli_invalid_settings_fail_on_stderr(synthetic_frames, options, error):
    process = run_cli(*synthetic_frames, *options)
    assert process.returncode != 0
    assert error in process.stderr
    assert process.stdout == ""


def test_missing_corrupt_and_invalid_image_inputs(synthetic_frames, tmp_path):
    reference, candidate = synthetic_frames
    missing = tmp_path / "missing.bmp"
    corrupt = tmp_path / "corrupt.png"
    corrupt.write_bytes(b"not an image")
    for invalid in (missing, corrupt):
        with pytest.raises(OSError):
            compare_frames.compare(invalid, candidate)
        process = run_cli(invalid, candidate)
        assert process.returncode == 1
        assert "Error:" in process.stderr
        assert process.stdout == ""
    with pytest.raises(ValueError, match="reference image must be"):
        compare_frames.compare(None, candidate)
    with pytest.raises(ValueError, match="candidate image must be"):
        compare_frames.compare(reference, object())


def test_bad_output_path_is_explicit(synthetic_frames, tmp_path):
    bad_output = tmp_path / "missing-directory" / "mask.png"
    with pytest.raises(OSError):
        compare_frames.compare(*synthetic_frames, diff_out=bad_output)
    process = run_cli(*synthetic_frames, "--diff", bad_output)
    assert process.returncode == 1
    assert "Error:" in process.stderr
    assert process.stdout == ""
    with pytest.raises(ValueError, match="diff output must be"):
        compare_frames.compare(*synthetic_frames, diff_out=42)


@pytest.mark.parametrize("input_index", [0, 1])
def test_diff_output_cannot_overwrite_an_input(synthetic_frames, input_index):
    output = synthetic_frames[input_index]
    original = output.read_bytes()
    with pytest.raises(ValueError, match="must not overwrite"):
        compare_frames.compare(*synthetic_frames, diff_out=output)
    process = run_cli(*synthetic_frames, "--diff", output)
    assert process.returncode == 1
    assert "must not overwrite" in process.stderr
    assert output.read_bytes() == original


@pytest.mark.parametrize("input_index", [0, 1])
def test_diff_output_cannot_overwrite_a_hard_link_to_an_input(
        synthetic_frames, tmp_path, input_index):
    source = synthetic_frames[input_index]
    alias = tmp_path / f"hard-link-{input_index}{source.suffix}"
    try:
        os.link(source, alias)
    except (OSError, NotImplementedError) as exc:
        pytest.skip(f"hard links are unavailable: {exc}")
    original = source.read_bytes()
    assert os.path.samefile(source, alias)
    with pytest.raises(ValueError, match="must not overwrite"):
        compare_frames.compare(*synthetic_frames, diff_out=alias)
    process = run_cli(*synthetic_frames, "--diff", alias)
    assert process.returncode == 1
    assert "must not overwrite" in process.stderr
    assert source.read_bytes() == alias.read_bytes() == original


def test_image_safety_limit_is_reported_without_traceback(synthetic_frames, monkeypatch, capsys):
    monkeypatch.setattr(Image, "MAX_IMAGE_PIXELS", 1)
    assert compare_frames.main([str(path) for path in synthetic_frames]) == 1
    captured = capsys.readouterr()
    assert "Error:" in captured.err
    assert "DecompressionBombError" not in captured.err
    assert captured.out == ""
