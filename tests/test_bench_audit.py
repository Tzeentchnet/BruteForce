import json
import os
from pathlib import Path
import sys
import time

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bench  # noqa: E402
import bench_control  # noqa: E402


def write_report(path, count):
    Path(path).write_text(
        "# GPU audit: submissions are not visibility; active_begin=0\n"
        "category,key,count,vertices\n"
        f"summary,REQUESTS,{count},0\n"
        "overflow,INVALID_MIRROR_ONLY,0,0\n",
        encoding="ascii",
    )


@pytest.fixture
def fake_runtime(monkeypatch, tmp_path):
    class State:
        launches = []
        fail_first = False
        write_reports = True
        truncate_on_shutdown = False

    state = State()
    boot_dir = tmp_path / "synthetic-generation" / "boots" / "shared"
    boot_dir.mkdir(parents=True)

    class Log:
        def __init__(self, _path):
            self.rows = []

        def poll(self):
            if self.rows:
                return []
            self.rows.extend({
                "flip": index + 1, "t": index / 10, "draws": 800,
                "vertices": 1000, "hash": "1", "distinct": 0, "constants": "c",
            } for index in range(1000))
            return self.rows[:]

    class Process:
        def __init__(self, env, cwd):
            self.number = len(state.launches) + 1
            self.returncode = 1 if state.fail_first and self.number == 1 else None
            self.active = True
            self.report = None
            if env.get("RECOMP_GPU_AUDIT") == "1":
                self.report = Path(env.get("RECOMP_GPU_AUDIT_FILE", cwd / "gpu_audit.csv"))
                if state.write_reports:
                    write_report(self.report, self.number)

        def poll(self):
            return self.returncode

    def launch(_command, *, cwd, env, **_kwargs):
        process = Process(env, cwd)
        state.launches.append((dict(env), process))
        return process

    def sleep(_seconds):
        for _env, process in state.launches:
            if process.active and process.returncode is None and process.report and state.write_reports:
                write_report(process.report, process.number + 100)
                stamp = time.time() + 1
                os.utime(process.report, (stamp, stamp))

    def shutdown(process):
        process.active = False
        if process.report and state.truncate_on_shutdown:
            process.report.write_text("# GPU audit: incomplete\n", encoding="ascii")
        return {"method": "ctrl_break", "forced": False, "exit_code": 0xC000013A,
                "error": None}

    monkeypatch.setattr(
        bench.build, "prepare_game_run",
        lambda _config, **kwargs: (["synthetic-host"], boot_dir, kwargs["base_env"]),
    )
    monkeypatch.setattr(bench.subprocess, "Popen", launch)
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench.time, "sleep", sleep)
    monkeypatch.setattr(bench_control, "shutdown_process", shutdown)
    return state


