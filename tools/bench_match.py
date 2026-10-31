r"""Compare audio modes only over independently reviewed matched intervals.

Run --capture-world in each attempt, then label the captured images as in
bench_world.py. A local matching manifest has a reviewer, a visible ``state``,
an ``input_tolerance_ms`` (at most 500), and at least three alternating pairs:

    {
      "reviewer": "name",
      "state": "precise visible tutorial state",
      "input_tolerance_ms": 250,
      "pairs": [{
        "off_result": "run01-off\\result.json",
        "on_result": "run02-on\\result.json",
        "off_labels": "run01-off\\world-labels.json",
        "on_labels": "run02-on\\world-labels.json",
        "matched": true,
        "start_match": "visible evidence of matching start states",
        "end_match": "visible evidence of matching end states",
        "interval_match": "how the entire interval was reviewed as matched"
      }]
    }

These are human assertions, not a way to infer matching from equal pixels.
Each label file must cover consecutive, time-aligned captured flips inside
the measurement and have complete real-frame, ROI, alignment and presentation
reviews. Unavailable evidence yields no numerical comparison.
"""

import csv
import json
import math
from pathlib import Path
import re
import statistics

import bench_control
import bench_world


class MatchError(ValueError):
    pass


OPTION_FLAGS = (
    "profile", "census", "periodic_dump", "trace_frame", "capture_stationary",
    "capture_movement", "capture_world", "capture_audio", "automatic_panel_presses",
)
OPTION_FIELDS = frozenset(OPTION_FLAGS + (
    "config", "watchdog_seconds", "trace_after_seconds", "readiness",
    "ready_timeout_seconds", "phase_timeout_seconds",
))


def _read_json(path):
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise MatchError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise MatchError(f"{path} must contain a JSON object")
    return value


def _text(value, name):
    if not isinstance(value, str) or not value.strip():
        raise MatchError(f"missing reviewed evidence: {name}")
    return value.strip()


def _path(base, spec, name):
    return (base / _text(spec, name)).resolve()


def _run(path, mode, state):
    result = _read_json(path)
    if path.name != "result.json":
        raise MatchError(f"expected a run result.json: {path}")
    if result.get("error") or result.get("state") != "finishing":
        raise MatchError(f"run did not complete successfully: {path}")
    shutdown = result.get("shutdown", {})
    if (shutdown.get("method") != "ctrl_break" or shutdown.get("forced") is not False or
            shutdown.get("error") is not None):
        raise MatchError(f"run did not complete the non-forced console-control exit: {path}")
    readiness = result.get("readiness", {})
    if (readiness.get("mode") != "manual" or readiness.get("acknowledged") is not True or
            readiness.get("state") != state or result.get("automatic_panel_presses") is not False):
        raise MatchError(f"run does not have the requested operator-confirmed, fixed-input state: {path}")
    options = result.get("options")
    if (not isinstance(options, dict) or not OPTION_FIELDS.issubset(options) or
            any(type(options[key]) is not bool for key in OPTION_FLAGS) or
            options["readiness"] != "manual" or options["automatic_panel_presses"] or
            not options["capture_audio"] or not options["capture_world"]):
        raise MatchError(f"run lacks the complete benchmark option record required for matching: {path}")
    audio = result.get("audio", {})
    env = result.get("env", {})
    if not isinstance(env, dict) or audio.get("mode") != mode or (
            bench_control.audio_enabled(env) != (mode == "on")):
        raise MatchError(f"audio mode and environment disagree in {path}")
    if mode == "on":
        wav = audio.get("wav", {})
        wav_path = Path(wav.get("path", "")).resolve()
        if (wav.get("finalized") is not True or
                str(wav_path) != audio.get("wav_path") or wav_path.parent != path.parent):
            raise MatchError(f"audio-on run lacks its isolated finalized WAV: {path}")
        try:
            bench_control.validate_wav_capture(wav_path, path.parent / "stderr.txt")
        except bench_control.BenchControlError as exc:
            raise MatchError(f"audio-on WAV is invalid in {path}: {exc}") from exc
    elif audio.get("wav_path") is not None:
        raise MatchError(f"audio-off run unexpectedly captured a WAV: {path}")
    if result.get("scene") != "movement" or not result.get("world_capture", {}).get("files"):
        raise MatchError(f"continuous movement captures are required: {path}")
    match = re.match(r"^run(\d+)-", path.parent.name)
    if not match:
        raise MatchError(f"cannot establish campaign order from {path.parent.name}")
    return result, int(match.group(1))


