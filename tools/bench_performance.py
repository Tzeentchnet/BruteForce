"""Offline, reviewer-matched performance comparison; no measurements are inferred.

The manifest contains ``reviewer``, ``state``, ``input_tolerance_ms`` and at
least three ``pairs``. Each pair has baseline_result, candidate_result,
baseline_labels, candidate_labels, matched=true, start_match, end_match and
interval_match. Paths are relative to the manifest. ``native`` optionally
supplies labels, matched=true and evidence. ``g4`` and ``g5`` are separate
reviewed correctness/headroom evidence; absent evidence is inconclusive.
"""

import math
from pathlib import Path
import re
import statistics

import bench_match
import bench_world

REQUIRED_SWITCHES = (
    "RECOMP_PFB_FLUSH_WAIT", "RECOMP_NV2A_IDLE_WAIT", "RECOMP_NV2A_SEGMENTS",
)
SWITCHES = REQUIRED_SWITCHES + ("RECOMP_GPU_TEX_INDEX",)
MIN_INTERVALS = 100
MIN_SECONDS = 10


def _identity(run):
    executable = run.get("executable", {}).get("sha256")
    values = (run.get("generation"), run.get("toolkit_tree"), executable, run.get("threads"))
    if (not all(values) or not re.fullmatch(r"[0-9a-fA-F]{40}", str(values[1])) or
            not re.fullmatch(r"[0-9a-fA-F]{64}", str(executable)) or
            type(values[3]) is not int or values[3] < 1):
        raise bench_match.MatchError("missing generation, toolkit tree, executable hash or thread count")
    return values


def _clean_shutdown(run):
    shutdown = run.get("shutdown", {})
    return (shutdown.get("method") == "ctrl_break" and shutdown.get("forced") is False
            and shutdown.get("error") is None)


def _run(path, state):
    run = bench_match._read_json(path)
    if path.name != "result.json" or run.get("error") or run.get("state") != "finishing":
        raise bench_match.MatchError(f"run did not complete: {path}")
    if not _clean_shutdown(run):
        raise bench_match.MatchError(f"run did not exit cleanly: {path}")
    ready = run.get("readiness", {})
    options = run.get("options", {})
    if (run.get("scene") != "movement" or ready.get("mode") != "manual" or
            ready.get("acknowledged") is not True or ready.get("state") != state or
            run.get("automatic_panel_presses") is not False or
            options.get("automatic_panel_presses") is not False or
            options.get("readiness") != "manual" or options.get("capture_world") is not True or
            options.get("profile") is not True or options.get("profile_callers") is not True):
        raise bench_match.MatchError(f"run lacks reviewed-state, fixed-input, capture or profile controls: {path}")
    if not isinstance(run.get("runtime_environment"), dict) or not isinstance(run.get("runtime_options"), dict):
        raise bench_match.MatchError(f"run lacks complete effective runtime options: {path}")
    diagnostics = run.get("runtime_diagnostics") or {}
    kinds = {item.get("kind") for item in diagnostics.get("submit_intervals", [])
             if isinstance(item, dict) and item.get("calls", 0) > 0}
    if (run["runtime_environment"].get("BRUTEFORCE_PERF_COUNTERS") != "1" or
            diagnostics.get("submit_caller_overflow") is not False or
            not {"pfb", "put"}.issubset(kinds)):
        raise bench_match.MatchError(f"caller-attributed PFB/PUT counters are missing or overflowed: {path}")
    _switches(run)
    number = re.match(r"^run(\d+)-", path.parent.name)
    if not number:
        raise bench_match.MatchError(f"cannot establish run order: {path}")
    return run, int(number.group(1))


def _switches(run):
    options, environment = run["runtime_options"], run["runtime_environment"]
    for key in SWITCHES:
        if key in REQUIRED_SWITCHES and key not in options:
            raise bench_match.MatchError(f"missing or invalid {key}")
        if options.get(key) not in (None, "0", "1"):
            raise bench_match.MatchError(f"missing or invalid {key}")
        if options.get(key) != environment.get(key):
            raise bench_match.MatchError(f"effective {key} metadata disagrees")
    return tuple(options.get(key) == "1" for key in SWITCHES)


def _environment(run):
    return tuple(sorted((key, value) for key, value in run["runtime_environment"].items()
                        if key not in SWITCHES))


