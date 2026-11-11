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
without reviewed images even those black frames have an unknown cause. Use
--analyze-world on a separately reviewed local manifest (see bench_world.py)
to classify images and exclude HUD regions; raw ROI changes alone are not
world-frame FPS. A collected but unreviewed movement campaign, or an
inconclusive offline world analysis, exits with status 2.

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
import bench_performance
import bench_world

MENU_SCRIPT = (
    "20000:a:200,23000:a:200,28000:a:200,32000:a:200,"
    "36000:down:200,37000:a:200,42000:a:200,47000:a:200,"
    "52000:a:200,57000:a:200,62000:a:200"
)
WORLD_DRAWS = 400
WORLD_RUN = 5
# The campaign menus can also exceed WORLD_DRAWS. The verified Quick Campaign
# route finishes its last scripted press at 62 seconds from the first USB
# report, so do not consider draw counts until after that route can complete.
LEGACY_WORLD_NOT_BEFORE_SECONDS = 80.0
HOLD_DETECT = 20
STATIONARY_FRAMES = 120
MOVEMENT_SECONDS = 40.0
# Frame CSV flushes at least every 0.5 s of flipping. Drain it after an
# operator assertion so buffered pre-acknowledgement rows cannot start a hold.
READY_DRAIN_SECONDS = 1.0
# Live input after the hold, as (seconds after input starts, line).
MOVEMENT_INPUT = (
    (4.0, "a:200"), (7.0, "a:200"), (10.0, "a:200"), (13.0, "a:200"),
    (16.0, "lup:2000"), (18.75, "y:300"),
    # Forward movement enters a tutorial cutscene shortly after measurement
    # starts at native speed. Keep the measured segment camera-only.
    (19.0, "rright:2000"), (22.0, "rleft:2500"),
    (25.0, "rright:1500"), (27.0, "rleft:2500"), (30.0, "rright:3000"),
    (34.0, "rleft:3000"), (38.0, "rright:3000"), (42.0, "rleft:2000"),
    (45.0, "rright:3000"), (49.0, "rleft:3000"), (53.0, "rright:3000"),
)
MOVEMENT_START = 20.0           # after the fixed tutorial-menu skip has settled
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
GUEST_GPU_WAITS = ("sub_00372180", "sub_00371CE0", "pfb_flush_wait_once",
                   "nv2a_pfb_flush_wait", "xbox_Nv2aPfbFlushWait",
                   "nv2a_segment_wait_once")
KERNEL_WAIT_SYMBOL_MARKERS = (
    "!NtWaitFor", "!ZwWaitFor", "!NtDelayExecution", "!ZwDelayExecution",
    "!NtYieldExecution", "!ZwYieldExecution", "!NtRemoveIoCompletion",
    "!ZwRemoveIoCompletion", "!NtWaitForAlertByThreadId",
    "!ZwWaitForAlertByThreadId", "!NtSignalAndWaitForSingleObject",
    "!NtWaitForWorkViaWorkerFactory", "!ZwWaitForWorkViaWorkerFactory",
    "!NtReplyWaitReceivePort", "!NtAlertThreadByThreadId",
    "!NtUserMsgWaitForMultipleObjectsEx", "!NtUserGetMessage",
    "!NtUserWaitMessage", "!NtGdiDdDDIWaitFor", "!NtDxgkWait",
)
DEFAULT_WATCHDOG = 900
# The boot can hang intermittently before the menus (FINDINGS); such a run
# is recorded and retried rather than measured.
STALL_SECONDS = 60

# nv2a_frame_hash of a 640x480 four-byte black surface, with alpha 0 or 255.
KNOWN_BLACK_FRAME_HASHES = bench_world.KNOWN_BLACK_FRAME_HASHES
RUNTIME_OPTION_KEYS = (
    "RECOMP_GPU", "RECOMP_D3D11_WARP", "RECOMP_RASTER_THREADS",
    "RECOMP_GPU_VSH", "RECOMP_GPU_VSH_SYNC", "RECOMP_GPU_VSH_VERIFY",
    "RECOMP_GPU_VSH_MULZERO", "RECOMP_GPU_TEX_DECODE",
    "RECOMP_GPU_TEX_CACHE_APPROX", "RECOMP_GPU_TEX_INDEX",
    "RECOMP_GPU_ZPASS", "RECOMP_GPU_PSCACHE",
    "RECOMP_GPU_DEPTH_TEXTURE", "RECOMP_GPU_STENCIL_ALIAS",
    "RECOMP_GPU_DIRECT_PRESENT", "RECOMP_GPU_SCALE", "RECOMP_GPU_OUTPUT",
    "RECOMP_GPU_AUDIT", "RECOMP_GPU_AUDIT_FILE",
    "RECOMP_PROGRAM_NEAR_ZERO", "RECOMP_PFB_FLUSH_WAIT",
    "RECOMP_NV2A_IDLE_WAIT", "RECOMP_NV2A_SEGMENTS", "RECOMP_PB_WALK",
    "RECOMP_PB_EXEC", "RECOMP_PB_INDEX_RUNS", "RECOMP_PGRAPH_IRQ",
    "RECOMP_VBLANK", "RECOMP_AUDIO", "BRUTEFORCE_PERF_COUNTERS",
    "BRUTEFORCE_PROFILE_CALLERS",
)
PERFORMANCE_SWITCHES = frozenset(("RECOMP_PFB_FLUSH_WAIT", "RECOMP_NV2A_IDLE_WAIT",
                                  "RECOMP_NV2A_SEGMENTS", "RECOMP_GPU_TEX_INDEX",
                                  "RECOMP_PB_INDEX_RUNS"))
