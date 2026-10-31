#!/usr/bin/env python3
"""Scripted tutorial benchmark for the performance plan (docs/PERFPLAN.md).

Each run boots the verified build with isolated saves. The default legacy menu
script and draw-count threshold are heuristics, not verified state signals.
For matched audio experiments use --readiness manual: an operator navigates
to the requested visible state and acknowledges each attempt. Timeouts still
apply. The executor's RECOMP_FRAME_STATS then helps find the measured windows:

* world: in legacy mode, the first WORLD_RUN consecutive flips with at least
  WORLD_DRAWS draws; manual mode starts after the operator's assertion;
* stationary: the tutorial's static hold after the world appears, found as
  HOLD_DETECT consecutive flips whose picture did not change. The hold is
  measured over STATIONARY_FRAMES rendered frames. Its length is a frame
  count, not a time (FINDINGS), so it is never measured as a time window;
* movement: once the hold has been measured, scripted live pad input
  (RECOMP_PAD_LIVE) dismisses the help panels and moves the character and
  camera; MOVEMENT_SECONDS from the first stick input are measured. Input
  starts then because the hold does not always end by itself.

Whole-picture hash changes and the vertex-constant fallback are diagnostics,
not distinct world frames. Known-black hashes identify only specific surfaces;
without reviewed images all other movement frames remain unknown. Use
--analyze-world on a separately reviewed local manifest (see bench_world.py)
to classify images and exclude HUD regions; raw ROI changes alone are not
world-frame FPS.

--capture-audio reserves a unique WAV for each audio-enabled attempt. A console
control event requests the runtime's WAV close handler; a killed process, an
absent close marker, or a stale/truncated WAV is a failed run. Mixed audio
variants require manual readiness and deterministic input. Their separately
reported results are not a comparison until content and input are matched.
--compare-matched checks a separately reviewed local manifest; its result
remains an operator assertion, not an automatically identified title state.

The profiler (BRUTEFORCE_PROFILE) and the GPU census (RECOMP_GPU_CENSUS) are
gated to the measured window of the chosen scene with flag files.

Results go to analysis/bench/<timestamp>/ with one JSON file per run and a
summary. Nothing here is uploaded anywhere.
"""

import argparse
import csv
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

import build
import bench_control
import bench_match
import bench_world

MENU_SCRIPT = "20000:a:200,23000:a:200,28000:a:200"
WORLD_DRAWS = 400
WORLD_RUN = 5
HOLD_DETECT = 20
STATIONARY_FRAMES = 120
MOVEMENT_SECONDS = 40.0
# Frame CSV flushes at least every 0.5 s of flipping. Drain it after an
# operator assertion so buffered pre-acknowledgement rows cannot start a hold.
READY_DRAIN_SECONDS = 1.0
# Live input after the hold, as (seconds after input starts, line).
MOVEMENT_INPUT = (
    (4.0, "a:200"), (7.0, "a:200"), (10.0, "a:200"), (13.0, "a:200"),
    (16.0, "lup:2500"), (19.0, "lup+rright:2000"), (22.0, "lup:2500"),
    (25.0, "rleft:1500"), (27.0, "lup+rleft:2500"), (30.0, "lup:3000"),
    (34.0, "lup+rright:3000"), (38.0, "lup:3000"), (42.0, "rleft:2000"),
    (45.0, "lup:3000"), (49.0, "lup+rright:3000"), (53.0, "lup:3000"),
)
MOVEMENT_START = 16.0           # measurement starts with the first stick input
# Help panels pause the tutorial until A is pressed; their timing depends on
# how far the character got. During movement, a picture unchanged for
# PANEL_FRAMES flips gets an A press, at most one per PANEL_SECONDS.
PANEL_FRAMES = 8
TRACE_AFTER = 10.0
CAPTURE_FLIPS = 2
CAPTURE_SETTLE_FLIPS = 2
MOVEMENT_CAPTURE_OFFSETS = (2.0, 5.0)
PANEL_SECONDS = 3.0
# Guest functions that poll for GPU progress (FINDINGS: the PFB flush poll and
# the fence wait). A game thread in them is waiting for the NV2A thread.
GUEST_GPU_WAITS = ("sub_00372180", "sub_00371CE0")
DEFAULT_WATCHDOG = 900
# The boot can hang intermittently before the menus (FINDINGS); such a run
# is recorded and retried rather than measured.
STALL_SECONDS = 60

# nv2a_frame_hash of a 640x480 four-byte black surface, with alpha 0 or 255.
KNOWN_BLACK_FRAME_HASHES = bench_world.KNOWN_BLACK_FRAME_HASHES


class BenchError(Exception):
    pass


def clean_environment(parent):
    """Do not inherit title/framework switches; audio defaults on afterward."""
    return {key: value for key, value in parent.items()
            if not key.upper().startswith(("RECOMP_", "BRUTEFORCE_"))}


def parse_env(spec):
    """'NAME:K=V,K=V' or 'K=V,K=V' -> (name, {K: V})."""
    name = None
    if ":" in spec.split("=", 1)[0]:
        name, spec = spec.split(":", 1)
    env = {}
    for item in filter(None, (part.strip() for part in spec.split(","))):
        if "=" not in item:
            raise BenchError(f"Expected K=V, got {item!r}")
        key, value = item.split("=", 1)
        env[key] = value
    return name or ",".join(f"{k}={v}" for k, v in env.items()) or "default", env