def _window(path, run, labels):
    result = bench_match._frame_window(path, run, labels)
    cadence = result.get("reviewed_world_cadence") or {}
    if (cadence.get("frames", 0) < MIN_INTERVALS or
            result["seconds"] < MIN_SECONDS or
            not result.get("reviewed_world_changes_per_second") or
            not result.get("reviewed_world_low_1pct_fps")):
        raise bench_match.MatchError(
            f"reviewed world cadence needs at least {MIN_INTERVALS} intervals and {MIN_SECONDS} seconds: {labels}")
    timed = []
    for item in (run.get("runtime_diagnostics") or {}).get("submit_intervals", []):
        if not isinstance(item, dict):
            continue
        start, end = item.get("window_start_us"), item.get("window_end_us")
        if (type(start) is int and type(end) is int and
                result["first_timestamp_us"] <= start < end <= result["last_timestamp_us"]):
            if (item.get("kind") not in ("pfb", "put") or type(item.get("calls")) is not int or
                    item["calls"] < 1 or
                    any(not re.fullmatch(r"[0-9A-Fa-f]{8}", str(item.get(key, ""))) or
                        int(item[key], 16) == 0 for key in ("site", "caller"))):
                raise bench_match.MatchError(f"unattributed PFB/PUT caller counters: {labels}")
            timed.append(item)
    if not {"pfb", "put"}.issubset({item.get("kind") for item in timed}):
        raise bench_match.MatchError(f"reviewed interval lacks timed PFB/PUT caller counters: {labels}")
    result["submit_caller_intervals"] = timed
    return result


def _mean(values):
    return round(statistics.mean(values), 3)


def _gate(status, reason, **data):
    return {"status": status, "reason": reason, **data}


def _native_gate(base, manifest, candidates, state):
    native = manifest.get("native")
    if not isinstance(native, dict) or native.get("matched") is not True:
        return _gate("inconclusive", "no reviewed native movement comparison")
    try:
        evidence = bench_match._text(native.get("evidence"), "native match evidence")
        labels = bench_match._path(base, native.get("labels"), "native labels")
        review = bench_world.analyze_manifest(labels)
        cadence = review.get("reviewed_world_cadence") or {}
        details = review["frame_details"]
        duration = details[-1]["t"] - details[0]["t"]
        native_fps = review.get("reviewed_world_changes_per_second")
        if (review["status"] != "reviewer_asserted" or cadence.get("frames", 0) < MIN_INTERVALS or
                duration < MIN_SECONDS or not native_fps):
            raise bench_match.MatchError("native world labels have insufficient reviewed cadence")
        if any(review[key] != candidates[0][field] for key, field in
               (("region", "region"), ("exclude", "exclude"),
                ("image_dimensions", "dimensions"), ("tolerance", "tolerance"))):
            raise bench_match.MatchError("native and candidate reviewed image regions differ")
        rates = [item["reviewed_world_changes_per_second"] for item in candidates]
        lows = [item["reviewed_world_low_1pct_fps"] for item in candidates]
        passed = all(rate >= native_fps and low >= 0.8 * native_fps
                     for rate, low in zip(rates, lows))
        return _gate("pass" if passed else "fail", "each reviewed run must reach native rate and 80% low",
                     native_world_fps=native_fps, candidate_world_fps_mean=_mean(rates),
                     candidate_world_fps_min=min(rates), candidate_low_1pct_fps_mean=_mean(lows),
                     candidate_low_1pct_fps_min=min(lows), native_labels=str(labels),
                     state=state, matching_evidence=evidence)
    except (bench_match.MatchError, bench_world.WorldLabelError, OSError, KeyError, TypeError, ValueError) as exc:
        return _gate("inconclusive", str(exc))


def _profile_gate(runs):
    values = []
    for run in runs:
        game = (run.get("profile") or {}).get("game_thread") or {}
        value = game.get("gpu_wait_pct")
        if (game.get("caller_attribution_complete") is not True or
                type(value) not in (int, float) or not math.isfinite(value) or
                not 0 <= value <= 100):
            return _gate("inconclusive", "complete, symbolized game-thread caller profiles are required")
        values.append(value)
    return _gate("pass" if all(value < 10 for value in values) else "fail",
                 "each game-thread GPU wait share must be below 10%",
                 mean_pct=_mean(values), max_pct=max(values), runs=len(values))


