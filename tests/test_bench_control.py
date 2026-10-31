import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import wave

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bench  # noqa: E402
import bench_control  # noqa: E402


@pytest.mark.parametrize("inherited", ["1", "0", ""])
def test_audio_off_unsets_inherited_presence_and_on_is_explicit(inherited):
    parent = {
        "RECOMP_AC97_READY": inherited, "RECOMP_AUDIO_WAV": "existing-user.wav",
        "recomp_audio_host": "1", "BRUTEFORCE_PROFILE": "old.txt", "OTHER": "keep",
    }
    off = bench.clean_environment(parent)
    assert off == {"OTHER": "keep"}
    assert not bench_control.audio_enabled(off)
    on = {**off, "RECOMP_AC97_READY": "1"}
    assert bench_control.audio_enabled(on)
    assert bench_control.audio_enabled({"RECOMP_AC97_READY": "0"})
    assert parent["RECOMP_AUDIO_WAV"] == "existing-user.wav"


def test_audio_paths_are_unique_reserved_and_never_overwrite(tmp_path, monkeypatch):
    first, second = tmp_path / "first", tmp_path / "second"
    first.mkdir()
    second.mkdir()
    env1, env2 = {"RECOMP_AC97_READY": "1"}, {"RECOMP_AC97_READY": "1"}
    one = bench_control.reserve_audio_capture(first, env1)
    two = bench_control.reserve_audio_capture(second, env2)
    assert one != two and one.is_file() and two.is_file()
    assert "RECOMP_AUDIO_HOST" not in env1 and "RECOMP_AUDIO_HOST" not in env2
    assert env1["RECOMP_AUDIO_WAV"] == str(one)
    assert bench_control.reserve_audio_capture(first, {}) is None

    class Fixed:
        hex = "occupied"

    monkeypatch.setattr(bench_control.uuid, "uuid4", lambda: Fixed())
    existing = first / "audio-occupied.wav"
    existing.write_bytes(b"user audio")
    with pytest.raises(bench_control.BenchControlError, match="non-overwriting"):
        bench_control.reserve_audio_capture(first, {"RECOMP_AC97_READY": "1"})
    assert existing.read_bytes() == b"user audio"


def test_audio_capture_rejects_external_path_and_early_cap(tmp_path):
    for key in ("RECOMP_AUDIO_WAV", "recomp_audio_wav_secs"):
        with pytest.raises(bench_control.BenchControlError, match="owns"):
            bench_control.reserve_audio_capture(tmp_path, {"RECOMP_AC97_READY": "1", key: "x"})


def write_wav(path, frames=5, channels=2):
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(channels)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"\x01\x00" * channels * frames)


def test_wav_requires_exact_payload_and_runtime_close_marker(tmp_path):
    path, log = tmp_path / "audio.wav", tmp_path / "stderr.txt"
    write_wav(path, frames=5)
    log.write_text("[AUDIO-WAV] writing file\n[AUDIO-WAV] closed: 5 samples (0.00 s)\n")
    info = bench_control.validate_wav_capture(path, log)
    assert info["finalized"] and info["sample_frames"] == 5 and info["channels"] == 2
    valid = path.read_bytes()

    path.write_bytes(valid[:-1])
    with pytest.raises(bench_control.BenchControlError, match="truncated"):
        bench_control.validate_wav_capture(path, log)
    path.write_bytes(valid + b"tail")
    with pytest.raises(bench_control.BenchControlError, match="stale header"):
        bench_control.validate_wav_capture(path, log)
    path.write_bytes(valid)
    log.write_text("[AUDIO-WAV] writing file\n")
    with pytest.raises(bench_control.BenchControlError, match="open/close markers"):
        bench_control.validate_wav_capture(path, log)
    log.write_text("[AUDIO-WAV] writing file\n[AUDIO-WAV] closed: 4 samples\n")
    with pytest.raises(bench_control.BenchControlError, match="does not match"):
        bench_control.validate_wav_capture(path, log)


def test_missing_empty_and_wrong_format_wavs_are_rejected(tmp_path):
    path, log = tmp_path / "audio.wav", tmp_path / "stderr.txt"
    log.write_text("[AUDIO-WAV] writing file\n[AUDIO-WAV] closed: 0 samples\n")
    with pytest.raises(bench_control.BenchControlError, match="missing or unreadable"):
        bench_control.validate_wav_capture(path, log)
    path.write_bytes(b"")
    with pytest.raises(bench_control.BenchControlError, match="truncated header"):
        bench_control.validate_wav_capture(path, log)
    write_wav(path, frames=0)
    with pytest.raises(bench_control.BenchControlError, match="empty"):
        bench_control.validate_wav_capture(path, log)
    write_wav(path, channels=1)
    with pytest.raises(bench_control.BenchControlError, match="stereo"):
        bench_control.validate_wav_capture(path, log)


