import csv
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
FRAME_STATS_EXE = os.environ.get("BRUTEFORCE_TEST_FRAME_STATS")
pytestmark = pytest.mark.skipif(
    not FRAME_STATS_EXE, reason="set BRUTEFORCE_TEST_FRAME_STATS to the native frame-stats fixture"
)
XBE = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"


def run_fixture(mode, output):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    return subprocess.run(
        [FRAME_STATS_EXE, str(XBE), mode, str(output)],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )


def read_rows(path):
    with path.open(newline="", encoding="ascii") as stream:
        return list(csv.DictReader(stream))


def test_hash_and_distinct_counting(tmp_path):
    output = tmp_path / "unit.csv"
    result = run_fixture("unit", output)
    assert result.returncode == 0, result.stderr
    assert "frame statistics unit tests passed" in result.stdout
    rows = read_rows(output)
    assert [row["distinct"] for row in rows] == ["1", "0", "1", "-1", "1"]
    assert [row["t_us"] for row in rows] == ["1000", "2000", "3000", "4000", "5000"]
    assert rows[3]["hash"] == "0" * 16


def test_executor_hashes_presented_surface(tmp_path):
    output = tmp_path / "executor.csv"
    result = run_fixture("executor", output)
    assert result.returncode == 0, result.stderr
    rows = read_rows(output)
    assert len(rows) == 4
    assert [row["distinct"] for row in rows] == ["1", "0", "1", "-1"]
    assert rows[0]["hash"] == rows[1]["hash"] != rows[2]["hash"]
    assert rows[3]["hash"] == "0" * 16, "a registered back end owns the pixels"
    assert rows[0]["constants"] == rows[1]["constants"] != rows[2]["constants"]
    assert rows[3]["constants"] == "0" * 16, "no constants uploaded since the last flip"
    times = [int(row["t_us"]) for row in rows]
    assert times == sorted(times)


def read_census(path):
    lines = path.read_text(encoding="ascii").splitlines()
    assert lines[0].startswith("# GPU feature census:")
    rows = list(csv.DictReader(lines[1:]))
    return lines[0], {(row["category"], row["key"]): int(row["count"]) for row in rows}


def test_census_counts_only_while_gated(tmp_path):
    output = tmp_path / "census.csv"
    result = run_fixture("census", output)
    assert result.returncode == 0, result.stderr
    header, counts = read_census(output)
    assert "2 flips counted" in header, "flips while the gate file existed"
    assert counts[("path", "00000053")] == 1, "one screen-space draw inside the gate"
    assert counts[("primitive", "00000005")] == 1
    assert counts[("render_to_texture", "00100010")] == 1, "16x16 surface sampled as a texture"
    assert counts[("stage_mode", "00000001")] == 1
    assert counts[("clear_flags", "000000F0")] == 1
    assert counts[("unhandled_method", "00001D7C")] == 1
    assert not (tmp_path / "census.csv.tmp").exists()


@pytest.mark.parametrize(("mode", "flip_only"), [
    ("dump-default", False),
    ("dump-flip-only", True),
])
def test_flip_capture_can_exclude_report_and_after_draw_dumps(mode, flip_only, tmp_path):
    result = run_fixture(mode, tmp_path / "hold")
    assert result.returncode == 0, result.stderr
    assert "flip capture tests passed" in result.stdout
    images = sorted(tmp_path.glob("hold*.bmp"))
    if flip_only:
        assert [path.name for path in images] == ["hold00000.bmp"]
        assert images[0].read_bytes()[54:57] == bytes((0x78, 0x56, 0x34))
    else:
        assert len(images) > 1, "report-time dumps remain enabled by default"


def test_tagged_capture_uses_the_recorded_flip_id(tmp_path):
    result = run_fixture("dump-flip-id", tmp_path / "hold")
    assert result.returncode == 0, result.stderr
    images = sorted(tmp_path.glob("hold*.bmp"))
    assert [path.name for path in images] == ["hold0000000002.bmp"]
    assert images[0].read_bytes()[54:57] == bytes((0x78, 0x56, 0x34))
    assert [row["flip"] for row in read_rows(tmp_path / "hold.csv")] == ["1", "2", "3"]


def test_tagged_capture_preserves_ids_across_gate_reopens(tmp_path):
    result = run_fixture("dump-flip-id-reopen", tmp_path / "hold")
    assert result.returncode == 0, result.stderr
    images = sorted(tmp_path.glob("hold*.bmp"))
    assert [path.name for path in images] == ["hold0000000002.bmp", "hold0000000004.bmp"]
    assert [row["flip"] for row in read_rows(tmp_path / "hold.csv")] == ["1", "2", "3", "4"]
    assert [path.read_bytes()[54:57] for path in images] == [
        bytes((0x78, 0x56, 0x34)), bytes((0x33, 0x22, 0x11)),
    ]


@pytest.mark.parametrize(("mode", "message"), [
    ("dump-flip-id-no-stats", "frame statistics or guest pixels unavailable"),
    ("dump-flip-id-no-readback", "frame statistics or guest pixels unavailable"),
    ("dump-flip-id-invalid-span", "invalid framebuffer span"),
    ("dump-flip-id-invalid", "RECOMP_FB_DUMP_FLIP_ID must be 0 or 1"),
])
def test_tagged_capture_rejects_unreliable_inputs(mode, message, tmp_path):
    result = run_fixture(mode, tmp_path / "hold")
    assert result.returncode != 0
    assert message in result.stderr
    assert not list(tmp_path.glob("hold*.bmp"))


def test_tagged_capture_does_not_overwrite_existing_file(tmp_path):
    image = tmp_path / "hold0000000002.bmp"
    image.write_bytes(b"existing image")
    result = run_fixture("dump-flip-id", tmp_path / "hold")
    assert result.returncode != 0
    assert "cannot create framebuffer dump for flip 2" in result.stderr
    assert image.read_bytes() == b"existing image"


@pytest.mark.parametrize(("mode", "backend_owns_depth", "callbacks"), [
    ("depth-owner-cpu", False, 0),
    ("depth-owner-draw-only", False, 0),
    ("depth-owner-clear-only", False, 2),
    ("depth-owner-backend", True, 2),
])
def test_software_depth_clears_only_when_needed(mode, backend_owns_depth, callbacks, tmp_path):
    output = tmp_path / "depth-owner.csv"
    result = run_fixture(mode, output)
    assert result.returncode == 0, result.stderr
    assert f"depth clear callbacks {callbacks}" in result.stdout
    _, counts = read_census(output)
    assert counts[("depth_clear_backend", f"{int(backend_owns_depth):08X}")] == 2
    assert counts[("clear_flags", "00000001")] == 1
    assert counts[("clear_flags", "00000003")] == 1


@pytest.mark.parametrize("mode", ["probe-on", "probe-off"])
def test_probe_drops_only_transformed_batches(mode, tmp_path):
    result = run_fixture(mode, tmp_path / "unused")
    assert result.returncode == 0, result.stderr
    assert "probe tests passed" in result.stdout
    assert ("PROBE: dropping every transformed batch" in result.stderr) == (mode == "probe-on")


@pytest.mark.parametrize("mode", ["extended", "extended-experiments"])
def test_extended_backend_receives_batches_and_reads_back(mode, tmp_path):
    output = tmp_path / "extended.csv"
    result = run_fixture(mode, output)
    assert result.returncode == 0, result.stderr
    assert "extended back-end tests passed" in result.stdout
    rows = read_rows(output)
    assert len(rows) == 1
    assert rows[0]["hash"] != "0" * 16, "the frame read back into guest memory is hashed"
    assert rows[0]["distinct"] == "1"