def _g4_gate(base, manifest, identity, candidate_switches, campaign_environment):
    evidence = manifest.get("g4")
    if not isinstance(evidence, dict) or evidence.get("matched") is not True:
        return _gate("inconclusive", "no reviewed CPU-reference frame pairs")
    try:
        from compare_frames import compare
        bench_match._text(evidence.get("reviewer"), "G4 reviewer")
        match = bench_match._text(evidence.get("matching_evidence"), "G4 matched-state evidence")
        pairs = evidence.get("pairs")
        if not isinstance(pairs, list) or not pairs:
            raise bench_match.MatchError("G4 needs at least one matched CPU/GPU image pair")
        comparisons = []
        for pair in pairs:
            refs = []
            for role in ("cpu", "gpu"):
                result_path = bench_match._path(base, pair.get(f"{role}_result"), f"{role} result")
                run = bench_match._read_json(result_path)
                if (run.get("error") or run.get("state") != "finishing" or not _clean_shutdown(run) or
                        _identity(run) != identity or not run.get("options", {}).get("capture_stationary") or
                        not run.get("readiness", {}).get("acknowledged")):
                    raise bench_match.MatchError(f"G4 {role} run lacks a matched captured identity")
                image = bench_match._path(base, pair.get(f"{role}_image"), f"{role} image")
                if image.parent != (result_path.parent / "capture").resolve() or image.name not in run.get("captures", []):
                    raise bench_match.MatchError(f"G4 {role} image is not a recorded capture")
                refs.append((run, image))
            if (refs[0][0].get("scene") != refs[1][0].get("scene") or
                    refs[0][0].get("readiness", {}).get("state") != refs[1][0].get("readiness", {}).get("state") or
                    refs[0][0].get("runtime_options", {}).get("RECOMP_GPU") != "cpu" or
                    refs[1][0].get("runtime_options", {}).get("RECOMP_GPU") != "d3d11" or
                    refs[0][0].get("runtime_environment", {}).get("RECOMP_GPU") != "cpu" or
                    refs[1][0].get("runtime_environment", {}).get("RECOMP_GPU") != "d3d11" or
                    _switches(refs[0][0]) != candidate_switches or
                    _switches(refs[1][0]) != candidate_switches or
                    any(tuple(sorted((k, v) for k, v in ref[0].get("runtime_environment", {}).items()
                                     if k not in SWITCHES and k != "RECOMP_GPU")) !=
                        tuple((k, v) for k, v in campaign_environment if k != "RECOMP_GPU")
                        for ref in refs) or
                    {k: v for k, v in refs[0][0].get("runtime_environment", {}).items() if k != "RECOMP_GPU"} !=
                    {k: v for k, v in refs[1][0].get("runtime_environment", {}).items() if k != "RECOMP_GPU"} or
                    refs[0][0].get("options") != refs[1][0].get("options")):
                raise bench_match.MatchError("G4 requires matched CPU and D3D11 reference captures")
            comparison = compare(refs[0][1], refs[1][1], tolerance=0,
                                 reference_label="CPU", candidate_label="D3D11")
            comparisons.append({"cpu_image": str(refs[0][1]), "gpu_image": str(refs[1][1]),
                                "pixels_differing": comparison["pixels_differing"]})
        different = any(item["pixels_differing"] for item in comparisons)
        explained = evidence.get("all_differences_explained")
        if different and type(explained) is not bool:
            raise bench_match.MatchError("G4 differences need an explicit explanation review")
        if different and explained:
            bench_match._text(evidence.get("difference_explanation"), "G4 listed differences")
        return _gate("fail" if different and not explained else "pass",
                     "matched CPU-reference image differences must be explained",
                     matching_evidence=match, comparisons=comparisons,
                     difference_explanation=evidence.get("difference_explanation"))
    except (ImportError, OSError, KeyError, TypeError, ValueError, bench_match.MatchError) as exc:
        return _gate("inconclusive", str(exc))


