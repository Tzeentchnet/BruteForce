import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bench  # noqa: E402


HEADER = "flip,t_us,draws,program_vertices,hash,distinct,constants\n"


def row(flip, t, distinct=1, constants="c", vertices=200000, hashed=True):
    return (f"{flip},{int(t * 1e6)},10,{vertices},"
            f"{('%016x' % (flip + 1)) if hashed else '0' * 16},{distinct},{constants}\n")


def test_parse_env_names_and_values():
    assert bench.parse_env("gpu:RECOMP_GPU=null,X=1") == ("gpu", {"RECOMP_GPU": "null", "X": "1"})
    assert bench.parse_env("A=1") == ("A=1", {"A": "1"})
    assert bench.parse_env("cpu:") == ("cpu", {})
    with pytest.raises(bench.BenchError):
        bench.parse_env("broken")


def test_frame_log_reads_partial_lines(tmp_path):
    path = tmp_path / "frames.csv"
    path.write_text(HEADER + row(1, 1.0) + row(2, 1.5)[:5], encoding="ascii")
    log = bench.FrameLog(path)
    assert [r["flip"] for r in log.poll()] == [1]
    with path.open("a", encoding="ascii") as stream:
        stream.write(row(2, 1.5)[5:] + row(3, 2.0))
    assert [r["flip"] for r in log.poll()] == [2, 3]
    assert log.rows[1]["t"] == 1.5


def test_flip_capture_waits_for_new_images_not_buffered_frame_rows(tmp_path):
    gate = bench.FlipCapture(tmp_path / "capture.flag", tmp_path / "hold")
    (tmp_path / "hold").write_bytes(b"report-time image")
    gate.open()
    assert not gate.poll()
    assert gate.flag.exists()
    (tmp_path / "hold00000.bmp").write_bytes(b"first flip")
    assert not gate.poll()
    (tmp_path / "hold00001.bmp").write_bytes(b"second flip")
    assert gate.poll()
    assert not gate.flag.exists()
    assert [path.name for path in gate.files()] == ["hold00000.bmp", "hold00001.bmp"]


def test_missing_profile_is_not_reported_as_success(tmp_path):
    class Exited:
        def poll(self):
            return 1

    stderr = tmp_path / "stderr.txt"
    stderr.write_text("", encoding="utf-8")
    assert not bench.wait_for_outputs(tmp_path, stderr, True, False, Exited())


def rows_from(lines, tmp_path):
    path = tmp_path / "f.csv"
    path.write_text(HEADER + "".join(lines), encoding="ascii")
    log = bench.FrameLog(path)
    log.poll()
    return log.rows


def test_distinct_uses_pixels_and_falls_back_to_constants(tmp_path):
    rows = rows_from([row(1, 0.0), row(2, 0.1, 0), row(3, 0.2, 1), row(4, 0.3, 0)], tmp_path)
    result = bench.distinct_metrics(rows, 0, 4)
    assert result["source"] == "pixels" and result["distinct_frames"] == 1
    rows = rows_from([row(1, 0.0, -1, "a", hashed=False), row(2, 0.1, -1, "a", hashed=False),
                      row(3, 0.2, -1, "b", hashed=False), row(4, 0.3, -1, "b", hashed=False)],
                     tmp_path)
    result = bench.distinct_metrics(rows, 0, 4)
    assert result["source"] == "constants" and result["distinct_frames"] == 1


def test_constant_cycles_count_as_unchanged(tmp_path):
    signatures = ["a", "a", "a", "a", "p"] * 4 + ["n1", "n2"]
    rows = rows_from([row(i + 1, i * 0.1, -1, c, hashed=False) for i, c in enumerate(signatures)],
                     tmp_path)
    flags = [bench.changed(rows, i)[0] for i in range(len(rows))]
    assert flags[5:20] == [False] * 15, "a periodic extra pass repeats a known signature"
    assert flags[20:] == [True, True]


def test_frame_metrics_rate_and_percentiles(tmp_path):
    rows = rows_from([row(i, i * 0.5) for i in range(1, 11)] + [row(11, 6.0)], tmp_path)
    metrics = bench.frame_metrics(rows)
    assert metrics["frames"] == 10
    assert metrics["frame_ms_p50"] == 500.0
    assert metrics["frame_ms_p99"] == 1000.0
    assert metrics["low_1pct_fps"] == 1.0


def test_agreement_requires_pixels(tmp_path):
    rows = rows_from([row(1, 0, 1, "a"), row(2, 1, 0, "a"), row(3, 2, 1, "b"), row(4, 3, 1, "b")],
                     tmp_path)
    assert bench.pixel_constant_agreement(rows, 0, 4) == {"agree": 2, "total": 3}
    rows = rows_from([row(1, 0, -1), row(2, 1, -1)], tmp_path)
    assert bench.pixel_constant_agreement(rows, 0, 2) is None


