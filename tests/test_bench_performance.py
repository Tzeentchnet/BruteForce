import csv
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import bench
import bench_performance
from test_bench_match import campaign_run, save_json


def _review(kind="movement"):
    return {
        "kind": "real", "reviewer": "synthetic reviewer", "complete": True,
        "method": f"synthetic {kind} labels",
        "region_validation": "synthetic HUD excluded",
        "alignment_validation": "synthetic image identifiers and timestamps",
        "presentation_validation": "synthetic displayed frames",
    }


def _extend_frames(run):
    capture = run / "capture"
    labels, files = [], []
    with (run / "frames.csv").open("w", newline="", encoding="ascii") as stream:
        writer = csv.writer(stream)
        writer.writerow(["flip", "t_us"])
        for index in range(102):
            flip = index + 101
            timestamp = 1_000_000 + index * 100_000
            name = f"hold{flip:010}.bmp"
            image = Image.new("RGB", (4, 4), (80 + (index % 2) * 50, 90, 100))
            image.putpixel((3, 3), (10, 20, 30))
            image.save(capture / name)
            files.append(name)
            labels.append({"flip": flip, "t": timestamp / 1e6,
                           "image": str(Path("capture") / name),
                           "label": "world" if not index else "world_changed"})
            writer.writerow([flip, timestamp])
    save_json(run / "world-labels.json", {
        "region": [0, 0, 4, 4], "exclude": [], "frames": labels, "review": _review(),
    })
    return files


@pytest.fixture
def performance_manifest(tmp_path):
    campaign = tmp_path / "campaign"
    campaign.mkdir()
    pairs = []
    runs = []
    for index in range(6):
        run = campaign_run(campaign, index + 1, False)
        files = _extend_frames(run)
        path = run / "result.json"
        result = json.loads(path.read_text(encoding="utf-8"))
        enabled = index % 2 == 1
        environment = {"RECOMP_GPU": "d3d11", "RECOMP_AUDIO": "0",
                       "BRUTEFORCE_PERF_COUNTERS": "1"}
        if enabled:
            environment["RECOMP_PFB_FLUSH_WAIT"] = "1"
        result.update({
            "generation": "generation-A", "toolkit_tree": "a" * 40,
            "executable": {"sha256": "b" * 64}, "threads": 12,
            "runtime_environment": environment,
            "runtime_options": {
                "RECOMP_GPU": "d3d11", "RECOMP_PFB_FLUSH_WAIT": "1" if enabled else None,
                "RECOMP_NV2A_IDLE_WAIT": None, "RECOMP_NV2A_SEGMENTS": None,
            },
            "profile": {"game_thread": {"caller_attribution_complete": True,
                                         "gpu_wait_pct": 5}},
            "runtime_diagnostics": {"submit_caller_overflow": False,
                                    "submit_intervals": [
                                        {"kind": "pfb", "calls": 1, "site": "00371FBC",
                                         "caller": "00123456",
                                         "window_start_us": 2_000_000, "window_end_us": 11_000_000},
                                        {"kind": "put", "calls": 1, "site": "00371FA0",
                                         "caller": "00123456",
                                         "window_start_us": 2_000_000, "window_end_us": 11_000_000}]},
        })
        result["options"].update({"profile": True, "profile_callers": True})
        result["world_capture"]["files"] = files
        result["marks"]["movement_end"] = len(files) - 1
        save_json(path, result)
        runs.append((run, result))
        if enabled:
            a, b = runs[-2][0], run
            pairs.append({
                "baseline_result": str(a.relative_to(campaign) / "result.json"),
                "candidate_result": str(b.relative_to(campaign) / "result.json"),
                "baseline_labels": str(a.relative_to(campaign) / "world-labels.json"),
                "candidate_labels": str(b.relative_to(campaign) / "world-labels.json"),
                "matched": True, "start_match": "same synthetic start",
                "end_match": "same synthetic end", "interval_match": "same synthetic movement",
            })
    for role, gpu in (("cpu", "cpu"), ("gpu", "d3d11")):
        directory = campaign / role
        (directory / "capture").mkdir(parents=True)
        image = directory / "capture" / "frame.bmp"
        Image.new("RGB", (4, 4), (30, 40, 50)).save(image)
        result = json.loads(json.dumps(runs[1][1]))
        result["runtime_options"]["RECOMP_GPU"] = gpu
        result["runtime_environment"]["RECOMP_GPU"] = gpu
        result["captures"] = ["frame.bmp"]
        save_json(directory / "result.json", result)
    static_image = campaign / "native.bmp"
    static = Image.new("RGB", (4, 4), (50, 60, 70))
    static.putpixel((3, 3), (10, 20, 30))
    static.save(static_image)
    hold_labels = [{"flip": i, "t": i * 0.1, "image": "native.bmp",
                    "label": "world" if not i else "world_static"} for i in range(102)]
    save_json(campaign / "native-hold.json", {
        "region": [0, 0, 4, 4], "frames": hold_labels, "review": _review("hold"),
    })
    scaled = []
    for index in range(3):
        directory = campaign / f"scaled{index}"
        directory.mkdir()
        (directory / "capture").mkdir()
        Image.new("RGB", (4, 4), (30, 40, 50)).save(directory / "capture" / "frame.bmp")
        result = json.loads(json.dumps(runs[1][1]))
        result["scene"] = "stationary"
        result["stationary"] = {"fps": 30, "frames": 120, "seconds": 4,
                                "unit": "flips", "complete": True}
        result["captures"] = ["frame.bmp"]
        result["runtime_options"]["RECOMP_GPU_SCALE"] = "2"
        result["runtime_environment"]["RECOMP_GPU_SCALE"] = "2"
        save_json(directory / "result.json", result)
        scaled.append(str(directory.relative_to(campaign) / "result.json"))
    manifest = campaign / "performance.json"
    save_json(manifest, {
        "reviewer": "synthetic reviewer", "state": "reviewed tutorial",
        "input_tolerance_ms": 250, "pairs": pairs,
        "native": {"labels": pairs[0]["baseline_labels"], "matched": True,
                   "evidence": "synthetic native match"},
        "g4": {"reviewer": "synthetic reviewer", "matched": True,
               "matching_evidence": "synthetic CPU/GPU pair", "pairs": [{
                   "cpu_result": "cpu/result.json", "gpu_result": "gpu/result.json",
                   "cpu_image": "cpu/capture/frame.bmp", "gpu_image": "gpu/capture/frame.bmp",
               }]},
        "g5": {"reviewer": "synthetic reviewer", "matched": True,
               "matching_evidence": "synthetic stationary match",
               "native_hold_labels": "native-hold.json", "scaled_results": scaled,
               "scaled_review_evidence": {name: "synthetic held presentation"
                                          for name in scaled}},
    })
    return manifest