def _g5_gate(base, manifest, identity, candidate_switches, campaign_environment):
    evidence = manifest.get("g5")
    if not isinstance(evidence, dict) or evidence.get("matched") is not True:
        return _gate("inconclusive", "no reviewed 2x stationary headroom evidence")
    try:
        bench_match._text(evidence.get("reviewer"), "G5 reviewer")
        match = bench_match._text(evidence.get("matching_evidence"), "G5 matching evidence")
        native = bench_world.analyze_manifest(
            bench_match._path(base, evidence.get("native_hold_labels"), "native hold labels"))
        frames = native["frame_details"]
        duration = frames[-1]["t"] - frames[0]["t"]
        review = native["review"]
        complete_review = (review.get("kind") == "real" and review.get("complete") is True and
                           all(isinstance(review.get(key), str) and review[key].strip() for key in
                               ("reviewer", "method", "region_validation", "alignment_validation",
                                "presentation_validation")))
        if (not complete_review or not native["consecutive_flip_ids"] or
                len(frames) < 100 or duration < MIN_SECONDS or
                any(frame["classification"] not in ("world_baseline", "world_static", "hud_only")
                    for frame in frames)):
            raise bench_match.MatchError("native hold needs at least 100 reviewed flips over 10 seconds")
        native_rate = (len(frames) - 1) / duration
        paths = evidence.get("scaled_results")
        if not isinstance(paths, list) or len(paths) < 3 or len(set(paths)) != len(paths):
            raise bench_match.MatchError("G5 needs at least three distinct 2x hold runs")
        scaled_reviews = evidence.get("scaled_review_evidence")
        if not isinstance(scaled_reviews, dict) or set(scaled_reviews) != set(paths):
            raise bench_match.MatchError("G5 needs an explicit review for every scaled hold")
        rates = []
        common = None
        for name in paths:
            bench_match._text(scaled_reviews[name], "scaled hold review")
            path = bench_match._path(base, name, "scaled result")
            run = bench_match._read_json(path)
            opts = run.get("runtime_options", {})
            ready = run.get("readiness", {})
            stationary = run.get("stationary") or {}
            rate = stationary.get("fps")
            flips, seconds = stationary.get("frames"), stationary.get("seconds")
            current = (ready.get("state"), run.get("scene"), run.get("options"),
                       tuple(sorted((k, v) for k, v in run.get("runtime_environment", {}).items()
                                    if k != "RECOMP_GPU_SCALE")))
            captures = run.get("captures", [])
            if (run.get("error") or run.get("state") != "finishing" or not _clean_shutdown(run) or
                    _identity(run) != identity or
                    run.get("scene") != "stationary" or
                    not run.get("options", {}).get("capture_stationary") or
                    not captures or any(not (path.parent / "capture" / name).is_file()
                                        or Path(name).name != name for name in captures) or
                    ready.get("mode") != "manual" or ready.get("acknowledged") is not True or
                    opts.get("RECOMP_GPU_SCALE") != "2" or
                    run.get("runtime_environment", {}).get("RECOMP_GPU_SCALE") != "2" or
                    _switches(run) != candidate_switches or
                    tuple(sorted((k, v) for k, v in run.get("runtime_environment", {}).items()
                                 if k not in SWITCHES and k != "RECOMP_GPU_SCALE")) !=
                    tuple((k, v) for k, v in campaign_environment if k != "RECOMP_GPU_SCALE") or
                    stationary.get("complete") is not True or stationary.get("unit") != "flips" or
                    type(flips) is not int or flips < 100 or
                    type(seconds) not in (int, float) or not math.isfinite(seconds) or seconds <= 0 or
                    type(rate) not in (int, float) or
                    not math.isfinite(rate) or rate <= 0 or
                    abs(rate - flips / seconds) > max(0.01, rate * 0.001)):
                raise bench_match.MatchError("invalid or unmatched 2x stationary run")
            if common is not None and current != common:
                raise bench_match.MatchError("2x hold scene or non-scaling options differ")
            common = current
            rates.append(rate)
        return _gate("pass" if all(rate >= native_rate for rate in rates) else "fail",
                     "each 2x stationary flip rate must reach the reviewed native hold rate",
                     native_hold_flips_per_second=round(native_rate, 3),
                     scaled_hold_flips_per_second_mean=_mean(rates),
                     scaled_hold_flips_per_second_min=min(rates), matching_evidence=match)
    except (bench_match.MatchError, bench_world.WorldLabelError, OSError, KeyError, TypeError, ValueError) as exc:
        return _gate("inconclusive", str(exc))