class FrameLog:
    """Incrementally reads the executor's per-flip CSV."""

    def __init__(self, path):
        self.path = Path(path)
        self.offset = 0
        self.pending = ""
        self.header = None
        self.rows = []

    def poll(self):
        if not self.path.is_file():
            return []
        with self.path.open("r", encoding="ascii", errors="replace") as stream:
            stream.seek(self.offset)
            data = stream.read()
            self.offset = stream.tell()
        text = self.pending + data
        lines = text.split("\n")
        self.pending = lines.pop()
        new = []
        for line in lines:
            if not line:
                continue
            fields = line.split(",")
            if self.header is None:
                self.header = fields
                continue
            row = dict(zip(self.header, fields))
            row = {
                "flip": int(row["flip"]), "t": int(row["t_us"]) / 1e6,
                "draws": int(row["draws"]), "vertices": int(row["program_vertices"]),
                "hash": row["hash"], "distinct": int(row["distinct"]),
                "constants": row.get("constants", ""),
            }
            new.append(row)
        self.rows.extend(new)
        return new


# Without pictures, a flip "changed" when its constant signature is none of
# the previous CONSTANT_WINDOW flips': a static scene that refreshes, say, a
# shadow map every fifth frame repeats a short cycle of signatures.
CONSTANT_WINDOW = 8


def changed(rows, i):
    """Did flip i change the picture? (value, source)."""
    row = rows[i]
    if row["distinct"] >= 0:
        return bool(row["distinct"]), "pixels"
    recent = {r["constants"] for r in rows[max(0, i - CONSTANT_WINDOW):i]}
    return row["constants"] not in recent, "constants"


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def frame_metrics(rows):
    """Rate and frame times over consecutive flips (rows in order)."""
    if len(rows) < 2:
        return None
    duration = rows[-1]["t"] - rows[0]["t"]
    intervals = [(b["t"] - a["t"]) * 1000 for a, b in zip(rows, rows[1:])]
    p99 = percentile(intervals, 0.99)
    return {
        "frames": len(rows) - 1,
        "unit": "flips",
        "seconds": round(duration, 3),
        "fps": round((len(rows) - 1) / duration, 3) if duration > 0 else None,
        "frame_ms_p50": round(percentile(intervals, 0.5), 2),
        "frame_ms_p90": round(percentile(intervals, 0.9), 2),
        "frame_ms_p99": round(p99, 2),
        "low_1pct_fps": round(1000 / p99, 3) if p99 else None,
    }


def known_black_metrics(rows):
    """Count matches to the known black signatures among frames with pixel hashes."""
    hashed = [row for row in rows if row["distinct"] >= 0]
    if not hashed:
        return None
    matched = sum(row["hash"] in KNOWN_BLACK_FRAME_HASHES for row in hashed)
    return {
        "scope": "640x480 four-byte solid black, alpha 0 or 255",
        "pixel_frames": len(hashed),
        "matched_frames": matched,
        "percent": round(100 * matched / len(hashed), 3),
    }


def distinct_metrics(rows, start, end):
    """Whole-picture changes or constant hints, never classified world FPS."""
    picked = []
    sources = set()
    for i in range(max(start, 1), end):
        is_changed, source = changed(rows, i)
        sources.add(source)
        if is_changed:
            picked.append(rows[i])
    window = rows[start:end]
    if len(window) < 2:
        return None
    duration = window[-1]["t"] - window[0]["t"]
    result = {
        "source": next(iter(sources)) if len(sources) == 1 else "mixed",
        "scope": "whole-frame pixel changes or vertex-constant hints, not world frames",
        "establishes_world_fps": False,
        "distinct_frames": len(picked),
        "distinct_fps": round(len(picked) / duration, 3) if duration > 0 else None,
    }
    if len(picked) >= 2:
        times = [(b["t"] - a["t"]) * 1000 for a, b in zip(picked, picked[1:])]
        p99 = percentile(times, 0.99)
        result.update({
            "distinct_ms_p50": round(percentile(times, 0.5), 2),
            "distinct_ms_p99": round(p99, 2),
            "distinct_low_1pct_fps": round(1000 / p99, 3) if p99 else None,
        })
    return result


def pixel_constant_agreement(rows, start, end):
    """How often the constants hint agrees with the pixel hash (CPU path)."""
    agree = total = 0
    for i in range(max(start, 1), end):
        if rows[i]["distinct"] < 0:
            return None
        total += 1
        recent = {r["constants"] for r in rows[max(0, i - CONSTANT_WINDOW):i]}
        agree += bool(rows[i]["distinct"]) == (rows[i]["constants"] not in recent)
    return {"agree": agree, "total": total} if total else None


def parse_profile(path):
    """Per-thread busy/wait from the profiler, plus guest GPU-poll shares."""
    path = Path(path)
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    threads = {}
    for line in text.splitlines():
        if line.startswith("THREAD,"):
            _, index, tid, samples, busy, wait, start = line.split(",", 6)
            threads[int(index)] = {
                "tid": int(tid), "samples": int(samples), "busy": int(busy),
                "kernel_wait": int(wait), "start": start,
            }
    blocks = re.split(r"\n(?=T\d+ +tid )", text.split("== All threads")[0])
    for block in blocks:
        match = re.match(r"T(\d+) ", block)
        if not match or int(match.group(1)) not in threads:
            continue
        info = threads[int(match.group(1))]
        functions = re.findall(r"^\s+([\d.]+)%\s+(\S+)$", block, re.MULTILINE)
        info["top"] = [(name, float(share)) for share, name in functions[:12]]
        info["guest_gpu_wait_pct"] = round(sum(
            float(share) for share, name in functions
            if any(name.endswith("!" + wait) for wait in GUEST_GPU_WAITS)), 2)
    header = text.splitlines()[0] if text else ""
    summary = {"header": header, "threads": threads}
    game = [t for t in threads.values() if t["start"].endswith("!mainCRTStartup")]
    nv2a = [t for t in threads.values() if t["start"].endswith("!nv2a_ack_thread")]
    raster = [t for t in threads.values() if t["start"].endswith("!raster_worker")]
    if game:
        g = game[0]
        kernel = 100.0 * g["kernel_wait"] / g["samples"]
        summary["game_thread"] = {
            "busy_pct": round(100.0 * g["busy"] / g["samples"], 2),
            "kernel_wait_pct": round(kernel, 2),
            "guest_gpu_poll_pct": g.get("guest_gpu_wait_pct", 0.0),
            # G3: kernel waits plus the known guest polls on GPU progress.
            "gpu_wait_pct": round(kernel + g.get("guest_gpu_wait_pct", 0.0), 2),
            "top": g.get("top", [])[:6],
        }
    if nv2a:
        n = nv2a[0]
        summary["nv2a_thread"] = {
            "busy_pct": round(100.0 * n["busy"] / n["samples"], 2),
            "top": n.get("top", [])[:10],
        }
    if raster:
        summary["raster_workers"] = {
            "count": len(raster),
            "mean_busy_pct": round(statistics.mean(
                100.0 * t["busy"] / t["samples"] for t in raster), 2),
        }
    return summary


