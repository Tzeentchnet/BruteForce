import csv
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
AUDIT_EXE = os.environ.get("BRUTEFORCE_TEST_GPU_AUDIT")
D3D11_EXE = os.environ.get("BRUTEFORCE_TEST_D3D11")
HARDWARE = os.environ.get("BRUTEFORCE_TEST_D3D11_HARDWARE") == "1"
XBE = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"
pytestmark = pytest.mark.skipif(
    not AUDIT_EXE, reason="set BRUTEFORCE_TEST_GPU_AUDIT to the native audit fixture"
)


def clean_env():
    return {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}


def run_fixture(tmp_path, mode, switch, suffix):
    env = clean_env()
    report = tmp_path / f"{suffix}.csv"
    snapshot = tmp_path / f"{suffix}.snapshot"
    env["RECOMP_GPU_AUDIT_FILE"] = str(report)
    if switch is not None:
        env["RECOMP_GPU_AUDIT"] = switch
    result = subprocess.run(
        [AUDIT_EXE, str(XBE), mode, str(snapshot)],
        capture_output=True, text=True, check=False, timeout=60, cwd=tmp_path, env=env,
    )
    assert result.returncode == 0, result.stderr
    assert "[GPU-AUDIT]" not in result.stderr
    return result, report, snapshot


def read_report(path):
    lines = path.read_text(encoding="ascii").splitlines()
    assert lines[0].startswith("# GPU audit:")
    assert "submissions are not visibility" in lines[0]
    return {
        (row["category"], row["key"]): (int(row["count"]), int(row["vertices"]))
        for row in csv.DictReader(lines[1:])
    }


def count(rows, category, key):
    return rows.get((category, key), (0, 0))[0]


@pytest.mark.parametrize("mode", ["cpu", "backend"])
@pytest.mark.parametrize("disabled", [None, "0", "not-enabled"])
def test_off_and_on_preserve_guest_bytes_and_callback_order(tmp_path, mode, disabled):
    off, off_report, off_snapshot = run_fixture(tmp_path, mode, disabled, "off")
    on, on_report, on_snapshot = run_fixture(tmp_path, mode, "1", "on")
    assert not off_report.exists()
    assert off.stdout == on.stdout
    assert off.stderr == on.stderr
    assert off_snapshot.read_bytes() == on_snapshot.read_bytes()
    assert on_report.exists()
    expected_events = b"events=CBEREAF" if mode == "backend" else b"events=\n"
    assert expected_events in on_snapshot.read_bytes()


@pytest.mark.parametrize("mode", ["cpu", "backend"])
def test_requests_routes_methods_and_visibility_are_separate(tmp_path, mode):
    _, report, _ = run_fixture(tmp_path, mode, "1", mode)
    rows = read_report(report)
    assert count(rows, "summary", "REQUESTS") == (15 if mode == "backend" else 14)
    assert count(rows, "source", "NONE") == 2
    assert count(rows, "source", "INLINE") == 2
    assert count(rows, "source", "IMMEDIATE") == 4
    assert count(rows, "primitive", "00000001") == 1
    assert count(rows, "primitive", "00000002") == 1
    assert count(rows, "reason", "SHORT_BATCH") >= 3
    assert count(rows, "reason", "INLINE_LAYOUT") == 1
    assert count(rows, "reason", "SUPERSEDED") == 1
    assert count(rows, "reason", "UNSUPPORTED_PRIMITIVE") == 2
    assert count(rows, "reason", "UNTRANSFORMED") == 1
    assert count(rows, "route", "SCREEN_SPACE") == 1
    assert count(rows, "route", "FIXED_FUNCTION") == 2
    assert count(rows, "route", "CPU_PROGRAM") == 1
    assert count(rows, "cull", "SOFTWARE_TRIANGLES") == 1
    assert count(rows, "cull", "CULL_WITH_NO_TRIANGLES_EMITTED") == 1
    assert count(rows, "output", "NO_TRIANGLES_EMITTED") == 11
    assert count(rows, "clear_flags", "000000F0") == 1
    assert count(rows, "clear_rect_set", "00000003") == 1
    assert count(rows, "flip_size", "16:16") == 1

    assert count(rows, "method_seen", "0:1D70") == 2
    assert count(rows, "method_unhandled", "0:1D70") == 1
    assert count(rows, "method_seen", "4:1D70") == 1
    assert count(rows, "method_unhandled", "4:1D70") == 1
    assert count(rows, "semaphore", "DIRECT_RELEASE") == 1
    assert count(rows, "semaphore", "NO_TARGET") == 1
    assert count(rows, "method_seen", "5:0310") == 1
    assert count(rows, "method_unhandled", "5:0310") == 0
    assert count(rows, "method_unhandled", "6:0310") == 1
    assert count(rows, "method_unhandled", "0:1D7C") == 1
    assert count(rows, "method_unhandled", "5:1D7C") == 1
    assert count(rows, "method_mirror_only", "0:1B0C") == 1

    if mode == "backend":
        assert count(rows, "route", "GPU_VERTEX") == 1
        assert count(rows, "backend", "BASE_SUBMITTED") == 1
        assert count(rows, "backend", "EXTENDED_SUBMITTED") == 2
        assert count(rows, "backend", "GPU_VERTEX_SUBMITTED") == 1
        assert count(rows, "backend", "GPU_VERTEX_REJECTED") == 1
        assert count(rows, "fallback", "VSH_REJECTED") == 1
        assert count(rows, "visibility", "UNKNOWN") == 5
    else:
        assert not any(category == "backend" for category, _ in rows)
        assert count(rows, "fallback", "VSH_UNAVAILABLE") == 1
        assert count(rows, "visibility", "UNKNOWN") == 4
    assert count(rows, "visibility", "NOT_SUBMITTED") == 10
    assert count(rows, "overflow", "TABLE_EVENTS") == 0