def test_reviewed_pairs_and_all_gates(performance_manifest):
    result = bench_performance.compare_manifest(performance_manifest, bench.MOVEMENT_INPUT)
    assert result["status"] == "pass", result
    assert all(gate["status"] == "pass" for gate in result["gates"].values())
    assert result["pairs"][0]["candidate"]["reviewed_world_cadence"]["frame_ms_p99"] == 100
    assert result["summary"]["candidate_world_low_1pct_fps_mean"] == 10
    assert "flips are not world FPS" in result["scope"]


@pytest.mark.parametrize("defect", [
    "unreviewed", "bad-order", "hash", "option", "input", "few-frames",
    "missing-profile", "bad-profile", "g4-difference", "g5-rate", "no-native",
    "caller-overflow", "g5-incomplete", "g5-unreviewed", "native-region",
    "g4-campaign-option", "unattributed-caller", "counter-outside-window",
])
def test_incomplete_or_failed_evidence_never_passes(performance_manifest, defect):
    root = performance_manifest.parent
    manifest = json.loads(performance_manifest.read_text(encoding="utf-8"))
    pair = manifest["pairs"][0]
    path = root / pair["candidate_result"]
    result = json.loads(path.read_text(encoding="utf-8"))
    if defect == "unreviewed":
        pair["matched"] = False
    elif defect == "bad-order":
        manifest["pairs"].reverse()
    elif defect == "hash":
        result["executable"]["sha256"] = "c" * 64
    elif defect == "option":
        result["runtime_environment"]["RECOMP_GPU_SCALE"] = "2"
    elif defect == "input":
        result["input_events"][0]["dispatch_seconds"] += 1
    elif defect == "few-frames":
        labels_path = root / pair["candidate_labels"]
        labels = json.loads(labels_path.read_text(encoding="utf-8"))
        labels["frames"] = labels["frames"][:4]
        save_json(labels_path, labels)
    elif defect == "missing-profile":
        result["profile"]["game_thread"]["caller_attribution_complete"] = False
    elif defect == "bad-profile":
        result["profile"]["game_thread"]["gpu_wait_pct"] = 10
    elif defect == "g4-difference":
        Image.new("RGB", (4, 4), (1, 2, 3)).save(root / "gpu" / "capture" / "frame.bmp")
        manifest["g4"]["all_differences_explained"] = False
    elif defect == "g5-rate":
        scaled = root / manifest["g5"]["scaled_results"][0]
        scaled_run = json.loads(scaled.read_text(encoding="utf-8"))
        scaled_run["stationary"]["fps"] = 1
        scaled_run["stationary"]["seconds"] = 120
        save_json(scaled, scaled_run)
    elif defect == "no-native":
        del manifest["native"]
    elif defect == "caller-overflow":
        result["runtime_diagnostics"]["submit_caller_overflow"] = True
    elif defect == "g5-incomplete":
        scaled = root / manifest["g5"]["scaled_results"][0]
        scaled_run = json.loads(scaled.read_text(encoding="utf-8"))
        scaled_run["stationary"]["complete"] = False
        save_json(scaled, scaled_run)
    elif defect == "g5-unreviewed":
        del manifest["g5"]["scaled_review_evidence"]
    elif defect == "native-region":
        native_labels = root / "native-other-region.json"
        baseline_labels = root / pair["baseline_labels"]
        native_review = json.loads(baseline_labels.read_text(encoding="utf-8"))
        native_review["region"] = [0, 0, 3, 3]
        # Images in the copied manifest must still resolve relative to it.
        native_review["frames"] = [
            {**frame, "image": str(baseline_labels.parent / frame["image"])}
            for frame in native_review["frames"]
        ]
        save_json(native_labels, native_review)
        manifest["native"]["labels"] = str(native_labels.name)
    elif defect == "g4-campaign-option":
        for role in ("cpu", "gpu"):
            ref_path = root / role / "result.json"
            ref_run = json.loads(ref_path.read_text(encoding="utf-8"))
            ref_run["runtime_environment"]["RECOMP_AUDIO"] = "1"
            save_json(ref_path, ref_run)
    elif defect == "unattributed-caller":
        result["runtime_diagnostics"]["submit_intervals"][0]["caller"] = "00000000"
    elif defect == "counter-outside-window":
        result["runtime_diagnostics"]["submit_intervals"][0]["window_start_us"] = 0
    save_json(path, result)
    save_json(performance_manifest, manifest)
    comparison = bench_performance.compare_manifest(performance_manifest, bench.MOVEMENT_INPUT)
    assert comparison["status"] != "pass"
    if defect == "missing-profile":
        assert comparison["gates"]["G3"]["status"] == "inconclusive"
    if defect == "bad-profile":
        assert comparison["gates"]["G3"]["status"] == "fail"
    if defect == "g4-difference":
        assert comparison["gates"]["G4"]["status"] == "fail"
    if defect == "g5-rate":
        assert comparison["gates"]["G5"]["status"] == "fail"