def test_gpu_audit_campaign_isolates_retry_reports_and_records_options(
        tmp_path, monkeypatch, fake_runtime):
    inherited = tmp_path / "inherited.csv"
    inherited.write_text("do not overwrite", encoding="ascii")
    monkeypatch.setenv("RECOMP_GPU_AUDIT", "1")
    monkeypatch.setenv("RECOMP_GPU_AUDIT_FILE", str(inherited))
    fake_runtime.fail_first = True
    campaign = tmp_path / "campaign with spaces"
    assert bench.main([
        "--runs", "2", "--retries", "1", "--no-profile",
        "--variant", "on:RECOMP_GPU_AUDIT=1",
        "--variant", "off:RECOMP_GPU_AUDIT=0",
        "--variant", "default:",
        "--out", str(campaign),
    ]) == 0

    first = campaign / "run01-on"
    retry = campaign / "run01-on-retry1"
    later = campaign / "run04-on"
    paths = [directory / "gpu_audit.csv" for directory in (first, retry, later)]
    for path, index in zip(paths, (0, 1, 4)):
        env, _process = fake_runtime.launches[index]
        assert env["RECOMP_GPU_AUDIT"] == "1"
        assert env["RECOMP_GPU_AUDIT_FILE"] == str(path.resolve())
        assert path.is_file()
    assert "summary,REQUESTS,1,0" in paths[0].read_text(encoding="ascii")
    assert "summary,REQUESTS,102,0" in paths[1].read_text(encoding="ascii")
    assert "summary,REQUESTS,105,0" in paths[2].read_text(encoding="ascii")
    assert inherited.read_text(encoding="ascii") == "do not overwrite"
    assert "RECOMP_GPU_AUDIT_FILE" not in fake_runtime.launches[2][0]
    assert "RECOMP_GPU_AUDIT" not in fake_runtime.launches[3][0]
    assert "RECOMP_GPU_AUDIT_FILE" not in fake_runtime.launches[3][0]

    failed = json.loads((first / "result.json").read_text(encoding="utf-8"))
    succeeded = json.loads((retry / "result.json").read_text(encoding="utf-8"))
    repeated = json.loads((later / "result.json").read_text(encoding="utf-8"))
    off = json.loads((campaign / "run02-off" / "result.json").read_text(encoding="utf-8"))
    default = json.loads((campaign / "run03-default" / "result.json").read_text(encoding="utf-8"))
    summary = json.loads((campaign / "summary.json").read_text(encoding="utf-8"))
    assert failed["error"] and succeeded["attempts"] == 2
    assert succeeded["attempt_errors"][0]["out_dir"] == str(first)
    for result, path in zip((failed, succeeded, repeated), paths):
        assert result["env"]["RECOMP_GPU_AUDIT"] == "1"
        assert result["runtime_options"]["RECOMP_GPU_AUDIT"] == "1"
        assert result["runtime_options"]["RECOMP_GPU_AUDIT_FILE"] == str(path.resolve())
        assert result["gpu_audit"]["path"] == str(path.resolve())
        assert result["gpu_audit"]["report_complete"]
    assert not failed["gpu_audit"]["snapshot_after_window_confirmed"]
    assert succeeded["gpu_audit"]["snapshot_after_window_confirmed"]
    assert repeated["gpu_audit"]["snapshot_after_window_confirmed"]
    assert off["runtime_options"]["RECOMP_GPU_AUDIT"] == "0"
    assert default["runtime_options"]["RECOMP_GPU_AUDIT"] is None
    assert off["gpu_audit"] is None and default["gpu_audit"] is None
    assert summary["variant_options"] == {
        "on": {"RECOMP_GPU_AUDIT": "1"}, "off": {"RECOMP_GPU_AUDIT": "0"},
        "default": {},
    }


@pytest.mark.parametrize("switch", ["0", "true", "1 ", "", None])
def test_gpu_audit_only_enables_exact_one_and_records_disabled_values(
        tmp_path, monkeypatch, fake_runtime, switch):
    monkeypatch.setenv("RECOMP_GPU_AUDIT", "1")
    monkeypatch.setenv("RECOMP_GPU_AUDIT_FILE", str(tmp_path / "inherited.csv"))
    overrides = {} if switch is None else {"RECOMP_GPU_AUDIT": switch}
    result = bench.run_once(
        tmp_path / "run", "cpu", "stationary", overrides, 1, 900, False, False, "Release",
    )
    env = fake_runtime.launches[0][0]
    assert not result.get("error")
    assert result["runtime_options"]["RECOMP_GPU_AUDIT"] == switch
    assert result["runtime_options"]["RECOMP_GPU_AUDIT_FILE"] is None
    assert "RECOMP_GPU_AUDIT_FILE" not in env
    assert result["gpu_audit"] is None


def test_gpu_audit_case_alias_is_canonicalized_without_changing_original_option(
        tmp_path, fake_runtime):
    result = bench.run_once(
        tmp_path / "run", "cpu", "stationary", {"recomp_gpu_audit": "1"},
        1, 900, False, False, "Release",
    )
    env = fake_runtime.launches[0][0]
    assert result["env"] == {"recomp_gpu_audit": "1"}
    assert env["RECOMP_GPU_AUDIT"] == result["runtime_options"]["RECOMP_GPU_AUDIT"] == "1"
    assert "recomp_gpu_audit" not in env
    assert result["gpu_audit"]["report_complete"]


@pytest.mark.parametrize("key", ["RECOMP_GPU_AUDIT_FILE", "recomp_gpu_audit_file"])
def test_gpu_audit_report_path_override_is_rejected_before_creating_directories(
        tmp_path, capsys, key):
    campaign = tmp_path / "campaign"
    assert bench.main([
        "--variant", f"on:RECOMP_GPU_AUDIT=1,{key}=shared.csv",
        "--out", str(campaign),
    ]) == 1
    assert "RECOMP_GPU_AUDIT_FILE" in capsys.readouterr().err
    assert not campaign.exists()
    attempt = tmp_path / "attempt"
    with pytest.raises(bench.BenchError, match="RECOMP_GPU_AUDIT_FILE"):
        bench.run_once(
            attempt, "on", "stationary", {"RECOMP_GPU_AUDIT": "1", key: "shared.csv"},
            1, 900, False, False, "Release",
        )
    assert not attempt.exists()