def test_readiness_uses_unique_token_and_bounded_failure(tmp_path):
    ready = bench_control.ManualReadiness(tmp_path, "same visible tutorial hold", 10)
    request = json.loads((tmp_path / "ready.request.json").read_text())
    assert request["token"] == ready.token and request["state"] == ready.state
    assert "Set-Content -LiteralPath" in ready.instruction()
    assert not ready.poll(1)
    ready.ready_file.write_text("partial")
    assert not ready.poll(2)
    ready.ready_file.write_text(ready.token)
    assert ready.poll(2.5)

    other_dir = tmp_path / "retry"
    other_dir.mkdir()
    other = bench_control.ManualReadiness(other_dir, ready.state, 10)
    assert other.token != ready.token
    other.ready_file.write_text(ready.token)
    assert not other.poll(1)
    with pytest.raises(bench_control.BenchControlError, match="invalid readiness token"):
        other.poll(2.1)
    other.ready_file.unlink()
    with pytest.raises(bench_control.BenchControlError, match="timed out"):
        other.poll(10)


def test_audio_zero_is_not_an_off_mode_and_manual_is_required(tmp_path, capsys):
    out = tmp_path / "campaign"
    assert bench.main(["--variant", "off:RECOMP_AC97_READY=0", "--readiness", "manual",
                       "--ready-state", "tutorial hold", "--out", str(out)]) == 1
    assert "enabled by presence" in capsys.readouterr().err
    assert not out.exists()
    assert bench.main(["--capture-audio", "--variant", "on:RECOMP_AC97_READY=1",
                       "--out", str(out)]) == 1
    assert "--readiness manual" in capsys.readouterr().err


def test_legacy_audio_zero_remains_enabled_and_warns(tmp_path, monkeypatch, capsys):
    seen = []

    def fake_run(out_dir, name, _scene, env, *_args):
        out_dir.mkdir()
        seen.append(dict(env))
        return {"label": name, "state": "finishing", "generation": "generation-A"}

    monkeypatch.setattr(bench, "run_once", fake_run)
    out = tmp_path / "campaign"
    assert bench.main(["--runs", "1", "--retries", "0", "--variant",
                       "legacy:RECOMP_AC97_READY=0", "--out", str(out)]) == 0
    assert seen == [{"RECOMP_AC97_READY": "0"}]
    assert json.loads((out / "summary.json").read_text())["audio_modes"] == {"legacy": "on"}
    assert "enabled by presence" in capsys.readouterr().err


def test_audio_comparison_requires_manual_matched_protocol(tmp_path, capsys):
    out = tmp_path / "campaign"
    args = ["--scene", "movement", "--variant", "off:",
            "--variant", "on:RECOMP_AC97_READY=1", "--out", str(out)]
    assert bench.main(args) == 1
    assert "--readiness manual" in capsys.readouterr().err
    assert not out.exists()


def test_mixed_audio_campaign_records_only_individual_unmatched_results(tmp_path, monkeypatch):
    calls = []

    def fake_run(out_dir, name, scene, env, *_args):
        out_dir.mkdir()
        calls.append((name, dict(env)))
        return {
            "label": name, "state": "finishing", "generation": "generation-A",
            "options": {"readiness": "manual", "capture_audio": True, "capture_movement": True},
        }

    monkeypatch.setattr(bench, "run_once", fake_run)
    out = tmp_path / "campaign"
    assert bench.main([
        "--scene", "movement", "--runs", "3", "--retries", "0",
        "--variant", "off:", "--variant", "on:RECOMP_AC97_READY=1",
        "--readiness", "manual", "--ready-state", "tutorial stationary hold",
        "--no-auto-panels", "--capture", "--capture-movement", "--capture-audio",
        "--out", str(out),
    ]) == 0
    assert [name for name, _ in calls] == ["off", "on", "off", "on", "off", "on"]
    summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
    assert summary["audio_modes"] == {"off": "off", "on": "on"}
    assert summary["variant_options"] == {
        "off": {}, "on": {"RECOMP_AC97_READY": "1"},
    }
    assert summary["options"]["capture_audio"] is True
    assert summary["runs_per_variant"] == 3 and summary["generation"] == "generation-A"
    assert summary["audio_comparison"]["status"] == "not_performed"
    assert "content-matched" in summary["audio_comparison"]["reason"]