def test_executor_input_caps_are_counted_without_hiding_requests(tmp_path):
    _, report, _ = run_fixture(tmp_path, "capacity", "1", "capacity")
    rows = read_report(report)
    assert count(rows, "input", "ARRAY_REQUESTED") == 65792
    assert count(rows, "input", "ARRAY_BUFFERED") == 65536
    assert count(rows, "input", "ARRAY_DISCARDED") == 256
    assert count(rows, "input", "INLINE_WORDS_REQUESTED") == 65537
    assert count(rows, "input", "INLINE_WORDS_BUFFERED") == 65536
    assert count(rows, "input", "INLINE_WORDS_DISCARDED") == 1
    assert count(rows, "input", "IMMEDIATE_REQUESTED") == 1025
    assert count(rows, "input", "IMMEDIATE_BUFFERED") == 1024
    assert count(rows, "input", "IMMEDIATE_DISCARDED") == 1


def test_value_table_and_integer_overflow_are_reported(tmp_path):
    _, report, _ = run_fixture(tmp_path, "overflow", "1", "overflow")
    rows = read_report(report)
    assert count(rows, "method_seen", "2:0400") == 10
    assert count(rows, "method_value_other", "2:0400") == 4
    assert count(rows, "overflow", "METHOD_VALUE_OTHER") == 4
    assert count(rows, "overflow", "TABLE_EVENTS") > 0
    assert count(rows, "overflow", "COUNTER_SATURATIONS") > 0
    assert count(rows, "overflow", "INVALID_METHODS") == 2
    assert count(rows, "overflow", "INVALID_UNHANDLED") == 1
    assert count(rows, "overflow", "INVALID_MIRROR_ONLY") == 1
    assert rows[("input", "ARRAY_REQUESTED")][0] == 2**64 - 1


def test_rejections_and_offscreen_triangles_are_not_double_counted(tmp_path):
    _, report, _ = run_fixture(tmp_path, "rejections", "1", "rejections")
    rows = read_report(report)
    assert count(rows, "rejection", "INVALID_SURFACE_FORMAT") == 1
    assert count(rows, "rejection", "SCREEN_DEGENERATE") == 1
    assert count(rows, "cull", "OFFSCREEN_TRIANGLES") == 1
    assert count(rows, "output", "NO_TRIANGLES_EMITTED") == 3


def test_gpu_preparation_falls_back_and_reports_missing_program(tmp_path):
    _, report, _ = run_fixture(tmp_path, "missing-program", "1", "missing-program")
    rows = read_report(report)
    assert count(rows, "fallback", "VSH_PREPARATION") == 1
    assert count(rows, "reason", "TRANSFORM_FAILED") == 1
    assert count(rows, "route", "DROPPED") == 1
    assert count(rows, "output", "NO_TRIANGLES_EMITTED") == 1
    assert not any(category == "backend" for category, _ in rows)


def test_concurrent_report_snapshot_keeps_open_request_and_full_dimensions(tmp_path):
    _, report, _ = run_fixture(tmp_path, "snapshot", "1", "snapshot")
    rows = read_report(report)
    assert count(rows, "method_seen", "3:0770") == 10000
    assert count(rows, "method_value", "3:0770=00001234") == 10000
    assert count(rows, "clear_size", "70000:90000") == 1
    assert count(rows, "open_request", "00000008") == 1
    assert rows[("open_input", "INLINE_WORDS")] == (5, 4)
    assert count(rows, "summary", "REQUESTS") == 0


@pytest.mark.skipif(not D3D11_EXE, reason="set BRUTEFORCE_TEST_D3D11 for renderer integration")
@pytest.mark.parametrize(("mode", "route"), [
    ("cpu-vsh", "CPU_PROGRAM"),
    ("gpu-vsh", "GPU_VERTEX"),
    pytest.param("gpu-vsh-hardware", "GPU_VERTEX", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for hardware")),
])
def test_real_renderer_output_is_unchanged_with_audit(tmp_path, mode, route):
    snapshots = []
    for switch in ("0", "1"):
        env = clean_env()
        report = tmp_path / f"{mode}-{switch}.csv"
        image = tmp_path / f"{mode}-{switch}.raw"
        env["RECOMP_GPU_AUDIT"] = switch
        env["RECOMP_GPU_AUDIT_FILE"] = str(report)
        result = subprocess.run(
            [D3D11_EXE, str(XBE), mode, str(image)],
            capture_output=True, text=True, check=False, timeout=120, cwd=tmp_path, env=env,
        )
        assert result.returncode == 0, result.stderr
        assert "[GPU-AUDIT]" not in result.stderr
        snapshots.append((result.stdout, image.read_bytes()))
        if switch == "0":
            assert not report.exists()
        else:
            rows = read_report(report)
            assert count(rows, "route", route) > 0
            assert count(rows, "visibility", "UNKNOWN") >= count(rows, "route", route)
            if route == "GPU_VERTEX":
                assert count(rows, "backend", "GPU_VERTEX_SUBMITTED") > 0
    assert snapshots[0] == snapshots[1]