def parse_census(path):
    path = Path(path)
    if not path.is_file():
        return None
    lines = path.read_text(encoding="ascii", errors="replace").splitlines()
    result = {"header": lines[0] if lines else "", "categories": {}}
    for row in csv.DictReader(lines[1:]):
        result["categories"].setdefault(row["category"], []).append(
            (row["key"], int(row["count"]), int(row["vertices"])))
    return result


class Gate:
    def __init__(self, path):
        self.path = Path(path)
        self.opened_at = self.closed_at = None

    def open(self, now):
        if self.opened_at is None:
            self.path.write_text("open\n", encoding="ascii")
            self.opened_at = now

    def close(self, now):
        if self.opened_at is not None and self.closed_at is None:
            self.path.unlink(missing_ok=True)
            self.closed_at = now


class FlipCapture:
    def __init__(self, flag, prefix):
        self.flag = Path(flag)
        self.prefix = Path(prefix)
        self.start_count = 0

    def open(self):
        self.start_count = len(self.files())
        self.flag.write_text("capture\n", encoding="ascii")

    def files(self):
        return sorted(
            path for path in self.prefix.parent.glob(f"{self.prefix.name}*.bmp")
            if path.stem[len(self.prefix.name):].isdigit()
        )

    def poll(self):
        if self.flag.exists() and len(self.files()) - self.start_count >= CAPTURE_FLIPS:
            self.close()
            return True
        return False

    def close(self):
        self.flag.unlink(missing_ok=True)


def configure_flip_capture(env, out_dir, census, tagged=False):
    capture_dir = out_dir / "capture"
    capture_dir.mkdir()
    prefix = capture_dir / "hold"
    flag = out_dir / "capture.flag"
    env["RECOMP_FB_DUMP"] = str(prefix)
    env["RECOMP_FB_DUMP_FLIPS"] = str(flag)
    env["RECOMP_FB_DUMP_FLIPS_ONLY"] = "1"
    if tagged:
        env["RECOMP_FB_DUMP_FLIP_ID"] = "1"
    if not census:
        # Census snapshots are written by the periodic report.
        env["RECOMP_PB_REPORT_MS"] = "600000"
    return FlipCapture(flag, prefix)