OWNED_ENV = frozenset((
    "RECOMP_RASTER_THREADS", "RECOMP_PAD_SCRIPT", "RECOMP_PAD_LIVE",
    "RECOMP_FRAME_STATS", "RECOMP_WATCHDOG_SECS", "BRUTEFORCE_PROFILE",
    "BRUTEFORCE_PROFILE_GATE", "BRUTEFORCE_PROFILE_FROM", "BRUTEFORCE_PROFILE_TO",
    "BRUTEFORCE_PROFILE_CALLERS", "RECOMP_GPU_CENSUS", "RECOMP_GPU_CENSUS_GATE",
    "RECOMP_FB_DUMP", "RECOMP_FB_DUMP_FLIPS", "RECOMP_FB_DUMP_FLIPS_ONLY",
    "RECOMP_FB_DUMP_FLIP_ID", "RECOMP_FRAME_TRACE", "RECOMP_PB_REPORT_MS",
    "RECOMP_AUDIO_WAV", "RECOMP_AUDIO_WAV_SECS", "BRUTEFORCE_SAVE_ROOT",
    "RECOMP_SAVE_DIR", "RECOMP_GPU_AUDIT_FILE", "RECOMP_USB", "RECOMP_PB_WALK",
    "RECOMP_PB_EXEC", "RECOMP_PGRAPH_IRQ", "RECOMP_VBLANK", "RECOMP_FB_WINDOW",
    "RECOMP_D3D_PRESENT",
))
SUBMIT_PERF_RE = re.compile(
    r"^\[PERF-SUBMIT\] kind=(?P<kind>pfb|put) site=(?P<site>[0-9A-Fa-f]{8}) "
    r"caller=(?P<caller>[0-9A-Fa-f]{8}) calls=(?P<calls>\d+) "
    r"elapsed_ms=(?P<elapsed_ms>[\d.]+)"
    r"(?: window_start_us=(?P<start_us>\d+) window_end_us=(?P<end_us>\d+))?$", re.MULTILINE)
SHADER_PERF_RE = re.compile(
    r"^\[PERF-SHADER\] kind=pixel-compile calls=(?P<calls>\d+) "
    r"failures=(?P<failures>\d+) elapsed_ms=(?P<elapsed_ms>[\d.]+)"
    r"(?: window_start_us=(?P<start_us>\d+) window_end_us=(?P<end_us>\d+))?$", re.MULTILINE)

