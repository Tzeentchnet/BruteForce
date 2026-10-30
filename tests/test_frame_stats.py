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
    env = os.environ.copy()
    env.pop("RECOMP_FRAME_STATS", None)
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