def test_inconclusive_world_analysis_cli_never_runs_game(tmp_path, monkeypatch, capsys):
    manifest = tmp_path / "labels.json"
    manifest.write_text(json.dumps({
        "region": [0, 0, 1, 1],
        "frames": [{"flip": 1, "t": 0.0, "label": "unknown"}],
    }))
    monkeypatch.setattr(bench, "run_once", lambda *_args: pytest.fail("game was started"))
    assert bench.main(["--analyze-world", str(manifest)]) == 0
    assert json.loads(capsys.readouterr().out)["status"] == "inconclusive"


def successful_stationary_rows():
    return [{
        "flip": index + 1, "t": index / 10, "draws": 800, "vertices": 1000,
        "hash": "1", "distinct": 0, "constants": "c",
    } for index in range(150)]


def fake_successful_run(monkeypatch, tmp_path, *, forced, capture_audio):
    class Process:
        returncode = None

        def poll(self):
            return None

    class Log:
        def __init__(self, _path):
            self.rows = []

        def poll(self):
            if self.rows:
                return []
            self.rows.extend(successful_stationary_rows())
            return self.rows[:]

    cwd = tmp_path / "generation" / "boots" / "id"
    monkeypatch.setattr(bench.build, "prepare_game_run",
                        lambda _config, **kwargs: (["fixture"], cwd, kwargs["base_env"]))
    monkeypatch.setattr(bench.subprocess, "Popen", lambda *_args, **_kwargs: Process())
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench_control, "shutdown_process", lambda _process: {
        "method": "ctrl_break", "forced": forced, "exit_code": 0xC000013A,
        "error": "forced cleanup" if forced else None,
    })
    return bench.run_once(
        tmp_path / "run", "fixture", "stationary",
        {"RECOMP_AC97_READY": "1"} if capture_audio else {},
        1, 900, False, False, "Release", capture_audio=capture_audio,
    )


def test_forced_process_cleanup_cannot_report_success(tmp_path, monkeypatch):
    result = fake_successful_run(monkeypatch, tmp_path, forced=True, capture_audio=False)
    assert result["state"] == "finishing"
    assert result["shutdown"]["forced"]
    assert result["error"] == "forced cleanup"
    assert bench.summarize([result])["fixture"]["runs"] == 0


def test_missing_or_unfinalized_wav_cannot_report_success(tmp_path, monkeypatch):
    result = fake_successful_run(monkeypatch, tmp_path, forced=False, capture_audio=True)
    assert result["state"] == "finishing"
    assert "truncated header" in result["audio"]["error"]
    assert result["error"] == result["audio"]["error"]
    assert bench.summarize([result])["fixture"]["runs"] == 0


def test_manual_run_ignores_legacy_progress_until_operator_ack(tmp_path, monkeypatch):
    class Process:
        returncode = None

        def poll(self):
            return None

    class Log:
        def __init__(self, _path):
            self.rows = []
            self.calls = 0

        def poll(self):
            self.calls += 1
            count = 30 if self.calls <= 5 else 150 if self.calls == 6 else 0
            start = len(self.rows)
            new = [{
                "flip": index + 1, "t": index / 10, "draws": 800, "vertices": 1000,
                "hash": "1", "distinct": 0, "constants": "c",
            } for index in range(start, start + count)]
            self.rows.extend(new)
            return new

    class Readiness:
        def __init__(self, _out, state, timeout):
            self.state, self.timeout, self.calls = state, timeout, 0

        def instruction(self):
            return "fixture readiness"

        def poll(self, _elapsed):
            self.calls += 1
            return self.calls >= 2

    observed_env = {}

    def prepare(_config, **kwargs):
        observed_env.update(kwargs["base_env"])
        return ["fixture"], tmp_path / "generation" / "boots" / "id", kwargs["base_env"]

    monkeypatch.setenv("RECOMP_AC97_READY", "1")
    monkeypatch.setattr(bench.build, "prepare_game_run", prepare)
    monkeypatch.setattr(bench.subprocess, "Popen", lambda *_args, **_kwargs: Process())
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench_control, "ManualReadiness", Readiness)
    monkeypatch.setattr(bench_control, "shutdown_process", lambda _process: {
        "method": "ctrl_break", "forced": False, "exit_code": 0xC000013A, "error": None,
    })
    clock = [0.0]

    def monotonic():
        clock[0] += 0.4
        return clock[0]

    monkeypatch.setattr(bench.time, "monotonic", monotonic)
    monkeypatch.setattr(bench.time, "sleep", lambda _seconds: None)
    result = bench.run_once(
        tmp_path / "manual-run", "manual", "stationary", {}, 1, 900, False, False,
        "Release", readiness="manual", ready_state="visible tutorial hold",
    )
    assert not result.get("error")
    assert result["readiness"]["acknowledged"]
    assert result["readiness"]["last_observed_flip"] == 60
    assert result["marks"]["world"] == 150
    assert result["readiness"]["csv_drain_seconds"] == 1
    assert "RECOMP_PAD_SCRIPT" not in observed_env
    assert not bench_control.audio_enabled(observed_env)