D3D11_DEVICE_RE = re.compile(
    r'^\[GPU-D3D11\] device adapter="(?P<adapter>.*)" '
    r'vendor=0x(?P<vendor>[0-9A-Fa-f]+) device=0x(?P<device>[0-9A-Fa-f]+) '
    r'subsystem=0x(?P<subsystem>[0-9A-Fa-f]+) revision=(?P<revision>\d+) '
    r'dedicated_vram=(?P<dedicated_vram>\d+) '
    r'luid=(?P<luid>[0-9A-Fa-f]+:[0-9A-Fa-f]+) '
    r'driver=(?P<driver>\S+) feature_level=0x(?P<feature_level>[0-9A-Fa-f]+)$',
    re.MULTILINE,
)
VALIDATION_PERF_RE = re.compile(
    r'^\[PERF\] (?P<name>guest_word|readable_guest_range): '
    r'(?P<calls>\d+) calls, (?P<accepted>\d+) accepted, (?P<rejected>\d+) rejected, '
    r'(?P<total_ms>[\d.]+) ms total; VirtualQuery (?P<queries>\d+) calls, '
    r'(?P<query_failures>\d+) failures, (?P<regions>\d+) regions, '
    r'(?P<query_ms>[\d.]+) ms$',
    re.MULTILINE,
)
TEXTURE_PERF_RE = re.compile(
    r'^\[PERF\] texture cache: (?P<lookups>\d+) lookups, (?P<probes>\d+) probes, '
    r'(?P<key_hits>\d+) key hits, (?P<key_misses>\d+) misses, '
    r'(?P<lookup_ms>[\d.]+) ms scanning; exact (?P<exact_checks>\d+) checks, '
    r'(?P<exact_hits>\d+) hits, (?P<source_bytes>\d+) source bytes, '
    r'(?P<palette_bytes>\d+) palette bytes, (?P<exact_ms>[\d.]+) ms; '
    r'snapshots (?P<snapshot_copies>\d+) copies, (?P<snapshot_bytes>\d+) bytes, '
    r'(?P<snapshot_ms>[\d.]+) ms; rewrites (?P<rewrites>\d+), '
    r'entry evictions (?P<evictions>\d+), snapshot evictions (?P<snapshot_evictions>\d+), '
    r'failed-upload discards (?P<discards>\d+), unsupported hits (?P<unsupported_hits>\d+)$',
    re.MULTILINE,
)
D3D11_INTERVAL_RE = re.compile(
    r'^\[GPU-D3D11\] (?P<flips>\d+) flips: .*?'
    r'readback (?P<readback_ms_per_flip>[\d.]+) ms/flip, '
    r'(?P<visibility_reads>\d+) visibility reads waiting '
    r'(?P<visibility_wait_ms>[\d.]+) ms, '
    r'(?:(?P<reports_written>\d+) reports written asynchronously '
    r'\((?P<report_pending_polls>\d+) polls found one pending\), )?.*?'
    r'(?P<approx_hash_bytes>\d+) approximate texture-hash bytes$',
    re.MULTILINE,
)
PSCACHE_EVENT_RE = re.compile(
    r'^\[GPU-PSCACHE\] (?P<kind>compiled|hit) in (?P<elapsed_ms>[\d.]+) ms$',
    re.MULTILINE,
)
PFB_FLUSH_WAIT_RE = re.compile(
    r'^\[PFB-WAIT\] waits=(?P<waits>\d+) acks=(?P<acks>\d+) '
    r'event_wakes=(?P<event_wakes>\d+) timer_wakes=(?P<timer_wakes>\d+) '
    r'spurious_wakes=(?P<spurious_wakes>\d+) rechecks=(?P<rechecks>\d+) '
    r'safe_points=(?P<safe_points>\d+)$',
    re.MULTILINE,
)
IDLE_WAIT_RE = re.compile(
    r"^\[NV2A-IDLE\] waits=(?P<waits>\d+) events=(?P<events>\d+) "
    r"timers=(?P<timers>\d+)$", re.MULTILINE)
SEGMENT_RE = re.compile(
    r"^\[NV2A-SEGMENT\] published=(?P<published>\d+) completed=(?P<completed>\d+) "
    r"pending=(?P<pending>\d+) high_water=(?P<high_water>\d+) "
    r"queue_waits=(?P<queue_waits>\d+) caller_overflow=(?P<caller_overflow>\d+)$",
    re.MULTILINE)
SEGMENT_CALLER_RE = re.compile(
    r"^\[NV2A-SEGMENT-CALLER\] caller=(?P<caller>[0-9A-Fa-f]{8}) "
    r"published=(?P<published>\d+) completed=(?P<completed>\d+)$", re.MULTILINE)
INDEX_RUN_RE = re.compile(
    r"^\[PB-INDEX-RUN\] calls=(?P<calls>\d+) words16=(?P<words16>\d+) "
    r"words32=(?P<words32>\d+) indices_buffered=(?P<indices_buffered>\d+)$",
    re.MULTILINE)


class BenchError(Exception):
    pass


def clean_environment(parent):
    """Do not inherit title/framework switches; audio defaults on afterward."""
    return {key: value for key, value in parent.items()
            if not key.upper().startswith(("RECOMP_", "BRUTEFORCE_"))}


def validate_owned_overrides(overrides):
    seen = set()
    for key in overrides:
        upper = key.upper()
        if upper in seen:
            raise BenchError(f"duplicate environment key with different casing: {key}")
        seen.add(upper)
        if not upper.startswith(("RECOMP_", "BRUTEFORCE_")):
            raise BenchError(f"variant environment must use RECOMP_ or BRUTEFORCE_: {key}")
        if upper in OWNED_ENV:
            raise BenchError(f"{upper} is managed by the benchmark and cannot be supplied by a variant")


def validate_gpu_audit_overrides(overrides):
    keys = [key for key in overrides if key.upper() == "RECOMP_GPU_AUDIT"]
    if any(key.upper() == "RECOMP_GPU_AUDIT_FILE" for key in overrides):
        raise BenchError("RECOMP_GPU_AUDIT_FILE is managed per attempt and cannot be supplied by a variant")
    if len(keys) > 1:
        raise BenchError("RECOMP_GPU_AUDIT cannot be supplied more than once with different casing")
    return keys[0] if keys else None


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
        if key in env:
            raise BenchError(f"duplicate variant environment key: {key}")
        env[key] = value
    return name or ",".join(f"{k}={v}" for k, v in env.items()) or "default", env