def test_owned_controls_and_performance_collection_requirements(tmp_path):
    for key in ("RECOMP_RASTER_THREADS", "RECOMP_PAD_SCRIPT", "RECOMP_PAD_LIVE",
                "RECOMP_FRAME_STATS", "BRUTEFORCE_PROFILE", "BRUTEFORCE_PROFILE_GATE"):
        with pytest.raises(bench.BenchError, match=key):
            bench.validate_owned_overrides({key.lower(): "4"})
    assert bench.main(["--performance", "--runs", "2",
                       "--variant", "base:", "--variant", "test:RECOMP_NV2A_IDLE_WAIT=1",
                       "--out", str(tmp_path / "never-created")]) == 1
    assert not (tmp_path / "never-created").exists()
    args = SimpleNamespace(runs=3, scene="movement", readiness="manual",
                           no_auto_panels=True, capture_world=True,
                           no_profile=False, profile_callers=True)
    with pytest.raises(bench.BenchError, match="BRUTEFORCE_PERF_COUNTERS"):
        bench.validate_performance_variants([
            ("base", {}), ("test", {"RECOMP_NV2A_IDLE_WAIT": "1"})], args)
    bench.validate_performance_variants([
        ("base", {
            "BRUTEFORCE_PERF_COUNTERS": "1", "RECOMP_GPU_TEX_INDEX": "0",
        }),
        ("test", {
            "BRUTEFORCE_PERF_COUNTERS": "1", "RECOMP_GPU_TEX_INDEX": "1",
        }),
    ], args)
    bench.validate_performance_variants([
        ("base", {
            "BRUTEFORCE_PERF_COUNTERS": "1", "RECOMP_PB_INDEX_RUNS": "0",
        }),
        ("test", {
            "BRUTEFORCE_PERF_COUNTERS": "1", "RECOMP_PB_INDEX_RUNS": "1",
        }),
    ], args)
    assert "RECOMP_GPU_TEX_INDEX" in bench.RUNTIME_OPTION_KEYS
    assert "RECOMP_PB_INDEX_RUNS" in bench.RUNTIME_OPTION_KEYS