def test_manual_timeout_is_persisted_as_failed_run(tmp_path, monkeypatch):
    class Process:
        returncode = None

        def poll(self):
            return None

    class Log:
        def __init__(self, _path):
            self.rows = []

        def poll(self):
            return []

    class Readiness:
        def __init__(self, *_args):
            pass

        def instruction(self):
            return "fixture readiness"

        def poll(self, _elapsed):
            raise bench_control.BenchControlError("readiness timed out after 10 s")

    monkeypatch.setattr(
        bench.build, "prepare_game_run",
        lambda _config, **kwargs: (["fixture"], tmp_path / "generation" / "boots" / "id",
                                   kwargs["base_env"]))
    monkeypatch.setattr(bench.subprocess, "Popen", lambda *_args, **_kwargs: Process())
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench_control, "ManualReadiness", Readiness)
    monkeypatch.setattr(bench_control, "shutdown_process", lambda _process: {
        "method": "ctrl_break", "forced": False, "exit_code": 0xC000013A, "error": None,
    })
    result = bench.run_once(
        tmp_path / "timed-out", "manual", "stationary", {}, 1, 900, False, False,
        "Release", readiness="manual", ready_state="visible tutorial hold",
    )
    assert result["state"] == "manual_ready"
    assert result["error"] == "readiness timed out after 10 s"
    assert json.loads((tmp_path / "timed-out" / "result.json").read_text())["error"] == result["error"]


def test_manual_hold_has_its_own_bounded_phase_timeout(tmp_path, monkeypatch):
    class Process:
        returncode = None

        def poll(self):
            return None

    class Log:
        def __init__(self, _path):
            self.rows = []
            self.calls = 0

        def poll(self):
            self.calls += 1
            if self.calls != 3:
                return []
            self.rows.append({
                "flip": 1, "t": 1.0, "draws": 800, "vertices": 1,
                "hash": "1", "distinct": 0, "constants": "c",
            })
            return self.rows[:]

    class Readiness:
        def __init__(self, *_args):
            pass

        def instruction(self):
            return "fixture readiness"

        def poll(self, _elapsed):
            return True

    clock = [0.0]

    def monotonic():
        clock[0] += 0.4
        return clock[0]

    monkeypatch.setattr(
        bench.build, "prepare_game_run",
        lambda _config, **kwargs: (["fixture"], tmp_path / "generation" / "boots" / "id",
                                   kwargs["base_env"]))
    monkeypatch.setattr(bench.subprocess, "Popen", lambda *_args, **_kwargs: Process())
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench_control, "ManualReadiness", Readiness)
    monkeypatch.setattr(bench_control, "shutdown_process", lambda _process: {
        "method": "ctrl_break", "forced": False, "exit_code": 0xC000013A, "error": None,
    })
    monkeypatch.setattr(bench, "READY_DRAIN_SECONDS", 0)
    monkeypatch.setattr(bench.time, "monotonic", monotonic)
    monkeypatch.setattr(bench.time, "sleep", lambda _seconds: None)
    result = bench.run_once(
        tmp_path / "stuck", "manual", "stationary", {}, 1, 900, False, False,
        "Release", readiness="manual", ready_state="visible tutorial hold", phase_timeout=0.1,
    )
    assert result["state"] == "hold"
    assert result["error"] == "timed out after 0.1 s during hold"