def run_once(out_dir, label, scene, env_overrides, threads, watchdog, profile, census,
             config, dump=False, trace_frame=False, capture=False, capture_movement=False,
             trace_after=TRACE_AFTER, readiness="legacy", ready_timeout=600.0,
             ready_state=None, no_auto_panels=False, capture_audio=False, phase_timeout=240.0,
             capture_world=False):
    overridden = {key.upper() for key in env_overrides}
    if "RECOMP_AUDIO_WAV" in overridden:
        raise BenchError("RECOMP_AUDIO_WAV cannot be supplied by a variant; use --capture-audio")
    if capture_audio and "RECOMP_AUDIO_WAV_SECS" in overridden:
        raise BenchError("--capture-audio cannot be combined with RECOMP_AUDIO_WAV_SECS")
    if readiness == "manual" and overridden.intersection({
            "RECOMP_PAD_SCRIPT", "RECOMP_PAD_LIVE", "RECOMP_FRAME_STATS",
            "RECOMP_WATCHDOG_SECS"}):
        raise BenchError("manual readiness owns pad input, frame statistics, and watchdog paths")
    if (capture or capture_movement or capture_world) and overridden.intersection({
            "RECOMP_FB_DUMP", "RECOMP_FB_DUMP_FLIPS", "RECOMP_FB_DUMP_FLIPS_ONLY",
            "RECOMP_FB_DUMP_FLIP_ID"}):
        raise BenchError("flip capture owns the framebuffer dump paths and gate")
    out_dir = Path(out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=False)
    save = out_dir / "save"
    save.mkdir()
    frames_path = out_dir / "frames.csv"
    live_path = out_dir / "pad_live.txt"
    live_path.write_text("", encoding="ascii")
    profile_gate = Gate(out_dir / "profile.gate")
    census_gate = Gate(out_dir / "census.gate")
    env = clean_environment(os.environ)
    env.update({
        "RECOMP_PAD_SCRIPT": MENU_SCRIPT,
        "RECOMP_PAD_LIVE": str(live_path),
        "RECOMP_FRAME_STATS": str(frames_path),
        "RECOMP_WATCHDOG_SECS": str(watchdog),
        "RECOMP_RASTER_THREADS": str(threads),
        "RECOMP_PROGRAM_NEAR_ZERO": "1",
        "RECOMP_PB_REPORT_MS": "5000",
    })
    if profile:
        env["BRUTEFORCE_PROFILE"] = str(out_dir / "profile.txt")
        env["BRUTEFORCE_PROFILE_GATE"] = str(profile_gate.path)
    if census:
        env["RECOMP_GPU_CENSUS"] = str(out_dir / "census.csv")
        env["RECOMP_GPU_CENSUS_GATE"] = str(census_gate.path)
    if dump and not (capture or capture_movement or capture_world):
        (out_dir / "dumps").mkdir()
        env["RECOMP_FB_DUMP"] = str(out_dir / "dumps" / "f")
    trace_flag = out_dir / "trace.flag"
    capture_control = None
    if capture or capture_movement or capture_world:
        # RECOMP_FB_DUMP_FLIPS dumps every flip while the flag exists; it is
        # created for CAPTURE_FLIPS flips once the hold is being measured, so
        # renderers can be compared on the same static picture (G4).
        capture_control = configure_flip_capture(env, out_dir, census, tagged=capture_world)
    if trace_frame:
        # RECOMP_FRAME_TRACE logs every batch of one frame once the flag
        # exists; it is created TRACE_AFTER seconds into the movement window.
        env["RECOMP_FRAME_TRACE"] = str(trace_flag)
    env.update(env_overrides)
    if readiness == "manual":
        env.pop("RECOMP_PAD_SCRIPT", None)
    wav_path = bench_control.reserve_audio_capture(out_dir, env) if capture_audio else None
    ready = (bench_control.ManualReadiness(out_dir, ready_state, ready_timeout)
             if readiness == "manual" else None)
    command, cwd, env = build.prepare_game_run(
        config, experimental_tutorial=True, isolated_saves=save, base_env=env
    )
    stdout = (out_dir / "stdout.txt").open("w", encoding="utf-8")
    stderr_path = out_dir / "stderr.txt"
    stderr = stderr_path.open("w", encoding="utf-8")
    started = time.monotonic()
    if ready:
        print(f"[bench] {ready.instruction()}", flush=True)
    creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
    try:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                   stdin=subprocess.DEVNULL, creationflags=creationflags)
    except OSError:
        stdout.close()
        stderr.close()
        raise
    log = FrameLog(frames_path)
    state = "manual_ready" if ready else "world"
    phase = state
    phase_started = 0.0
    marks = {}
    rows = log.rows
    world_run = hold_run = 0
    last_flip_at = 0.0
    last_panel_press = -1e9
    panel_presses = 0
    sent = 0
    input_events = []
    movement_captures = []
    movement_capture_start = None
    world_capture_start = None
    ready_drain_until = None
    result = {
        "label": label, "scene": scene, "env": env_overrides, "threads": threads,
        "generation": cwd.parents[1].name, "toolkit_tree": build.TOOLKIT_TREE,
        "boot_dir": str(cwd), "out_dir": str(out_dir),
        "options": {
            "config": config, "watchdog_seconds": watchdog,
            "profile": bool(profile), "census": bool(census),
            "periodic_dump": bool(dump), "trace_frame": bool(trace_frame),
            "trace_after_seconds": trace_after if trace_frame else None,
            "capture_stationary": bool(capture), "capture_movement": bool(capture_movement),
            "capture_world": bool(capture_world), "capture_audio": bool(capture_audio),
            "readiness": readiness, "ready_timeout_seconds": ready_timeout if ready else None,
            "phase_timeout_seconds": phase_timeout if ready else None,
            "automatic_panel_presses": not no_auto_panels,
        },
        "readiness": {"mode": readiness, "state": ready_state,
                      "verification": "operator assertion" if ready else "legacy heuristic"},
        "audio": {"mode": "on" if bench_control.audio_enabled(env) else "off",
                  "capture_requested": bool(wav_path),
                  "wav_path": str(wav_path) if wav_path else None},
        "automatic_panel_presses": not no_auto_panels,
    }

    def send_script_inputs(elapsed, flip):
        nonlocal sent
        while sent < len(MOVEMENT_INPUT) and elapsed >= MOVEMENT_INPUT[sent][0]:
            with live_path.open("a", encoding="ascii") as stream:
                stream.write(MOVEMENT_INPUT[sent][1] + "\n")
            input_events.append({
                "kind": "script", "input": MOVEMENT_INPUT[sent][1],
                "scheduled_seconds": MOVEMENT_INPUT[sent][0],
                "dispatch_seconds": round(elapsed, 3),
                "dispatch_wall_seconds": round(time.monotonic() - started, 3), "flip": flip,
            })
            sent += 1

    try:
        while True:
            exited = process.poll() is not None
            new = log.poll()
            now = time.monotonic() - started
            base = len(rows) - len(new)
            for offset, row in enumerate(new):
                i = base + offset
                if state == "ready_frames":
                    marks["world"] = i
                    state = "hold"
                elif state == "world":
                    world_run = world_run + 1 if row["draws"] >= WORLD_DRAWS else 0
                    if world_run >= WORLD_RUN:
                        marks["world"] = i - WORLD_RUN + 1
                        state = "hold"
                elif state == "hold":
                    is_changed, source = changed(rows, i)
                    hold_run = 0 if is_changed else hold_run + 1
                    if hold_run >= HOLD_DETECT:
                        marks["hold"] = i - HOLD_DETECT + 1
                        result["change_source"] = source
                        if capture:
                            capture_control.open()
                            marks["capture_start"] = len(rows) - 1
                            state = "capture"
                        else:
                            marks["stationary_start"] = i
                            state = "stationary"
                            if scene == "stationary":
                                profile_gate.open(now)
                                census_gate.open(now)
                elif state == "capture":
                    if ready and changed(rows, i)[0]:
                        result["error"] = "operator-confirmed hold changed during capture"
                        state = "failed"
                        break
                    if capture_control.poll():
                        marks["capture_end"] = len(rows) - 1
                        state = "capture-settle"
                    elif i - marks["capture_start"] >= STATIONARY_FRAMES:
                        result["error"] = "flip capture did not produce two images"
                        state = "failed"
                        break
                elif state == "capture-settle":
                    if ready and changed(rows, i)[0]:
                        result["error"] = "operator-confirmed hold changed during capture settling"
                        state = "failed"
                        break
                    if i - marks["capture_end"] >= CAPTURE_SETTLE_FLIPS:
                        marks["stationary_start"] = i
                        state = "stationary"
                        if scene == "stationary":
                            profile_gate.open(now)
                            census_gate.open(now)
                elif state == "stationary":
                    is_changed, _source = changed(rows, i)
                    done = i - marks["stationary_start"] >= STATIONARY_FRAMES
                    if ready and is_changed:
                        result["error"] = "operator-confirmed hold changed before stationary measurement completed"
                        state = "failed"
                        break
                    if is_changed or done:
                        marks["stationary_end"] = i if done else i - 1
                        marks["stationary_complete"] = done
                        if scene == "stationary":
                            profile_gate.close(now)
                            census_gate.close(now)
                            state = "finishing"
                        else:
                            # The hold does not end by itself in every run
                            # (FINDINGS): input starts once it is measured.
                            state = "moving"
                            marks["input_start"] = i
                            marks["input_start_t"] = rows[i]["t"]
                elif state == "moving":
                    elapsed = row["t"] - marks["input_start_t"]
                    if ready:
                        send_script_inputs(elapsed, row["flip"])
                    if elapsed >= MOVEMENT_START and "movement_start" not in marks:
                        marks["movement_start"] = i
                        profile_gate.open(now)
                        census_gate.open(now)
                        if capture_world:
                            world_capture_start = len(capture_control.files())
                            capture_control.open()
                    if capture_movement and len(movement_captures) < len(MOVEMENT_CAPTURE_OFFSETS):
                        if (movement_capture_start is None and
                                elapsed >= MOVEMENT_START + MOVEMENT_CAPTURE_OFFSETS[len(movement_captures)]):
                            movement_capture_start = len(capture_control.files())
                            capture_control.open()
                            marks[f"movement_capture_{len(movement_captures)}_start"] = i
                        if movement_capture_start is not None and capture_control.poll():
                            movement_captures.append({
                                "offset_seconds": MOVEMENT_CAPTURE_OFFSETS[len(movement_captures)],
                                "files": [path.name for path in
                                          capture_control.files()[movement_capture_start:]],
                            })
                            marks[f"movement_capture_{len(movement_captures) - 1}_end"] = i
                            movement_capture_start = None
                    if (trace_frame and elapsed >= MOVEMENT_START + trace_after
                            and not trace_flag.exists()):
                        trace_flag.write_text("trace\n", encoding="ascii")
                    if elapsed >= MOVEMENT_START + MOVEMENT_SECONDS:
                        marks["movement_end"] = i
                        profile_gate.close(now)
                        census_gate.close(now)
                        if capture_world:
                            capture_control.close()
                        state = "finishing"
            if state == "failed":
                break
            if state == "manual_ready" and ready.poll(now):
                result["readiness"].update({"acknowledged": True, "wall_seconds": round(now, 3),
                                             "last_observed_flip": rows[-1]["flip"] if rows else None,
                                             "csv_drain_seconds": READY_DRAIN_SECONDS})
                ready_drain_until = now + READY_DRAIN_SECONDS
                state = "ready_drain"
            elif state == "ready_drain" and now >= ready_drain_until:
                state = "ready_frames"
            if state == "moving" and rows:
                elapsed = rows[-1]["t"] - marks["input_start_t"]
                if not ready:
                    send_script_inputs(elapsed, rows[-1]["flip"])
                if (not no_auto_panels and len(rows) > PANEL_FRAMES
                        and rows[-1]["t"] - last_panel_press >= PANEL_SECONDS
                        and not any(changed(rows, k)[0]
                                    for k in range(len(rows) - PANEL_FRAMES, len(rows)))):
                    with live_path.open("a", encoding="ascii") as stream:
                        stream.write("a:200\n")
                    input_events.append({
                        "kind": "automatic_panel", "input": "a:200",
                        "dispatch_seconds": round(elapsed, 3),
                        "dispatch_wall_seconds": round(time.monotonic() - started, 3),
                        "flip": rows[-1]["flip"],
                    })
                    last_panel_press = rows[-1]["t"]
                    panel_presses += 1
            if state == "finishing":
                if process.poll() is not None:
                    result["error"] = f"process exited with code {process.returncode} during finishing"
                    break
                if capture_movement and len(movement_captures) != len(MOVEMENT_CAPTURE_OFFSETS):
                    result["error"] = "movement flip captures did not produce two images per window"
                    break
                if ready and scene == "movement" and sent != len(MOVEMENT_INPUT):
                    result["error"] = "fixed movement input script did not finish"
                    break
                if wait_for_outputs(out_dir, stderr_path, profile, census, process):
                    break
                result["error"] = "profile or census output was not written"
                break
            if exited:
                result["exit_code"] = process.returncode
                result["error"] = f"process exited with code {process.returncode} during {state}"
                break
            if state != phase:
                phase, phase_started = state, now
            elif ready and state not in ("manual_ready", "finishing") and now - phase_started > phase_timeout:
                result["error"] = f"timed out after {phase_timeout:g} s during {state}"
                break
            if (not frames_path.is_file() and state != "manual_ready" and
                    now - (phase_started if ready else 0.0) > 90):
                result["error"] = "no frame statistics after 90 s"
                break
            if new:
                last_flip_at = now
            elif state != "manual_ready" and rows and now - last_flip_at > STALL_SECONDS:
                result["error"] = f"stalled: no flip for {STALL_SECONDS} s after flip {len(rows)}"
                break
            time.sleep(0.1)
    except bench_control.BenchControlError as exc:
        result["error"] = str(exc)
    finally:
        profile_gate.close(time.monotonic() - started)
        census_gate.close(time.monotonic() - started)
        if capture_control:
            capture_control.close()
        shutdown = bench_control.shutdown_process(process)
        result["shutdown"] = shutdown
        if shutdown["error"] or shutdown["forced"]:
            result["error"] = result.get("error") or shutdown["error"] or "process was forcibly terminated"
        stdout.close()
        stderr.close()
        log.poll()
    result["state"] = state
    result["panel_presses"] = panel_presses
    result["input_events"] = input_events
    result["marks"] = marks
    result["wall_seconds"] = round(time.monotonic() - started, 1)
    result["flips"] = len(rows)
    if ready and scene == "movement" and state == "finishing":
        log_text = stderr_path.read_text(encoding="utf-8", errors="replace")
        queued = [{"input": name, "pad_time_ms": int(when)} for name, when in re.findall(
            r'PAD: live "([^"]+)" at t=(\d+) ms', log_text)]
        result["input_runtime"] = {
            "queued": queued,
            "scope": "runtime queue acknowledgements, not proof of guest response or matched state",
        }
        expected = [event["input"] for event in input_events]
        if [event["input"] for event in queued] != expected:
            result["error"] = result.get("error") or (
                "runtime did not acknowledge the complete live-input sequence; inspect stderr.txt")
    if wav_path:
        try:
            result["audio"]["wav"] = bench_control.validate_wav_capture(wav_path, stderr_path)
        except bench_control.BenchControlError as exc:
            result["audio"]["error"] = str(exc)
            result["error"] = result.get("error") or str(exc)
    if capture_control:
        result["captures"] = [path.name for path in capture_control.files()]
    if capture_movement:
        result["movement_captures"] = movement_captures
    if capture_world:
        files = (capture_control.files()[world_capture_start:]
                 if world_capture_start is not None else [])
        result["world_capture"] = {
            "files": [path.name for path in files],
            "alignment": bench_world.FLIP_ID_ALIGNMENT,
            "source": "guest framebuffer at flip, not a verified presented image",
            "performance": "continuous framebuffer dumping perturbs the measured run",
        }
        if len(files) < 2:
            result["error"] = result.get("error") or "continuous movement capture produced fewer than two images"
    if trace_frame:
        trace_log = stderr_path.read_text(encoding="utf-8", errors="replace")
        if "[FTRACE] frame begins" not in trace_log or "[FTRACE] end of frame" not in trace_log:
            result["error"] = result.get("error") or "requested frame trace did not complete"
    if "world" in marks:
        result["world_t"] = round(rows[marks["world"]]["t"] - rows[0]["t"], 2)
    if "stationary_start" in marks and "stationary_end" in marks:
        a, b = marks["stationary_start"], marks["stationary_end"] + 1
        result["stationary"] = frame_metrics(rows[a:b])
        if result["stationary"] is not None:
            result["stationary"]["complete"] = marks.get("stationary_complete", False)
            result["stationary"]["draws_per_frame"] = round(
                statistics.mean(r["draws"] for r in rows[a:b]), 1)
            result["stationary"]["vertices_per_frame"] = round(
                statistics.mean(r["vertices"] for r in rows[a:b]), 1)
        result["stationary_distinct"] = distinct_metrics(rows, a, b)
        result["stationary_known_black"] = known_black_metrics(rows[a:b])
        result["stationary_agreement"] = pixel_constant_agreement(rows, a, b)
        result["hold_frames_before_measure"] = a - marks["hold"]
    if "movement_start" in marks and "movement_end" in marks:
        a, b = marks["movement_start"], marks["movement_end"] + 1
        result["movement"] = frame_metrics(rows[a:b])
        result["movement_distinct"] = distinct_metrics(rows, a, b)
        result["movement_known_black"] = known_black_metrics(rows[a:b])
        result["movement_agreement"] = pixel_constant_agreement(rows, a, b)
        result["movement_world"] = bench_world.unreviewed_metrics(rows[a:b])
    if profile:
        result["profile"] = parse_profile(out_dir / "profile.txt")
    if census:
        result["census"] = parse_census(out_dir / "census.csv")
    build.write_json(out_dir / "result.json", result)
    return result