def _frame_window(result_path, result, labels_path):
    try:
        report = bench_world.analyze_manifest(labels_path)
    except bench_world.WorldLabelError as exc:
        raise MatchError(f"invalid movement labels {labels_path}: {exc}") from exc
    if report["status"] != "reviewer_asserted":
        raise MatchError(f"movement labels remain inconclusive: {labels_path}")
    try:
        with (result_path.parent / "frames.csv").open("r", encoding="ascii", newline="") as stream:
            rows = [(int(row["flip"]), int(row["t_us"])) for row in csv.DictReader(stream)]
        marks = result["marks"]
        start, end = rows[marks["movement_start"]][0], rows[marks["movement_end"]][0]
    except (OSError, KeyError, TypeError, IndexError, ValueError) as exc:
        raise MatchError(f"cannot establish movement flip bounds in {result_path}: {exc}") from exc
    times = dict(rows)
    if len(times) != len(rows):
        raise MatchError(f"duplicate flip IDs in {result_path.parent / 'frames.csv'}")
    capture_dir = (result_path.parent / "capture").resolve()
    captured = set(result["world_capture"]["files"])
    tagged = result["world_capture"].get("alignment") == bench_world.FLIP_ID_ALIGNMENT
    used = set()
    details = report["frame_details"]
    for frame in details:
        image = frame["image"]
        if not isinstance(image, str) or not image:
            raise MatchError(f"missing image for reviewed flip {frame['flip']}")
        image_path = (labels_path.parent / image).resolve()
        if image_path.parent != capture_dir or image_path.name not in captured or image_path in used:
            raise MatchError(f"reviewed flip is not a unique movement capture: {image_path}")
        used.add(image_path)
        flip = frame["flip"]
        if tagged:
            match = re.fullmatch(r"hold([0-9]{10})\.bmp", image_path.name)
            if not match or int(match.group(1)) != flip:
                raise MatchError(f"capture filename does not match reviewed flip {flip}: {image_path}")
        if (not start <= flip <= end or flip not in times or
                round(frame["t"] * 1_000_000) != times[flip]):
            raise MatchError(f"reviewed flip/time does not match the recorded movement: {flip}")
    duration = details[-1]["t"] - details[0]["t"]
    if duration <= 0:
        raise MatchError(f"reviewed interval has no duration: {labels_path}")
    return {
        "first_flip": details[0]["flip"], "last_flip": details[-1]["flip"],
        "seconds": round(duration, 3), "flips_per_second": round((len(details) - 1) / duration, 3),
        "reviewed_world_changes_per_second": report["reviewed_world_changes_per_second"],
        "region": report["region"], "exclude": report["exclude"],
        "dimensions": report["image_dimensions"], "tolerance": report["tolerance"],
        "label_manifest": str(labels_path),
    }


def _matching_input(off, on, tolerance, movement_input):
    off_events, on_events = off.get("input_events", []), on.get("input_events", [])
    off_queued = off.get("input_runtime", {}).get("queued", [])
    on_queued = on.get("input_runtime", {}).get("queued", [])
    if not off_events or len(off_events) != len(on_events):
        raise MatchError("fixed input event sequences are missing or differ")
    if len(off_queued) != len(off_events) or len(on_queued) != len(on_events):
        raise MatchError("runtime input acknowledgements are incomplete")
    try:
        if [(event["scheduled_seconds"], event["input"]) for event in off_events] != list(movement_input):
            raise MatchError("the fixed movement input script is incomplete or changed")
        for a, b, qa, qb in zip(off_events, on_events, off_queued, on_queued):
            numeric = (a["scheduled_seconds"], b["scheduled_seconds"],
                       a["dispatch_seconds"], b["dispatch_seconds"],
                       qa["pad_time_ms"], qb["pad_time_ms"])
            if any(type(value) not in (int, float) or not math.isfinite(value) for value in numeric):
                raise MatchError("input timing evidence is not finite")
            if (a["kind"] != "script" or b["kind"] != "script" or
                    a["input"] != b["input"] or a["input"] != qa["input"] or
                    b["input"] != qb["input"] or a["scheduled_seconds"] != b["scheduled_seconds"] or
                    abs(a["dispatch_seconds"] - b["dispatch_seconds"]) * 1000 > tolerance or
                    abs((qa["pad_time_ms"] - off_queued[0]["pad_time_ms"]) -
                        (qb["pad_time_ms"] - on_queued[0]["pad_time_ms"])) > tolerance):
                raise MatchError("actual input sequences or timing are not matched")
    except (KeyError, TypeError) as exc:
        raise MatchError(f"malformed input event evidence: {exc}") from exc