def validate_performance_variants(variants, args):
    if len(variants) != 2 or args.runs < 3:
        raise BenchError("performance comparisons require exactly two variants and at least three runs each")
    if (args.scene != "movement" or args.readiness != "manual" or
            not args.no_auto_panels or not args.capture_world or
            args.no_profile or not args.profile_callers):
        raise BenchError("performance comparisons require movement, manual readiness, fixed input, "
                         "--capture-world and --profile-callers")
    options = [{key.upper(): value for key, value in env.items()} for _, env in variants]
    for item in options:
        for key in PERFORMANCE_SWITCHES:
            if item.get(key, "0") not in ("0", "1"):
                raise BenchError(f"{key} must be 0 or 1")
    if any(options[0].get(key, "0") != "0" for key in PERFORMANCE_SWITCHES):
        raise BenchError("the first performance variant must be the all-switches-off baseline")
    if not any(options[1].get(key, "0") == "1" for key in PERFORMANCE_SWITCHES):
        raise BenchError("the second performance variant must enable at least one performance switch")
    if options[0].get("BRUTEFORCE_PERF_COUNTERS") != "1":
        raise BenchError("performance comparisons require BRUTEFORCE_PERF_COUNTERS=1 in both variants")
    if ({key: value for key, value in options[0].items() if key not in PERFORMANCE_SWITCHES} !=
            {key: value for key, value in options[1].items() if key not in PERFORMANCE_SWITCHES}):
        raise BenchError("performance variants must have identical nonexperimental options")


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
        "frame_ms_max": round(max(intervals), 2),
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


def legacy_world_candidate(rows, index):
    return (
        rows[index]["draws"] >= WORLD_DRAWS
        and rows[index]["t"] - rows[0]["t"] >= LEGACY_WORLD_NOT_BEFORE_SECONDS
    )


def caller_gpu_wait_shares(text):
    marker = "== Outside the exe, by first exe caller"
    if marker not in text:
        return {}, False
    section = text.split(marker, 1)[1].split("\n== All threads", 1)[0]
    shares = {}
    thread = None
    for line in section.splitlines():
        match = re.fullmatch(r"T(\d+)", line.strip())
        if match:
            thread = int(match.group(1))
            continue
        match = re.match(r"^\s+([\d.]+)%\s+(.+?)\s+<-\s+(.+?)\s*$", line)
        if thread is None or not match:
            continue
        share, outside, caller = match.groups()
        if (any(wait in outside for wait in KERNEL_WAIT_SYMBOL_MARKERS) and
                any(caller.endswith("!" + wait) for wait in GUEST_GPU_WAITS)):
            shares[thread] = shares.get(thread, 0.0) + float(share)
    return {thread: round(share, 2) for thread, share in shares.items()}, True


def unresolved_caller_wait(text, thread):
    marker = "== Outside the exe, by first exe caller"
    if marker not in text:
        return True
    section = text.split(marker, 1)[1].split("\n== All threads", 1)[0]
    current = None
    for line in section.splitlines():
        match = re.fullmatch(r"T(\d+)", line.strip())
        if match:
            current = int(match.group(1))
            continue
        match = re.match(r"^\s+[\d.]+%\s+(.+?)\s+<-\s+(.+?)\s*$", line)
        if current == thread and match:
            outside, caller = match.groups()
            if "!+0x" in outside or outside.startswith("0x"):
                return True
            if (any(wait in outside for wait in KERNEL_WAIT_SYMBOL_MARKERS) and
                    (caller == "(no exe frame)" or "!+0x" in caller or caller.startswith("0x"))):
                return True
    return False