def test_gpu_audit_rejects_case_duplicate_options(tmp_path):
    attempt = tmp_path / "attempt"
    with pytest.raises(bench.BenchError, match="RECOMP_GPU_AUDIT"):
        bench.run_once(
            attempt, "on", "stationary",
            {"RECOMP_GPU_AUDIT": "1", "recomp_gpu_audit": "0"},
            1, 900, False, False, "Release",
        )
    assert not attempt.exists()


def test_gpu_audit_waits_for_a_complete_post_window_snapshot(tmp_path, monkeypatch):
    report = tmp_path / "gpu_audit.csv"
    write_report(report, 1)
    os.utime(report, (999, 999))
    clock = [0.0]
    sleeps = []

    def sleep(seconds):
        clock[0] += seconds
        sleeps.append(seconds)
        if len(sleeps) == 1:
            report.write_text("# GPU audit: incomplete\n", encoding="ascii")
        else:
            write_report(report, 2)
        os.utime(report, (1001, 1001))

    class Alive:
        def poll(self):
            return None

    monkeypatch.setattr(bench.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(bench.time, "time", lambda: 1000)
    monkeypatch.setattr(bench.time, "sleep", sleep)
    assert bench.wait_for_outputs(tmp_path, tmp_path / "stderr.txt", False, False, Alive(),
                                  timeout=3, audit_path=report)
    assert len(sleeps) == 2


def test_gpu_audit_missing_report_times_out(tmp_path, monkeypatch):
    clock = [0.0]

    class Alive:
        def poll(self):
            return None

    monkeypatch.setattr(bench.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(bench.time, "time", lambda: 1000)
    monkeypatch.setattr(bench.time, "sleep", lambda seconds: clock.__setitem__(0, clock[0] + seconds))
    assert not bench.wait_for_outputs(
        tmp_path, tmp_path / "stderr.txt", False, False, Alive(), timeout=1,
        audit_path=tmp_path / "gpu_audit.csv",
    )


def test_gpu_audit_missing_report_fails_attempt(tmp_path, monkeypatch, fake_runtime):
    fake_runtime.write_reports = False
    monkeypatch.setattr(bench, "wait_for_outputs", lambda *_args, **_kwargs: False)
    result = bench.run_once(
        tmp_path / "run", "cpu", "stationary", {"RECOMP_GPU_AUDIT": "1"},
        1, 900, False, False, "Release",
    )
    assert "GPU audit" in result["error"]
    assert not result["gpu_audit"]["report_complete"]
    assert not result["gpu_audit"]["snapshot_after_window_confirmed"]


def test_gpu_audit_truncated_at_shutdown_fails_attempt(tmp_path, fake_runtime):
    fake_runtime.truncate_on_shutdown = True
    result = bench.run_once(
        tmp_path / "run", "cpu", "stationary", {"RECOMP_GPU_AUDIT": "1"},
        1, 900, False, False, "Release",
    )
    assert result["gpu_audit"]["snapshot_after_window_confirmed"]
    assert not result["gpu_audit"]["report_complete"]
    assert "GPU audit report" in result["error"]


AUDIT_EXE = os.environ.get("BRUTEFORCE_TEST_GPU_AUDIT")
XBE = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"


@pytest.mark.skipif(not AUDIT_EXE or not XBE.is_file(), reason="native synthetic GPU audit fixture unavailable")
def test_gpu_audit_native_fixture_receives_benchmark_environment(tmp_path, monkeypatch):
    boot_dir = tmp_path / "synthetic-generation" / "boots" / "fixture"
    boot_dir.mkdir(parents=True)
    snapshot = tmp_path / "synthetic.snapshot"
    monkeypatch.setattr(
        bench.build, "prepare_game_run",
        lambda _config, **kwargs: (
            [AUDIT_EXE, str(XBE), "cpu", str(snapshot)], boot_dir, kwargs["base_env"],
        ),
    )
    result = bench.run_once(
        tmp_path / "run with spaces", "cpu", "stationary", {"RECOMP_GPU_AUDIT": "1"},
        1, 900, False, False, "Release",
    )
    report = tmp_path / "run with spaces" / "gpu_audit.csv"
    assert result["exit_code"] == 0
    assert result["error"].startswith("process exited with code 0")
    assert result["runtime_options"]["RECOMP_GPU_AUDIT_FILE"] == str(report.resolve())
    assert result["gpu_audit"]["report_complete"]
    assert "summary,REQUESTS," in report.read_text(encoding="ascii")
    assert not (boot_dir / "gpu_audit.csv").exists()