def test_event_wait_profile_attribution_and_incomplete_stack(tmp_path):
    profile = tmp_path / "profile.txt"
    text = (
        "Profile (gate closed): 2000 samples over 2000 ticks (~1 ms each), 1 threads, 0 dropped\n"
        "THREAD,0,1,2000,1800,200,bruteforce!mainCRTStartup\n"
        "T0 tid 1 2000 samples\n"
        "      3.0%  bruteforce!sub_00372180\n"
        "== All threads: top modules\n"
        "CALLERS,complete_v1,0\n"
        "== Outside the exe, by first exe caller\n"
        "T0\n"
        "      4.0%  ntdll!NtWaitForMultipleObjects <- bruteforce!pfb_flush_wait_once\n"
        "      2.0%  ntdll!NtWaitForSingleObject <- bruteforce!nv2a_pfb_flush_wait\n"
        "      1.0%  ntdll!NtWaitForSingleObject <- bruteforce!nv2a_segment_wait_once\n"
        "      0.5%  ntdll!NtDelayExecution <- bruteforce!nv2a_segment_wait_once\n"
        "== All threads: top functions\n"
    )
    profile.write_text(text, encoding="utf-8")
    game = bench.parse_profile(profile)["game_thread"]
    assert game["gpu_wait_pct"] == 10.5
    assert game["caller_attribution_complete"] is True
    profile.write_text(text.replace("bruteforce!pfb_flush_wait_once", "(no exe frame)"),
                       encoding="utf-8")
    assert not bench.parse_profile(profile)["game_thread"]["caller_attribution_complete"]
    profile.write_text(text.replace("CALLERS,complete_v1,0", "CALLERS,complete_v1,2"),
                       encoding="utf-8")
    assert not bench.parse_profile(profile)["game_thread"]["caller_attribution_complete"]


def test_submit_diagnostic_parsing():
    text = ("[PERF-SUBMIT] kind=pfb site=00371FBC caller=00123456 calls=7 elapsed_ms=2.500"
            " window_start_us=100 window_end_us=200\n"
            "[PERF-SUBMIT] kind=put site=00372050 caller=00123456 calls=9 elapsed_ms=1.000\n"
            "[PERF-SHADER] kind=pixel-compile calls=3 failures=1 elapsed_ms=12.500"
            " window_start_us=100 window_end_us=200\n"
            "[NV2A-IDLE] waits=5 events=2 timers=3\n"
            "[NV2A-SEGMENT] published=7 completed=6 pending=1 high_water=2 queue_waits=1 caller_overflow=0\n"
            "[NV2A-SEGMENT-CALLER] caller=00123456 published=7 completed=6\n")
    diagnostics = bench.parse_runtime_diagnostics(text)
    items = diagnostics["submit_intervals"]
    assert [item["kind"] for item in items] == ["pfb", "put"]
    assert items[0]["caller"] == "00123456" and items[0]["calls"] == 7
    assert items[0]["window_start_us"] == 100 and items[0]["window_end_us"] == 200
    assert items[1]["window_start_us"] is None
    assert diagnostics["pixel_shader_compile_intervals"] == [
        {"calls": 3, "failures": 1, "elapsed_ms": 12.5,
         "window_start_us": 100, "window_end_us": 200}]
    assert diagnostics["idle_wait"]["snapshots"][0]["timers"] == 3
    assert diagnostics["segments"]["snapshots"][0]["pending"] == 1
    assert diagnostics["segments"]["callers"][0]["caller"] == "00123456"