def _compare(path, movement_input):
    manifest = bench_match._read_json(path)
    reviewer = bench_match._text(manifest.get("reviewer"), "reviewer")
    state = bench_match._text(manifest.get("state"), "state")
    tolerance = manifest.get("input_tolerance_ms")
    if type(tolerance) not in (int, float) or not math.isfinite(tolerance) or not 0 <= tolerance <= 500:
        raise bench_match.MatchError("input_tolerance_ms must be between 0 and 500")
    pairs = manifest.get("pairs")
    if not isinstance(pairs, list) or len(pairs) < 3:
        raise bench_match.MatchError("at least three reviewed alternating pairs are required")
    entries, numbers, baseline_runs, candidate_runs = [], [], [], []
    campaign = identity = common = candidate_switches = image_identity = None
    for pair in pairs:
        if not isinstance(pair, dict) or pair.get("matched") is not True:
            raise bench_match.MatchError("each pair requires matched-state review")
        match = {key: bench_match._text(pair.get(key), key)
                 for key in ("start_match", "end_match", "interval_match")}
        paths = [bench_match._path(path.parent, pair.get(f"{side}_result"), f"{side} result")
                 for side in ("baseline", "candidate")]
        if paths[0].parent.parent != paths[1].parent.parent:
            raise bench_match.MatchError("paired runs are not in the same campaign")
        if campaign is None:
            campaign = paths[0].parent.parent
        if campaign != paths[0].parent.parent:
            raise bench_match.MatchError("all runs must use the same campaign")
        (a, an), (b, bn) = [_run(p, state) for p in paths]
        if bn != an + 1:
            raise bench_match.MatchError("baseline/candidate runs must be adjacent in A/B order")
        numbers.extend((an, bn))
        current_identity = _identity(a)
        if current_identity != _identity(b) or (identity is not None and current_identity != identity):
            raise bench_match.MatchError("generation, toolkit, executable and thread count must match")
        identity = current_identity
        if any(_switches(a)) or not any(_switches(b)):
            raise bench_match.MatchError("expected an all-off baseline and an enabled candidate")
        if candidate_switches is not None and _switches(b) != candidate_switches:
            raise bench_match.MatchError("candidate switches changed across pairs")
        candidate_switches = _switches(b)
        if _environment(a) != _environment(b) or a["options"] != b["options"]:
            raise bench_match.MatchError("nonexperimental runtime or benchmark options differ")
        current = (_environment(a), a["options"])
        if common is not None and current != common:
            raise bench_match.MatchError("nonexperimental options changed across pairs")
        common = current
        bench_match._matching_input(a, b, tolerance, movement_input)
        windows = [_window(p, run, bench_match._path(path.parent, pair.get(f"{side}_labels"),
                                                     f"{side} labels"))
                   for p, run, side in zip(paths, (a, b), ("baseline", "candidate"))]
        for key in ("region", "exclude", "dimensions", "tolerance"):
            if windows[0][key] != windows[1][key]:
                raise bench_match.MatchError(f"reviewed image {key} differs between variants")
        current_image_identity = tuple(windows[0][key] for key in
                                       ("region", "exclude", "dimensions", "tolerance"))
        if image_identity is not None and current_image_identity != image_identity:
            raise bench_match.MatchError("reviewed image region changed across pairs")
        image_identity = current_image_identity
        entries.append({"baseline": windows[0], "candidate": windows[1], "state_evidence": match})
        baseline_runs.append(a)
        candidate_runs.append(b)
    if len(set(numbers)) != len(numbers) or numbers != list(range(numbers[0], numbers[0] + len(numbers))):
        raise bench_match.MatchError("runs must be unique and alternate A/B/A throughout")
    candidate_windows = [entry["candidate"] for entry in entries]
    gates = {
        "G2": _native_gate(path.parent, manifest, candidate_windows, state),
        "G3": _profile_gate(candidate_runs),
        "G4": _g4_gate(path.parent, manifest, identity, candidate_switches, common[0]),
        "G5": _g5_gate(path.parent, manifest, identity, candidate_switches, common[0]),
    }
    status = ("inconclusive" if any(g["status"] == "inconclusive" for g in gates.values()) else
              "fail" if any(g["status"] == "fail" for g in gates.values()) else "pass")
    baseline_windows = [entry["baseline"] for entry in entries]
    base_fps = _mean(item["reviewed_world_changes_per_second"] for item in baseline_windows)
    candidate_fps = _mean(item["reviewed_world_changes_per_second"] for item in candidate_windows)
    return {"status": status, "reviewer": reviewer, "state": state,
            "generation": identity[0], "toolkit_tree": identity[1],
            "executable_sha256": identity[2], "threads": identity[3],
            "candidate_switches": dict(zip(SWITCHES, candidate_switches)),
            "summary": {
                "baseline_world_fps_mean": base_fps,
                "candidate_world_fps_mean": candidate_fps,
                "candidate_minus_baseline_world_fps": round(candidate_fps - base_fps, 3),
                "baseline_world_low_1pct_fps_mean": _mean(
                    item["reviewed_world_low_1pct_fps"] for item in baseline_windows),
                "candidate_world_low_1pct_fps_mean": _mean(
                    item["reviewed_world_low_1pct_fps"] for item in candidate_windows),
            },
            "pairs": entries, "gates": gates,
            "scope": "Reviewer-asserted distinct world changes; flips are not world FPS. "
                     "Continuous capture and profiling add diagnostic overhead."}


def compare_manifest(path, movement_input):
    try:
        return _compare(Path(path).resolve(), movement_input)
    except (bench_match.MatchError, bench_world.WorldLabelError, OSError, AttributeError,
            IndexError, KeyError, TypeError, ValueError) as exc:
        return {"status": "inconclusive", "reason": str(exc), "pairs": None,
                "gates": {key: _gate("inconclusive", "comparison evidence is incomplete")
                          for key in ("G2", "G3", "G4", "G5")}}
