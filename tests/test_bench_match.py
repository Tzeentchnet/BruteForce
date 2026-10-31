import csv
import json
import sys
import wave
from pathlib import Path

import pytest
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bench  # noqa: E402
import bench_match  # noqa: E402
import bench_world  # noqa: E402


def save_json(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


def campaign_run(campaign, number, audio, tagged=True):
    mode = "on" if audio else "off"
    run = campaign / f"run{number:02d}-{mode}"
    capture = run / "capture"
    capture.mkdir(parents=True)
    labels = []
    files = []
    for index in range(3):
        image = Image.new("RGB", (4, 4), (80, 90, 100))
        if index == 2:
            image.putpixel((2, 2), (150, 90, 100))
        name = f"hold{index + 101:010}.bmp" if tagged else f"hold{index:05}.bmp"
        image.save(capture / name)
        files.append(name)
        labels.append({
            "flip": index + 101, "t": (1_000_000 + index * 100_000) / 1e6,
            "image": str(Path("capture") / name),
            "label": ("world", "world_static", "world_changed")[index],
        })
    save_json(run / "world-labels.json", {
        "region": [0, 0, 4, 4], "exclude": [[0, 0, 1, 1]],
        "frames": labels,
        "review": {
            "kind": "real", "reviewer": "test reviewer", "complete": True,
            "method": "synthetic labels simulate a real-frame review",
            "region_validation": "synthetic HUD is excluded",
            "alignment_validation": "synthetic flips and captures were generated together",
            "presentation_validation": "synthetic images are the test frames",
        },
    })
    with (run / "frames.csv").open("w", newline="", encoding="ascii") as stream:
        writer = csv.writer(stream)
        writer.writerow(["flip", "t_us"])
        for index in range(3):
            writer.writerow([index + 101, 1_000_000 + index * 100_000])
    wav_info = {"mode": "off", "wav_path": None}
    if audio:
        wav = run / "audio.wav"
        with wave.open(str(wav), "wb") as stream:
            stream.setnchannels(2)
            stream.setsampwidth(2)
            stream.setframerate(48000)
            stream.writeframes(b"\x01\x00\x02\x00" * 5)
        (run / "stderr.txt").write_text(
            "[AUDIO-WAV] writing test\n[AUDIO-WAV] closed: 5 samples\n", encoding="utf-8")
        wav_info = {
            "mode": "on", "wav_path": str(wav.resolve()),
            "wav": {"path": str(wav.resolve()), "finalized": True},
        }
    inputs = [{
        "kind": "script", "input": command, "scheduled_seconds": when,
        "dispatch_seconds": when,
    } for when, command in bench.MOVEMENT_INPUT]
    queued = [{"input": event["input"], "pad_time_ms": 1000 + event["scheduled_seconds"] * 1000}
              for event in inputs]
    result = {
        "state": "finishing", "scene": "movement", "generation": "generation-A",
        "toolkit_tree": "tree-A", "threads": 12,
        "options": {
            "config": "Release", "watchdog_seconds": 3600,
            "profile": False, "census": False, "periodic_dump": False,
            "trace_frame": False, "trace_after_seconds": None,
            "capture_stationary": True, "capture_movement": False,
            "capture_world": True, "capture_audio": True,
            "readiness": "manual", "ready_timeout_seconds": 900,
            "phase_timeout_seconds": 600, "automatic_panel_presses": False,
        },
        "shutdown": {"method": "ctrl_break", "forced": False, "error": None},
        "readiness": {"mode": "manual", "state": "reviewed tutorial", "acknowledged": True},
        "automatic_panel_presses": False,
        "audio": wav_info,
        "env": ({"RECOMP_GPU": "d3d11", "RECOMP_AC97_READY": "1"} if audio
                else {"RECOMP_GPU": "d3d11"}),
        "world_capture": {
            "files": files,
            **({"alignment": bench_world.FLIP_ID_ALIGNMENT} if tagged else {}),
        },
        "marks": {"movement_start": 0, "movement_end": 2},
        "input_events": inputs, "input_runtime": {"queued": queued},
    }
    save_json(run / "result.json", result)
    return run


@pytest.fixture
def reviewed_campaign(tmp_path):
    campaign = tmp_path / "campaign"
    campaign.mkdir()
    pairs = []
    for index in range(3):
        off = campaign_run(campaign, 2 * index + 1, False)
        on = campaign_run(campaign, 2 * index + 2, True)
        pairs.append({
            "off_result": str(off.relative_to(campaign) / "result.json"),
            "on_result": str(on.relative_to(campaign) / "result.json"),
            "off_labels": str(off.relative_to(campaign) / "world-labels.json"),
            "on_labels": str(on.relative_to(campaign) / "world-labels.json"),
            "matched": True,
            "start_match": "synthetic start images match",
            "end_match": "synthetic end images match",
            "interval_match": "all synthetic frame transitions reviewed",
        })
    manifest = campaign / "matches.json"
    save_json(manifest, {
        "reviewer": "synthetic test reviewer", "state": "reviewed tutorial",
        "input_tolerance_ms": 250, "pairs": pairs,
    })
    return manifest


def test_matched_review_compares_only_reviewed_intervals(reviewed_campaign):
    report = bench_match.compare_manifest(reviewed_campaign, bench.MOVEMENT_INPUT)
    assert report["status"] == "reviewer_asserted"
    assert len(report["pairs"]) == 3
    assert report["mean_on_minus_off_flips_per_second"] == 0
    assert report["mean_on_minus_off_reviewed_world_changes_per_second"] == 0
    assert "not world FPS" in report["scope"]


def test_legacy_numbered_capture_still_requires_reviewer_alignment(tmp_path):
    run = campaign_run(tmp_path, 1, False, tagged=False)
    result_path = run / "result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    report = bench_match._frame_window(result_path, result, run / "world-labels.json")
    assert (report["first_flip"], report["last_flip"]) == (101, 103)


@pytest.mark.parametrize("defect", [
    "no_match", "unknown", "bad_timestamp", "wrong_roi", "input_timing",
    "forced_exit", "truncated_wav", "missing_sequence", "missing_capture",
    "changed_options", "missing_options", "wrong_flip_filename",
])
def test_any_unmatched_or_incomplete_evidence_prevents_comparison(reviewed_campaign, defect):
    manifest = json.loads(reviewed_campaign.read_text(encoding="utf-8"))
    pair = manifest["pairs"][0]
    on_dir = reviewed_campaign.parent / Path(pair["on_result"]).parent
    result_path = on_dir / "result.json"
    labels_path = on_dir / "world-labels.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    labels = json.loads(labels_path.read_text(encoding="utf-8"))
    if defect == "no_match":
        pair["matched"] = False
        save_json(reviewed_campaign, manifest)
    elif defect == "unknown":
        labels["frames"][2]["label"] = "unknown"
        save_json(labels_path, labels)
    elif defect == "bad_timestamp":
        labels["frames"][1]["t"] += 0.001
        save_json(labels_path, labels)
    elif defect == "wrong_roi":
        labels["exclude"] = []
        save_json(labels_path, labels)
    elif defect == "input_timing":
        result["input_events"][1]["dispatch_seconds"] += 1
        save_json(result_path, result)
    elif defect == "forced_exit":
        result["shutdown"]["forced"] = True
        save_json(result_path, result)
    elif defect == "truncated_wav":
        wav = Path(result["audio"]["wav_path"])
        wav.write_bytes(wav.read_bytes()[:-1])
    elif defect == "missing_sequence":
        result["input_events"].pop()
        result["input_runtime"]["queued"].pop()
        save_json(result_path, result)
    elif defect == "changed_options":
        result["options"]["profile"] = True
        save_json(result_path, result)
    elif defect == "missing_options":
        del result["options"]
        save_json(result_path, result)
    elif defect == "wrong_flip_filename":
        old = on_dir / "capture" / "hold0000000102.bmp"
        new = on_dir / "capture" / "hold0000000202.bmp"
        old.rename(new)
        result["world_capture"]["files"][1] = new.name
        labels["frames"][1]["image"] = str(Path("capture") / new.name)
        save_json(result_path, result)
        save_json(labels_path, labels)
    else:
        (on_dir / "capture" / "hold0000000102.bmp").unlink()
    report = bench_match.compare_manifest(reviewed_campaign, bench.MOVEMENT_INPUT)
    assert report["status"] == "inconclusive"
    assert report["pairs"] is None
    assert "mean_on_minus_off_flips_per_second" not in report
    if defect == "wrong_flip_filename":
        assert "capture filename does not match reviewed flip" in report["reason"]


def test_matched_comparison_cli_never_runs_game(reviewed_campaign, monkeypatch, capsys):
    monkeypatch.setattr(bench, "run_once", lambda *_args: pytest.fail("game was started"))
    assert bench.main(["--compare-matched", str(reviewed_campaign)]) == 0
    assert json.loads(capsys.readouterr().out)["status"] == "reviewer_asserted"


def test_missing_real_evidence_is_inconclusive_even_in_offline_cli(tmp_path, monkeypatch, capsys):
    manifest = tmp_path / "matches.json"
    save_json(manifest, {"reviewer": "test", "state": "tutorial", "pairs": []})
    monkeypatch.setattr(bench, "run_once", lambda *_args: pytest.fail("game was started"))
    assert bench.main(["--compare-matched", str(manifest)]) == 1
    result = json.loads(capsys.readouterr().out)
    assert result["status"] == "inconclusive" and result["pairs"] is None