def parse_profile(path):
    """Per-thread activity plus strictly attributable game-thread GPU waits."""
    path = Path(path)
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    caller_waits, callers_available = caller_gpu_wait_shares(text)
    caller_manifest = re.search(r"^CALLERS,complete_v1,(\d+)$", text, re.MULTILINE)
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
        info["unresolved_symbols"] = any("!+0x" in name or name.startswith("0x")
                                         for _, name in functions)
        info["top"] = [(name, float(share)) for share, name in functions[:12]]
        info["guest_gpu_wait_pct"] = round(sum(
            float(share) for share, name in functions
            if any(name.endswith("!" + wait) for wait in GUEST_GPU_WAITS)), 2)
        if callers_available:
            info["kernel_gpu_wait_pct"] = caller_waits.get(int(match.group(1)), 0.0)
    header = text.splitlines()[0] if text else ""
    summary = {"header": header, "threads": threads}
    game = [(index, t) for index, t in threads.items()
            if t["start"].endswith("!mainCRTStartup") and t["samples"]]
    nv2a = [t for t in threads.values() if t["start"].endswith("!nv2a_ack_thread")]
    raster = [t for t in threads.values() if t["start"].endswith("!raster_worker")]
    if game:
        game_index, g = game[0]
        kernel = 100.0 * g["kernel_wait"] / g["samples"]
        guest_gpu = g.get("guest_gpu_wait_pct", 0.0)
        kernel_gpu = g.get("kernel_gpu_wait_pct") if callers_available else None
        gpu_wait = guest_gpu + (kernel_gpu or 0.0)
        summary["game_thread"] = {
            "busy_pct": round(100.0 * g["busy"] / g["samples"], 2),
            "kernel_wait_pct": round(kernel, 2),
            "guest_gpu_poll_pct": guest_gpu,
            "kernel_gpu_wait_pct": kernel_gpu,
            "other_kernel_wait_pct": (
                round(max(0.0, kernel - kernel_gpu), 2) if kernel_gpu is not None else None),
            "gpu_wait_pct": round(min(100.0, gpu_wait), 2),
            "caller_attribution_available": callers_available,
            "caller_attribution_complete": bool(
                callers_available and caller_manifest and caller_manifest.group(1) == "0"
                and re.search(r"\b0 dropped\b", header)
                and not g.get("unresolved_symbols", True)
                and not unresolved_caller_wait(text, game_index) and g["samples"] >= 1000),
            "samples": g["samples"],
            "gpu_wait_scope": (
                "known guest GPU polls plus caller-attributed kernel waits"
                if callers_available else
                "known guest GPU polls only; rerun with --profile-callers for kernel attribution"
            ),
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


def parse_runtime_diagnostics(text):
    """Structured device and periodic renderer diagnostics from stderr."""
    diagnostics = {
        "device": None,
        "d3d11_intervals": [],
        "validation_intervals": {"guest_word": [], "readable_guest_range": []},
        "texture_intervals": [],
        "shader_cache": {
            "compiled": {"count": 0, "total_ms": 0.0, "max_ms": None},
            "hits": {"count": 0, "total_ms": 0.0, "max_ms": None},
        },
        "pfb_flush_wait": {
            "enabled": "[PFB-WAIT] event-driven flush wait enabled" in text,
            "snapshots": [],
            "latest": None,
        },
        "submit_intervals": [],
        "submit_caller_overflow": "[PERF-SUBMIT] caller table overflow:" in text,
        "pixel_shader_compile_intervals": [],
        "idle_wait": {
            "enabled": "[NV2A-IDLE] independent idle wait enabled" in text,
            "snapshots": [],
        },
        "segments": {
            "enabled": "[NV2A-SEGMENT] guarded per-PUT queue enabled" in text,
            "snapshots": [], "callers": [],
        },
        "index_runs": {
            "enabled": "[PB-INDEX-RUN] non-incrementing index runs enabled" in text,
            "audit_scalar": (
                "[PB-INDEX-RUN] scalar dispatch retained while GPU audit is enabled" in text
            ),
            "snapshots": [],
        },
    }
    for match in SUBMIT_PERF_RE.finditer(text):
        item = match.groupdict()
        diagnostics["submit_intervals"].append({
            "kind": item["kind"], "site": item["site"].upper(),
            "caller": item["caller"].upper(), "calls": int(item["calls"]),
            "elapsed_ms": float(item["elapsed_ms"]),
            "window_start_us": int(item["start_us"]) if item["start_us"] is not None else None,
            "window_end_us": int(item["end_us"]) if item["end_us"] is not None else None,
        })
    for match in SHADER_PERF_RE.finditer(text):
        diagnostics["pixel_shader_compile_intervals"].append({
            "calls": int(match["calls"]), "failures": int(match["failures"]),
            "elapsed_ms": float(match["elapsed_ms"]),
            "window_start_us": int(match["start_us"]) if match["start_us"] is not None else None,
            "window_end_us": int(match["end_us"]) if match["end_us"] is not None else None,
        })
    for match in IDLE_WAIT_RE.finditer(text):
        diagnostics["idle_wait"]["snapshots"].append(
            {key: int(value) for key, value in match.groupdict().items()})
    for match in SEGMENT_RE.finditer(text):
        diagnostics["segments"]["snapshots"].append(
            {key: int(value) for key, value in match.groupdict().items()})
    for match in SEGMENT_CALLER_RE.finditer(text):
        diagnostics["segments"]["callers"].append({
            "caller": match["caller"].upper(), "published": int(match["published"]),
            "completed": int(match["completed"]),
        })
    for match in INDEX_RUN_RE.finditer(text):
        diagnostics["index_runs"]["snapshots"].append(
            {key: int(value) for key, value in match.groupdict().items()})
    match = D3D11_DEVICE_RE.search(text)
    if match:
        values = match.groupdict()
        diagnostics["device"] = {
            "adapter": values["adapter"],
            "vendor_id": int(values["vendor"], 16),
            "device_id": int(values["device"], 16),
            "subsystem_id": int(values["subsystem"], 16),
            "revision": int(values["revision"]),
            "dedicated_vram_bytes": int(values["dedicated_vram"]),
            "luid": values["luid"],
            "driver": values["driver"],
            "feature_level": values["feature_level"].upper(),
        }
    for match in D3D11_INTERVAL_RE.finditer(text):
        values = match.groupdict()
        diagnostics["d3d11_intervals"].append({
            "flips": int(values["flips"]),
            "readback_ms_per_flip": float(values["readback_ms_per_flip"]),
            "visibility_reads": int(values["visibility_reads"]),
            "visibility_wait_ms": float(values["visibility_wait_ms"]),
            "reports_written": (int(values["reports_written"])
                                if values["reports_written"] is not None else None),
            "report_pending_polls": (int(values["report_pending_polls"])
                                     if values["report_pending_polls"] is not None else None),
            "approx_hash_bytes": int(values["approx_hash_bytes"]),
        })
    for match in VALIDATION_PERF_RE.finditer(text):
        values = match.groupdict()
        diagnostics["validation_intervals"][values["name"]].append({
            "calls": int(values["calls"]),
            "accepted": int(values["accepted"]),
            "rejected": int(values["rejected"]),
            "total_ms": float(values["total_ms"]),
            "virtual_queries": int(values["queries"]),
            "query_failures": int(values["query_failures"]),
            "regions": int(values["regions"]),
            "query_ms": float(values["query_ms"]),
        })
    for match in TEXTURE_PERF_RE.finditer(text):
        values = match.groupdict()
        diagnostics["texture_intervals"].append({
            key: (float(value) if key.endswith("_ms") else int(value))
            for key, value in values.items()
        })
    cache_times = {"compiled": [], "hit": []}
    for match in PSCACHE_EVENT_RE.finditer(text):
        cache_times[match["kind"]].append(float(match["elapsed_ms"]))
    for kind, output in (("compiled", "compiled"), ("hit", "hits")):
        values = cache_times[kind]
        diagnostics["shader_cache"][output] = {
            "count": len(values),
            "total_ms": round(sum(values), 3),
            "max_ms": round(max(values), 3) if values else None,
        }
    for match in PFB_FLUSH_WAIT_RE.finditer(text):
        diagnostics["pfb_flush_wait"]["snapshots"].append(
            {key: int(value) for key, value in match.groupdict().items()}
        )
    if diagnostics["pfb_flush_wait"]["snapshots"]:
        diagnostics["pfb_flush_wait"]["latest"] = (
            diagnostics["pfb_flush_wait"]["snapshots"][-1]
        )
    return diagnostics


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
             capture_world=False, profile_callers=False):
    if profile_callers and not profile:
        raise BenchError("profile caller attribution requires profiling")
    audit_key = validate_gpu_audit_overrides(env_overrides)
    overridden = {key.upper() for key in env_overrides}
    if profile_callers and "BRUTEFORCE_PROFILE_CALLERS" in overridden:
        raise BenchError("--profile-callers owns BRUTEFORCE_PROFILE_CALLERS")
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
    validate_owned_overrides(env_overrides)
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
        if profile_callers:
            env["BRUTEFORCE_PROFILE_CALLERS"] = "1"
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
    env.update({key.upper(): value for key, value in env_overrides.items()})
    audit_path = None
    if audit_key is not None:
        env["RECOMP_GPU_AUDIT"] = env.pop(audit_key.upper())
        if env["RECOMP_GPU_AUDIT"] == "1":
            audit_path = out_dir / "gpu_audit.csv"
            env["RECOMP_GPU_AUDIT_FILE"] = str(audit_path)
    profile_callers_enabled = (
        profile and env.get("BRUTEFORCE_PROFILE_CALLERS") == "1"
    )
    if readiness == "manual":
        env.pop("RECOMP_PAD_SCRIPT", None)
    wav_path = bench_control.reserve_audio_capture(out_dir, env) if capture_audio else None
    ready = (bench_control.ManualReadiness(out_dir, ready_state, ready_timeout)
             if readiness == "manual" else None)
    command, cwd, env = build.prepare_game_run(
        config, experimental_tutorial=True, isolated_saves=save, base_env=env
    )
    executable_path = Path(command[0]).resolve()
    executable_sha256 = (
        build.sha256_file(executable_path) if executable_path.is_file() else None
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
        "executable": {
            "path": str(executable_path),
            "sha256": executable_sha256,
        },
        "boot_dir": str(cwd), "out_dir": str(out_dir),
        "runtime_options": {key: env.get(key) for key in RUNTIME_OPTION_KEYS},
        "runtime_environment": {key.upper(): value for key, value in env.items()
                                if key.upper().startswith(("RECOMP_", "BRUTEFORCE_"))
                                and key.upper() not in OWNED_ENV},
        "options": {
            "config": config, "watchdog_seconds": watchdog,
            "profile": bool(profile), "profile_callers": profile_callers_enabled,
            "census": bool(census),
            "periodic_dump": bool(dump), "trace_frame": bool(trace_frame),
            "trace_after_seconds": trace_after if trace_frame else None,
            "capture_stationary": bool(capture), "capture_movement": bool(capture_movement),
            "capture_world": bool(capture_world), "capture_audio": bool(capture_audio),
            "readiness": readiness, "ready_timeout_seconds": ready_timeout if ready else None,
            "phase_timeout_seconds": phase_timeout,
            "automatic_panel_presses": not no_auto_panels,
        },
        "readiness": {"mode": readiness, "state": ready_state,
                      "verification": "operator assertion" if ready else "legacy heuristic"},
        "audio": {"mode": "on" if bench_control.audio_enabled(env) else "off",
                  "capture_requested": bool(wav_path),
                  "wav_path": str(wav_path) if wav_path else None},
        "gpu_audit": {
            "path": str(audit_path), "snapshot_after_window_confirmed": False,
            "report_complete": False,
            "scope": "cumulative runtime snapshot, including startup and possibly post-window work; "
                     "not necessarily final at console-control exit",
        } if audit_path else None,
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
                    world_run = world_run + 1 if legacy_world_candidate(rows, i) else 0
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
                if wait_for_outputs(out_dir, stderr_path, profile, census, process,
                                    audit_path=audit_path):
                    if audit_path:
                        result["gpu_audit"]["snapshot_after_window_confirmed"] = True
                    break
                result["error"] = (
                    "requested profile, census, or GPU audit output was not written after the measured window"
                    if audit_path else "profile or census output was not written"
                )
                break
            if exited:
                result["exit_code"] = process.returncode
                result["error"] = f"process exited with code {process.returncode} during {state}"
                break
            if state != phase:
                phase, phase_started = state, now
            elif state not in ("manual_ready", "finishing") and now - phase_started > phase_timeout:
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
    if audit_path:
        complete = gpu_audit_report_complete(audit_path)
        result["gpu_audit"]["report_complete"] = complete
        if not complete:
            result["error"] = result.get("error") or f"GPU audit report missing or incomplete: {audit_path}"
    result["state"] = state
    result["panel_presses"] = panel_presses
    result["input_events"] = input_events
    result["marks"] = marks
    result["wall_seconds"] = round(time.monotonic() - started, 1)
    result["flips"] = len(rows)
    log_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    result["runtime_diagnostics"] = parse_runtime_diagnostics(log_text)
    if ready and scene == "movement" and state == "finishing":
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
        if "[FTRACE] frame begins" not in log_text or "[FTRACE] end of frame" not in log_text:
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
    if scene == "movement":
        result["measurement_status"] = "failed" if result.get("error") else "inconclusive"
        result["measurement_reason"] = result.get("error") or (
            "Movement collection requires independent frame, state, and presentation review.")
    if profile:
        result["profile"] = parse_profile(out_dir / "profile.txt")
    if census:
        result["census"] = parse_census(out_dir / "census.csv")
    build.write_json(out_dir / "result.json", result)
    return result


def gpu_audit_report_complete(path, newer_than=None):
    """Recognize the audit writer's header and final counter in a closed snapshot."""
    try:
        with Path(path).open("rb") as stream:
            stat = os.fstat(stream.fileno())
            if newer_than is not None and stat.st_mtime <= newer_than:
                return False
            if not stream.readline().startswith(b"# GPU audit:"):
                return False
            if stream.readline().rstrip(b"\r\n") != b"category,key,count,vertices":
                return False
            stream.seek(max(0, stat.st_size - 128))
            tail = stream.read()
    except OSError:
        return False
    return re.search(rb"(?:^|\n)overflow,INVALID_MIRROR_ONLY,[0-9]+,0\r?\n\Z", tail) is not None


def wait_for_outputs(out_dir, stderr_path, profile, census, process, timeout=120, audit_path=None):
    """Wait for requested reports after the measurement gates close."""
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
        if audit_path:
            ok &= gpu_audit_report_complete(audit_path, newer_than=closed)
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
    parser.add_argument("--profile-callers", action="store_true",
                        help="attribute kernel waits to their first executable caller; "
                             "slower and required for a complete G3 estimate")
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
    parser.add_argument("--performance", action="store_true",
                        help="enforce a controlled, alternating baseline/experiment performance collection")
    parser.add_argument("--compare-performance", type=Path, metavar="MATCHES.json",
                        help="evaluate reviewed performance pairs and G2/G3/G4/G5 offline")
    parser.add_argument("--readiness", choices=("legacy", "manual"), default="legacy",
                        help="legacy timed menu script, or operator-confirmed visible state")
    parser.add_argument("--ready-state", help="precise visible state the operator must confirm")
    parser.add_argument("--ready-timeout", type=float, default=600.0,
                        help="seconds allowed for manual readiness (default: 600)")
    parser.add_argument("--phase-timeout", type=float, default=240.0,
                        help="seconds per benchmark phase after readiness (default: 240)")
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
        if sum(bool(value) for value in (args.analyze_world, args.compare_matched,
                                         args.compare_performance)) > 1:
            raise BenchError("offline analysis modes are mutually exclusive")
        if args.analyze_world:
            analysis = bench_world.analyze_manifest(args.analyze_world)
            print(json.dumps(analysis, indent=2))
            return 0 if analysis["status"] == "reviewer_asserted" else 2
        if args.compare_matched:
            comparison = bench_match.compare_manifest(args.compare_matched, MOVEMENT_INPUT)
            print(json.dumps(comparison, indent=2))
            return 0 if comparison["status"] == "reviewer_asserted" else 1
        if args.compare_performance:
            comparison = bench_performance.compare_manifest(args.compare_performance,
                                                             MOVEMENT_INPUT)
            print(json.dumps(comparison, indent=2))
            return 0 if comparison["status"] == "pass" else 2
        if args.no_profile and args.profile_callers:
            raise BenchError("--profile-callers cannot be combined with --no-profile")
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
            validate_gpu_audit_overrides(env)
            validate_owned_overrides(env)
            for key, value in env.items():
                if key.upper() == "RECOMP_AC97_READY" and value.strip() in ("", "0"):
                    message = ("legacy RECOMP_AC97_READY is enabled by presence, even for 0; "
                               "use RECOMP_AUDIO=0 for audio off")
                    if args.readiness == "manual":
                        raise BenchError(message)
                    print(f"[bench] warning: {message}", file=sys.stderr, flush=True)
                if key.upper() == "RECOMP_AUDIO_WAV":
                    raise BenchError("RECOMP_AUDIO_WAV cannot be supplied by a variant; use --capture-audio")
        performance_collection = args.performance or (len(variants) > 1 and any(
                key.upper() in PERFORMANCE_SWITCHES for _, env in variants for key in env))
        if performance_collection:
            validate_performance_variants(variants, args)
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
        executable_identity = None
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
                                  args.phase_timeout, args.capture_world, args.profile_callers)
                run_generation = result.get("generation")
                if not result.get("error") and run_generation:
                    if generation is not None and run_generation != generation:
                        result["error"] = "game generation changed during the benchmark campaign"
                    else:
                        generation = run_generation
                    if performance_collection:
                        current_identity = (result.get("toolkit_tree"),
                                            result.get("executable", {}).get("sha256"))
                        if not all(current_identity):
                            result["error"] = "toolkit tree or executable hash missing"
                        elif executable_identity is not None and current_identity != executable_identity:
                            result["error"] = "toolkit tree or executable changed during the benchmark campaign"
                        else:
                            executable_identity = current_identity
                if not result.get("error"):
                    break
                attempt_errors.append({"attempt": attempt + 1, "error": result["error"],
                                       "out_dir": str(attempt_dir)})
                print(f"[bench]   {result['error']}", flush=True)
            result["attempts"] = attempt + 1
            result["attempt_errors"] = attempt_errors
            if args.scene == "movement":
                result["measurement_status"] = "failed" if result.get("error") else "inconclusive"
                result["measurement_reason"] = result.get("error") or (
                    "Movement collection requires independent frame, state, and presentation review.")
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
                      f"known black among {world['unknown_frames']} unclassified flips; "
                      "no reviewed labels",
                      flush=True)
            black = result.get("movement_known_black")
            if black and black["matched_frames"]:
                print(f"[bench]   warning: {black['percent']}% of hashed movement frames match "
                      "known black signatures; distinct fps does not establish world fps",
                      file=sys.stderr, flush=True)
            table = summarize(results)
            build.write_json(out / "summary.json", {
                "scene": args.scene, "runs_per_variant": args.runs, "threads": args.threads,
                "status": ("failed" if result.get("error") else
                           "inconclusive" if args.scene == "movement" else "collection_complete"),
                "collection_status": ("failed" if result.get("error") else
                                      "complete" if number == len(sequence) else "in_progress"),
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
        if args.scene == "movement":
            print("[bench] INCONCLUSIVE: movement data were collected, but world content, "
                  "dark/unchanged intervals, and presented frames remain unreviewed.",
                  file=sys.stderr)
            return 2
    except (BenchError, build.BuildError, bench_control.BenchControlError,
            bench_world.WorldLabelError, OSError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