def test_manual_run_rejects_changed_stationary_hold(tmp_path, monkeypatch):
    class Process:
        returncode = None

        def poll(self):
            return None

    class Log:
        def __init__(self, _path):
            self.rows = []
            self.calls = 0

        def poll(self):
            self.calls += 1
            if self.calls <= 2:
                indices = [self.calls - 1]
            elif self.calls == 3:
                indices = range(2, 24)
            else:
                return []
            new = [{
                "flip": index + 1, "t": index / 10, "draws": 800, "vertices": 1,
                "hash": "1", "distinct": int(index == 23), "constants": "c",
            } for index in indices]
            self.rows.extend(new)
            return new

    class Readiness:
        def __init__(self, *_args):
            pass

        def instruction(self):
            return "fixture readiness"

        def poll(self, _elapsed):
            return True

    monkeypatch.setattr(
        bench.build, "prepare_game_run",
        lambda _config, **kwargs: (["fixture"], tmp_path / "generation" / "boots" / "id",
                                   kwargs["base_env"]))
    monkeypatch.setattr(bench.subprocess, "Popen", lambda *_args, **_kwargs: Process())
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench_control, "ManualReadiness", Readiness)
    monkeypatch.setattr(bench_control, "shutdown_process", lambda _process: {
        "method": "ctrl_break", "forced": False, "exit_code": 0xC000013A, "error": None,
    })
    monkeypatch.setattr(bench, "READY_DRAIN_SECONDS", 0)
    monkeypatch.setattr(bench.time, "sleep", lambda _seconds: None)
    result = bench.run_once(
        tmp_path / "changed", "manual", "movement", {}, 1, 900, False, False,
        "Release", readiness="manual", ready_state="visible tutorial hold", phase_timeout=1,
    )
    assert result["state"] == "failed"
    assert "hold changed before stationary measurement completed" in result["error"]
    assert result["input_events"] == []


@pytest.mark.parametrize("acknowledge_all", [True, False])
def test_manual_movement_dispatches_full_script_in_buffered_log_batches(
        tmp_path, monkeypatch, acknowledge_all):
    class Process:
        returncode = None

        def poll(self):
            return None

    class Log:
        def __init__(self, _path):
            self.rows = []
            self.calls = 0

        def poll(self):
            self.calls += 1
            if self.calls == 1:
                new = [{"flip": 1, "t": 0.0, "draws": 800, "vertices": 1,
                        "hash": "1", "distinct": 0, "constants": "c"}]
            elif self.calls == 2:
                new = [{"flip": 2, "t": 0.05, "draws": 800, "vertices": 1,
                        "hash": "1", "distinct": 0, "constants": "c"}]
            elif self.calls == 3:
                new = [{
                    "flip": index + 2, "t": index / 10 + 0.05, "draws": 800, "vertices": 1,
                    "hash": "1", "distinct": 0, "constants": "c",
                } for index in range(1, 142)]
                for index, when in enumerate([item[0] for item in bench.MOVEMENT_INPUT] + [56.1]):
                    new.append({
                        "flip": index + 144, "t": 14.15 + when, "draws": 800, "vertices": 1,
                        "hash": "2", "distinct": 1, "constants": "d",
                    })
            else:
                return []
            self.rows.extend(new)
            return new

    class Readiness:
        def __init__(self, *_args):
            pass

        def instruction(self):
            return "fixture readiness"

        def poll(self, _elapsed):
            return True

    def shutdown(_process):
        events = bench.MOVEMENT_INPUT if acknowledge_all else bench.MOVEMENT_INPUT[:-1]
        with (tmp_path / "run" / "stderr.txt").open("a", encoding="utf-8") as stream:
            for when, command in events:
                stream.write(f'  PAD: live "{command}" at t={int(when * 1000)} ms\n')
        return {"method": "ctrl_break", "forced": False, "exit_code": 0xC000013A, "error": None}

    monkeypatch.setattr(
        bench.build, "prepare_game_run",
        lambda _config, **kwargs: (["fixture"], tmp_path / "generation" / "boots" / "id",
                                   kwargs["base_env"]))
    monkeypatch.setattr(bench.subprocess, "Popen", lambda *_args, **_kwargs: Process())
    monkeypatch.setattr(bench, "FrameLog", Log)
    monkeypatch.setattr(bench_control, "ManualReadiness", Readiness)
    monkeypatch.setattr(bench_control, "shutdown_process", shutdown)
    monkeypatch.setattr(bench, "READY_DRAIN_SECONDS", 0)
    monkeypatch.setattr(bench.time, "sleep", lambda _seconds: None)
    result = bench.run_once(
        tmp_path / "run", "manual", "movement", {}, 1, 900, False, False,
        "Release", readiness="manual", ready_state="visible tutorial hold",
        no_auto_panels=True, phase_timeout=1,
    )
    assert result["state"] == "finishing"
    assert [event["input"] for event in result["input_events"]] == [
        command for _when, command in bench.MOVEMENT_INPUT]
    assert len(result["input_runtime"]["queued"]) == len(bench.MOVEMENT_INPUT) - (not acknowledge_all)
    if acknowledge_all:
        assert not result.get("error")
    else:
        assert "did not acknowledge" in result["error"]