def wait_for_outputs(out_dir, stderr_path, profile, census, process, timeout=120):
    """After the gates close: the profiler writes once, the census at reports."""
    deadline = time.monotonic() + timeout
    closed = time.time()
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return False
        ok = True
        if profile:
            text = stderr_path.read_text(encoding="utf-8", errors="replace")
            ok &= "[PROFILE] written" in text
        if census:
            path = Path(out_dir) / "census.csv"
            ok &= path.is_file() and path.stat().st_mtime > closed
        if ok:
            return True
        time.sleep(0.5)
    return False


def summarize(results):
    """Mean, min, max per label for the headline numbers."""
    def stats(values):
        values = [v for v in values if v is not None]
        if not values:
            return None
        return {"mean": round(statistics.mean(values), 3), "min": round(min(values), 3),
                "max": round(max(values), 3), "n": len(values)}

    def get(result, *path):
        for key in path:
            if not isinstance(result, dict) or result.get(key) is None:
                return None
            result = result[key]
        return result

    labels = []
    for result in results:
        if result["label"] not in labels:
            labels.append(result["label"])
    table = {}
    for label in labels:
        selected = [r for r in results if r["label"] == label]
        runs = [r for r in selected if not r.get("error")]
        table[label] = {
            "runs": len(runs),
            "failed": len(selected) - len(runs),
            "failed_attempts": sum(r.get("attempts", 1) - 1 + bool(r.get("error"))
                                   for r in selected),
            "stationary_fps": stats([get(r, "stationary", "fps") for r in runs]),
            "stationary_low_1pct_fps": stats([get(r, "stationary", "low_1pct_fps") for r in runs]),
            "stationary_distinct_fps": stats([get(r, "stationary_distinct", "distinct_fps")
                                              for r in runs]),
            "movement_fps": stats([get(r, "movement", "fps") for r in runs]),
            "movement_distinct_fps": stats([get(r, "movement_distinct", "distinct_fps")
                                            for r in runs]),
            "movement_distinct_low_1pct_fps": stats([
                get(r, "movement_distinct", "distinct_low_1pct_fps") for r in runs]),
            "movement_known_black_pct": stats([
                get(r, "movement_known_black", "percent") for r in runs]),
            "game_gpu_wait_pct": stats([get(r, "profile", "game_thread", "gpu_wait_pct")
                                        for r in runs]),
            "nv2a_busy_pct": stats([get(r, "profile", "nv2a_thread", "busy_pct") for r in runs]),
            "world_t": stats([r.get("world_t") for r in runs]),
        }
    return table