def test_parse_profile_threads_and_game_wait(tmp_path):
    path = tmp_path / "profile.txt"
    path.write_text(
        "Profile (gate closed): 300 samples over 100 ticks\n\n"
        "THREAD,0,11,100,90,10,bruteforce!mainCRTStartup\n"
        "THREAD,3,12,100,80,20,bruteforce!nv2a_ack_thread\n"
        "THREAD,9,13,100,30,70,bruteforce!raster_worker\n\n"
        "== Threads (samples)\n"
        "T0   tid 11  100 samples  busy  90.0%  start bruteforce!mainCRTStartup\n"
        "      module  90.0%  bruteforce\n"
        "       70.0%  bruteforce!sub_00372180\n"
        "       10.0%  ntdll!NtWaitForSingleObject\n"
        "        5.0%  bruteforce!sub_00371CE0\n"
        "T3   tid 12  100 samples  busy  80.0%  start bruteforce!nv2a_ack_thread\n"
        "       40.0%  bruteforce!nv2a_vsh_run\n"
        "T9   tid 13  100 samples  busy  30.0%  start bruteforce!raster_worker\n"
        "       70.0%  ntdll!NtWaitForSingleObject\n"
        "\n== All threads: top modules\n", encoding="utf-8")
    profile = bench.parse_profile(path)
    assert profile["game_thread"]["kernel_wait_pct"] == 10.0
    assert profile["game_thread"]["guest_gpu_poll_pct"] == 75.0
    assert profile["game_thread"]["gpu_wait_pct"] == 85.0
    assert profile["nv2a_thread"]["busy_pct"] == 80.0
    assert profile["raster_workers"] == {"count": 1, "mean_busy_pct": 30.0}


def test_summary_groups_by_label():
    results = [
        {"label": "a", "stationary": {"fps": 3.0}}, {"label": "b", "stationary": {"fps": 5.0}},
        {"label": "a", "stationary": {"fps": 4.0}},
    ]
    table = bench.summarize(results)
    assert list(table) == ["a", "b"]
    assert table["a"]["stationary_fps"] == {"mean": 3.5, "min": 3.0, "max": 4.0, "n": 2}
    assert table["b"]["movement_fps"] is None


def test_summary_excludes_failed_runs():
    table = bench.summarize([
        {"label": "gpu", "stationary": {"fps": 30}},
        {"label": "gpu", "stationary": {"fps": 999}, "error": "process exited"},
    ])
    assert table["gpu"]["runs"] == 1 and table["gpu"]["failed"] == 1
    assert table["gpu"]["stationary_fps"]["mean"] == 30


def test_summary_counts_failed_attempts_including_retries():
    table = bench.summarize([
        {"label": "gpu", "attempts": 2, "stationary": {"fps": 30}},
        {"label": "gpu", "attempts": 3, "error": "process stalled"},
        {"label": "gpu", "stationary": {"fps": 28}},
    ])
    assert table["gpu"]["runs"] == 2 and table["gpu"]["failed"] == 1
    assert table["gpu"]["failed_attempts"] == 4
    assert table["gpu"]["stationary_fps"]["mean"] == 29


def test_retry_failure_is_persisted_and_reported(monkeypatch, tmp_path, capsys):
    import json

    calls = []

    def run_once(out_dir, label, *_args):
        out_dir.mkdir()
        calls.append(out_dir)
        if len(calls) == 1:
            return {"label": label, "state": "world", "error": "stalled"}
        return {"label": label, "state": "finishing", "stationary": {"fps": 30}}

    monkeypatch.setattr(bench, "run_once", run_once)
    out = tmp_path / "campaign"
    assert bench.main(["--runs", "1", "--retries", "1", "--variant", "gpu:",
                       "--out", str(out)]) == 0
    summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    result = json.loads((calls[1] / "result.json").read_text(encoding="utf-8"))
    assert summary["table"]["gpu"]["failed_attempts"] == 1
    assert result["attempts"] == 2
    assert result["attempt_errors"] == [{"attempt": 1, "error": "stalled",
                                          "out_dir": str(calls[0])}]
    assert "1 failed attempts" in capsys.readouterr().out


def test_compare_frames_counts_and_locates_differences(tmp_path):
    import compare_frames
    from PIL import Image

    a = Image.new("RGB", (64, 32), (10, 20, 30))
    b = a.copy()
    for x in range(40, 44):
        for y in range(8, 12):
            b.putpixel((x, y), (200, 20, 30))
    b.putpixel((0, 0), (12, 20, 30))                 # within tolerance
    a.save(tmp_path / "a.bmp")
    b.save(tmp_path / "b.bmp")
    result = compare_frames.compare(tmp_path / "a.bmp", tmp_path / "b.bmp", 8, 16,
                                    tmp_path / "diff.png")
    assert result["pixels_over_tolerance"] == 16
    assert result["histogram"]["1-2"] == 1 and result["histogram"]["33-255"] == 16
    assert result["largest_regions"][0] == {"x": 32, "y": 0, "pixels": 16}
    assert (tmp_path / "diff.png").is_file()