def test_shutdown_timeout_marks_forced_cleanup_as_failure(monkeypatch):
    class Process:
        def __init__(self):
            self.returncode = None

        def poll(self):
            return self.returncode

        def send_signal(self, _event):
            pass

        def wait(self, timeout=None):
            if self.returncode is None:
                raise subprocess.TimeoutExpired("fixture", timeout)
            return self.returncode

        def kill(self):
            self.returncode = 1

    monkeypatch.setattr(bench_control.signal, "CTRL_BREAK_EVENT", 1, raising=False)
    status = bench_control.shutdown_process(Process(), timeout=0.1)
    assert status["forced"] and "timed out" in status["error"]


@pytest.fixture(scope="module")
def native_wav_child(tmp_path_factory):
    if sys.platform != "win32":
        pytest.skip("the toolkit console-control handler is Windows-specific")
    gcc = shutil.which("gcc")
    source = ROOT / ".deps" / "xboxrecomp" / "src" / "apu" / "apu_wav.c"
    if not gcc or not source.is_file():
        pytest.skip("an existing MinGW compiler and toolkit checkout are required")
    output = tmp_path_factory.mktemp("bench-native") / "bench_wav_child.exe"
    command = [gcc, "-std=c11", "-O2", "-I", str(source.parent),
               str(ROOT / "tests" / "bench_wav_child.c"), str(source), "-o", str(output)]
    built = subprocess.run(command, capture_output=True, text=True, check=False)
    assert built.returncode == 0, built.stdout + built.stderr
    return output


def start_native_child(exe, tmp_path, audio=True):
    wav, ready = tmp_path / "audio.wav", tmp_path / "ready"
    stdout = (tmp_path / "stdout.txt").open("w", encoding="utf-8")
    stderr_path = tmp_path / "stderr.txt"
    stderr = stderr_path.open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(exe), str(wav) if audio else "-", str(ready)], stdin=subprocess.DEVNULL,
        stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    deadline = time.monotonic() + 10
    while not ready.exists() and time.monotonic() < deadline and process.poll() is None:
        time.sleep(0.02)
    if not ready.exists():
        if process.poll() is None:
            process.kill()
            process.wait()
        stdout.close()
        stderr.close()
        pytest.fail(f"native synthetic WAV child did not become ready: {stderr_path.read_text()}")
    return process, wav, stderr_path, stdout, stderr


def test_native_toolkit_ctrl_break_finalizes_wav(native_wav_child, tmp_path):
    process, wav, log, stdout, stderr = start_native_child(native_wav_child, tmp_path)
    try:
        status = bench_control.shutdown_process(process, timeout=5)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        stdout.close()
        stderr.close()
    assert not status["forced"] and status["error"] is None, status
    assert status["method"] == "ctrl_break"
    info = bench_control.validate_wav_capture(wav, log)
    assert info["sample_frames"] == 256 and info["finalized"]


def test_native_forced_kill_does_not_finalize_or_succeed(native_wav_child, tmp_path):
    process, wav, log, stdout, stderr = start_native_child(native_wav_child, tmp_path)
    try:
        process.kill()
        process.wait(timeout=5)
    finally:
        stdout.close()
        stderr.close()
    status = bench_control.shutdown_process(process)
    assert status["error"] and status["method"] == "already_exited"
    with pytest.raises(bench_control.BenchControlError):
        bench_control.validate_wav_capture(wav, log)


def test_native_audio_off_control_exit_needs_no_wav(native_wav_child, tmp_path):
    process, wav, _log, stdout, stderr = start_native_child(native_wav_child, tmp_path, audio=False)
    try:
        status = bench_control.shutdown_process(process, timeout=5)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        stdout.close()
        stderr.close()
    assert not status["forced"] and status["error"] is None, status
    assert not wav.exists()