def print_table(table):
    print("\n[bench] fps fields below count flips or whole-frame/constant changes; "
          "none establishes distinct world FPS.")
    for label, row in table.items():
        print(f"\n{label}  ({row['runs']} completed, {row['failed']} failed runs, "
              f"{row['failed_attempts']} failed attempts)")
        for key, value in row.items():
            if key in ("runs", "failed", "failed_attempts") or value is None:
                continue
            print(f"  {key:32} mean {value['mean']:>9}  min {value['min']:>9}  "
                  f"max {value['max']:>9}  n {value['n']}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", choices=("stationary", "movement"), default="stationary")
    parser.add_argument("--variant", action="append", default=[],
                        help="NAME:K=V,K=V environment for one variant; repeat for A/B "
                             "(runs alternate A, B, A, B, ...)")
    parser.add_argument("--runs", type=int, default=3, help="runs per variant")
    parser.add_argument("--threads", type=int, default=12, help="RECOMP_RASTER_THREADS")
    parser.add_argument("--watchdog", type=int, default=DEFAULT_WATCHDOG)
    parser.add_argument("--no-profile", action="store_true")
    parser.add_argument("--retries", type=int, default=1,
                        help="retries of a run that stalls or never starts")
    parser.add_argument("--census", action="store_true")
    parser.add_argument("--dump", action="store_true",
                        help="write a frame at each periodic report (RECOMP_FB_DUMP)")
    parser.add_argument("--capture", action="store_true",
                        help="dump flips at the start of the stationary hold, for comparing "
                             "renderers (tools/compare_frames.py)")
    parser.add_argument("--capture-movement", action="store_true",
                        help="dump flips at two scripted movement points (diagnostic; may affect fps)")
    parser.add_argument("--capture-world", action="store_true",
                        help="dump movement flips with frame-statistics IDs for later review "
                             "(large, affects timing, and does not prove presented content)")
    parser.add_argument("--analyze-world", type=Path, metavar="LABELS.json",
                        help="analyze a local reviewed image manifest without starting the game; "
                             "schema in tools\\bench_world.py")
    parser.add_argument("--compare-matched", type=Path, metavar="MATCHES.json",
                        help="compare only reviewer-matched, fully labeled captured intervals; "
                             "schema in tools\\bench_match.py")
    parser.add_argument("--readiness", choices=("legacy", "manual"), default="legacy",
                        help="legacy timed menu script, or operator-confirmed visible state")
    parser.add_argument("--ready-state", help="precise visible state the operator must confirm")
    parser.add_argument("--ready-timeout", type=float, default=600.0,
                        help="seconds allowed for manual readiness (default: 600)")
    parser.add_argument("--phase-timeout", type=float, default=240.0,
                        help="seconds per subsequent manual-run phase (default: 240)")
    parser.add_argument("--no-auto-panels", action="store_true",
                        help="disable adaptive A presses; retain only the fixed movement script")
    parser.add_argument("--capture-audio", action="store_true",
                        help="reserve and validate a unique WAV for each audio-on attempt")
    parser.add_argument("--trace-frame", action="store_true",
                        help="log every batch of one frame during movement (RECOMP_FRAME_TRACE)")
    parser.add_argument("--trace-after", type=float, default=TRACE_AFTER,
                        help="seconds into movement before tracing a frame (default: 10)")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--out", type=Path, help="results directory (default analysis/bench/<time>)")
    args = parser.parse_args(argv)
    try:
        if args.analyze_world and args.compare_matched:
            raise BenchError("--analyze-world and --compare-matched are separate offline modes")
        if args.analyze_world:
            print(json.dumps(bench_world.analyze_manifest(args.analyze_world), indent=2))
            return 0
        if args.compare_matched:
            comparison = bench_match.compare_manifest(args.compare_matched, MOVEMENT_INPUT)
            print(json.dumps(comparison, indent=2))
            return 0 if comparison["status"] == "reviewer_asserted" else 1
        variants = [parse_env(spec) for spec in (args.variant or ["cpu:"])]
        if args.capture_movement and args.scene != "movement":
            raise BenchError("--capture-movement requires --scene movement")
        if args.capture_world and args.scene != "movement":
            raise BenchError("--capture-world requires --scene movement")
        if args.capture_world and args.capture_movement:
            raise BenchError("--capture-world and --capture-movement cannot share the flip gate")
        if args.trace_frame and args.scene != "movement":
            raise BenchError("--trace-frame requires --scene movement")
        if not math.isfinite(args.trace_after) or not 0 <= args.trace_after < MOVEMENT_SECONDS:
            raise BenchError("--trace-after must be within the movement window")
        if (not math.isfinite(args.ready_timeout) or args.ready_timeout <= 0 or
                not math.isfinite(args.phase_timeout) or args.phase_timeout <= 0):
            raise BenchError("--ready-timeout and --phase-timeout must be finite and positive")
        if args.runs < 1 or args.retries < 0 or args.threads < 1 or args.watchdog < 1:
            raise BenchError("--runs, --threads and --watchdog must be positive; --retries cannot be negative")
        if args.readiness == "manual" and not (args.ready_state and args.ready_state.strip()):
            raise BenchError("--readiness manual requires a precise --ready-state")
        if args.capture_audio and args.readiness != "manual":
            raise BenchError("--capture-audio requires --readiness manual")
        if len({name for name, _ in variants}) != len(variants):
            raise BenchError("Variant names must be distinct")
        for _, env in variants:
            for key, value in env.items():
                if key.upper() == "RECOMP_AC97_READY" and value.strip() in ("", "0"):
                    message = ("legacy RECOMP_AC97_READY is enabled by presence, even for 0; "
                               "use RECOMP_AUDIO=0 for audio off")
                    if args.readiness == "manual":
                        raise BenchError(message)
                    print(f"[bench] warning: {message}", file=sys.stderr, flush=True)
                if key.upper() == "RECOMP_AUDIO_WAV":
                    raise BenchError("RECOMP_AUDIO_WAV cannot be supplied by a variant; use --capture-audio")
        audio_modes = {name: "on" if bench_control.audio_enabled(env) else "off"
                       for name, env in variants}
        mixed_audio = len(set(audio_modes.values())) > 1
        if args.capture_audio and "on" not in audio_modes.values():
            raise BenchError("--capture-audio needs an audio-on variant (RECOMP_AUDIO=1)")
        if mixed_audio:
            if args.readiness != "manual" or not args.no_auto_panels:
                raise BenchError("audio-on/off runs require --readiness manual and --no-auto-panels")
            if not args.capture_audio:
                raise BenchError("audio-on/off runs require --capture-audio for each audio-on attempt")
            if args.runs < 3:
                raise BenchError("audio-on/off runs require at least three runs per variant")
            other_options = {
                tuple(sorted((key.upper(), value) for key, value in env.items()
                             if key.upper() not in ("RECOMP_AUDIO", "RECOMP_AC97_READY")))
                for _, env in variants
            }
            if len(other_options) != 1:
                raise BenchError("audio-on/off variants must otherwise use identical environment options")
            if args.scene == "movement" and not (args.capture_movement or args.capture_world):
                raise BenchError("audio-on/off movement runs require --capture-movement or --capture-world "
                                 "for later state review")
        out = args.out or build.ANALYSIS_DIR / "bench" / time.strftime("%Y%m%d-%H%M%S")
        out.mkdir(parents=True, exist_ok=False)
        results = []
        generation = None
        sequence = [v for _ in range(args.runs) for v in variants]
        if mixed_audio:
            print("[bench] audio-on/off runs are collection only; do not compare rates until "
                  "the complete movement states and actual input are independently matched.",
                  flush=True)
        for number, (name, env) in enumerate(sequence, 1):
            run_dir = out / f"run{number:02d}-{re.sub(r'[^A-Za-z0-9_.-]', '_', name)}"
            print(f"[bench] run {number}/{len(sequence)}: {name} -> {run_dir}", flush=True)
            attempt_errors = []
            for attempt in range(args.retries + 1):
                attempt_dir = run_dir if attempt == 0 else run_dir.with_name(
                    f"{run_dir.name}-retry{attempt}")
                result = run_once(attempt_dir, name, args.scene, env, args.threads,
                                  args.watchdog, not args.no_profile, args.census, args.config,
                                  args.dump, args.trace_frame, args.capture, args.capture_movement,
                                  args.trace_after, args.readiness, args.ready_timeout,
                                  args.ready_state, args.no_auto_panels, args.capture_audio,
                                  args.phase_timeout, args.capture_world)
                run_generation = result.get("generation")
                if not result.get("error") and run_generation:
                    if generation is not None and run_generation != generation:
                        result["error"] = "game generation changed during the benchmark campaign"
                    else:
                        generation = run_generation
                if not result.get("error"):
                    break
                attempt_errors.append({"attempt": attempt + 1, "error": result["error"],
                                       "out_dir": str(attempt_dir)})
                print(f"[bench]   {result['error']}", flush=True)
            result["attempts"] = attempt + 1
            result["attempt_errors"] = attempt_errors
            build.write_json(attempt_dir / "result.json", result)
            results.append(result)
            stationary = result.get("stationary") or {}
            movement = result.get("movement_distinct") or {}
            print(f"[bench]   state {result['state']}, world at {result.get('world_t')} s, "
                  f"stationary {stationary.get('fps')} flips/s, "
                  f"movement whole-frame/constant changes {movement.get('distinct_fps')}/s",
                  flush=True)
            world = result.get("movement_world")
            if world:
                print(f"[bench]   world-frame rate inconclusive: {world['known_black_frames']} "
                      f"known black, {world['unknown_frames']} unknown; no reviewed labels",
                      flush=True)
            black = result.get("movement_known_black")
            if black and black["matched_frames"]:
                print(f"[bench]   warning: {black['percent']}% of hashed movement frames match "
                      "known black signatures; distinct fps does not establish world fps",
                      file=sys.stderr, flush=True)
            table = summarize(results)
            build.write_json(out / "summary.json", {
                "scene": args.scene, "runs_per_variant": args.runs, "threads": args.threads,
                "sequence": [name for name, _ in sequence], "table": table,
                "generation": generation, "audio_modes": audio_modes,
                "options": result.get("options"),
                "variant_options": {name: env for name, env in variants},
                "metric_definitions": {
                    "stationary_fps": "flip rate",
                    "movement_fps": "flip rate, not world FPS",
                    "movement_distinct_fps": "whole-frame pixel-change rate or constant hint, not world FPS",
                },
                "audio_comparison": {
                    "status": "not_performed",
                    "reason": "visible states and actual input must be content-matched before comparison",
                    "review_mode": "python tools\\bench.py --compare-matched MATCHES.json",
                } if mixed_audio else None,
            })
            if result.get("error"):
                print(f"[bench] stopping after failed run: {result['error']}", file=sys.stderr)
                return 1
        print_table(summarize(results))
        print(f"\n[bench] results: {out}")
    except (BenchError, build.BuildError, bench_control.BenchControlError,
            bench_world.WorldLabelError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