def _compare(path, movement_input):
    manifest = _read_json(path)
    reviewer = _text(manifest.get("reviewer"), "reviewer")
    state = _text(manifest.get("state"), "state")
    tolerance = manifest.get("input_tolerance_ms")
    if (type(tolerance) not in (int, float) or not math.isfinite(tolerance) or
            not 0 <= tolerance <= 500):
        raise MatchError("input_tolerance_ms must be finite and between 0 and 500")
    pairs = manifest.get("pairs")
    if not isinstance(pairs, list) or len(pairs) < 3:
        raise MatchError("at least three reviewed off/on pairs are required")
    entries, campaign, common = [], None, None
    run_numbers = []
    for pair in pairs:
        if not isinstance(pair, dict) or pair.get("matched") is not True:
            raise MatchError("each pair needs an affirmative matched-state review")
        evidence = {key: _text(pair.get(key), key)
                    for key in ("start_match", "end_match", "interval_match")}
        off_path = _path(path.parent, pair.get("off_result"), "off_result")
        on_path = _path(path.parent, pair.get("on_result"), "on_result")
        if off_path.parent.parent != on_path.parent.parent:
            raise MatchError("paired results are not from the same campaign")
        if campaign is None:
            campaign = off_path.parent.parent
        elif campaign != off_path.parent.parent:
            raise MatchError("all pairs must be from the same campaign")
        off, off_number = _run(off_path, "off", state)
        on, on_number = _run(on_path, "on", state)
        if abs(off_number - on_number) != 1:
            raise MatchError("off/on runs in each pair must be adjacent")
        run_numbers.extend([(off_number, "off"), (on_number, "on")])
        options = {
            tuple(sorted((key.upper(), value) for key, value in item["env"].items()
                         if key.upper() != "RECOMP_AC97_READY"))
            for item in (off, on)
        }
        identity = {(item.get("generation"), item.get("toolkit_tree"), item.get("threads"))
                    for item in (off, on)}
        if len(options) != 1 or len(identity) != 1 or any(value is None for value in next(iter(identity))):
            raise MatchError("generation, toolkit, threads, and non-audio options must match")
        if off["options"] != on["options"]:
            raise MatchError("enabled benchmark options differ between audio modes")
        current = (next(iter(identity)), next(iter(options)),
                   json.dumps(off["options"], sort_keys=True))
        if common is None:
            common = current
        elif current != common:
            raise MatchError("campaign settings changed across pairs")
        _matching_input(off, on, tolerance, movement_input)
        off_labels = _path(path.parent, pair.get("off_labels"), "off_labels")
        on_labels = _path(path.parent, pair.get("on_labels"), "on_labels")
        off_window = _frame_window(off_path, off, off_labels)
        on_window = _frame_window(on_path, on, on_labels)
        for key in ("region", "exclude", "dimensions", "tolerance"):
            if off_window[key] != on_window[key]:
                raise MatchError(f"reviewed image {key} differs between audio modes")
        entries.append({
            "off_result": str(off_path), "on_result": str(on_path),
            "off": off_window, "on": on_window, "state_evidence": evidence,
            "on_minus_off_flips_per_second": round(
                on_window["flips_per_second"] - off_window["flips_per_second"], 3),
            "on_minus_off_reviewed_world_changes_per_second": round(
                on_window["reviewed_world_changes_per_second"] -
                off_window["reviewed_world_changes_per_second"], 3),
        })
    ordered = sorted(run_numbers)
    if (len(set(number for number, _mode in ordered)) != len(ordered) or
            any(b[0] != a[0] + 1 or a[1] == b[1] for a, b in zip(ordered, ordered[1:]))):
        raise MatchError("runs must be unique and alternate audio off/on across the campaign")
    return {
        "status": "reviewer_asserted", "reviewer": reviewer, "state": state,
        "scope": ("Matched intervals, image alignment and presented content are human-reviewed "
                  "assertions. Flip rates are not world FPS; world-region changes are labeled, "
                  "not inferred from ROI pixels alone. Continuous capture affects timing."),
        "input_tolerance_ms": tolerance, "pairs": entries,
        "mean_on_minus_off_flips_per_second": round(statistics.mean(
            item["on_minus_off_flips_per_second"] for item in entries), 3),
        "mean_on_minus_off_reviewed_world_changes_per_second": round(statistics.mean(
            item["on_minus_off_reviewed_world_changes_per_second"] for item in entries), 3),
    }


def compare_manifest(path, movement_input):
    """Return no comparisons when any required match or capture evidence is missing."""
    try:
        return _compare(Path(path).resolve(), movement_input)
    except (MatchError, OSError, AttributeError, IndexError, KeyError, TypeError, ValueError) as exc:
        return {"status": "inconclusive", "reason": str(exc), "pairs": None}
