#!/usr/bin/env python3
"""Stream measurements from a 48 kHz stereo s16 WAV and an optional host log.

The JSON Lines output is evidence for an audio investigation, not a listening
test or an audio-quality verdict. Sample indices count stereo frames.
"""

import argparse
from bisect import bisect_right
from dataclasses import dataclass
from contextlib import ExitStack
import io
import json
import math
from pathlib import Path
import re
import struct
import sys


RATE = 48000
FRAME_BYTES = 4
CHUNK_FRAMES = 8192
MAX_INTERVALS = 1024
MAX_LABEL_LENGTH = 128
MAX_LOG_LINE_CHARS = 16384
DEFAULT_MIN_ZERO_RUN = 32
CROSSING_ZERO_GAP_MAX = 31
HOLE_LENGTHS = (32, 256)
SEQUENCER_PERIOD_FRAMES = 2048
RECURRENCE_FRAMES = SEQUENCER_PERIOD_FRAMES
RECURRENCE_TOLERANCE = 32
SPECTRUM_WINDOW = 256
SPECTRUM_STRIDE = 4096


class AudioAnalysisError(ValueError):
    pass


@dataclass(frozen=True)
class WavInfo:
    data_offset: int
    frames: int
    file_size: int
    format_name: str


@dataclass(frozen=True)
class Segment:
    number: int
    kind: str
    label: str | None
    start: int
    end: int

    def fields(self):
        return {
            "segment": self.number,
            "kind": self.kind,
            "label": self.label,
            "start_frame": self.start,
            "end_frame": self.end,
        }


def read_wav_info(stream):
    """Validate the entire RIFF container without loading its chunks."""
    stream.seek(0, 2)
    file_size = stream.tell()
    stream.seek(0)
    header = stream.read(12)
    if len(header) != 12 or header[:4] != b"RIFF" or header[8:12] != b"WAVE":
        raise AudioAnalysisError("expected a little-endian RIFF/WAVE file")
    riff_size = struct.unpack_from("<I", header, 4)[0]
    if riff_size < 4:
        raise AudioAnalysisError("invalid RIFF size")
    riff_end = riff_size + 8
    if riff_end > file_size:
        raise AudioAnalysisError("truncated RIFF/WAVE file")
    if riff_end < file_size:
        raise AudioAnalysisError("bytes beyond RIFF size (stale header or trailing data)")

    fmt = None
    data = None
    offset = 12
    while offset < riff_end:
        if riff_end - offset < 8:
            raise AudioAnalysisError("truncated RIFF chunk header")
        stream.seek(offset)
        chunk_header = stream.read(8)
        if len(chunk_header) != 8:
            raise AudioAnalysisError("truncated RIFF chunk header")
        chunk_id, size = struct.unpack("<4sI", chunk_header)
        start = offset + 8
        end = start + size
        padded_end = end + (size & 1)
        if padded_end > riff_end:
            raise AudioAnalysisError("RIFF chunk extends beyond the file")
        if chunk_id == b"fmt ":
            if fmt is not None:
                raise AudioAnalysisError("duplicate WAV format chunk")
            stream.seek(start)
            fmt = (size, stream.read(min(size, 40)))
        elif chunk_id == b"data":
            if data is not None:
                raise AudioAnalysisError("duplicate WAV data chunk")
            if fmt is None:
                raise AudioAnalysisError("WAV data chunk precedes its format chunk")
            data = (start, size)
        offset = padded_end

    if fmt is None or data is None:
        raise AudioAnalysisError("WAV must contain one format and one data chunk")
    size, body = fmt
    if size < 16 or len(body) < min(size, 40):
        raise AudioAnalysisError("truncated WAV format chunk")
    tag, channels, rate, byte_rate, block_align, bits = struct.unpack_from(
        "<HHIIHH", body
    )
    if channels != 2 or rate != RATE or bits != 16:
        raise AudioAnalysisError("WAV must be 48000 Hz, stereo, 16-bit PCM")
    if block_align != FRAME_BYTES or byte_rate != RATE * FRAME_BYTES:
        raise AudioAnalysisError("invalid WAV block alignment or byte rate")

    if size == 17:
        raise AudioAnalysisError("truncated WAV format extension")
    extension_size = struct.unpack_from("<H", body, 16)[0] if size >= 18 else 0
    if size >= 18 and extension_size > size - 18:
        raise AudioAnalysisError("truncated WAV format extension")
    if tag == 1:
        format_name = "PCM"
    elif tag == 0xFFFE:
        pcm_guid = bytes.fromhex("0100000000001000800000aa00389b71")
        if size < 40 or extension_size < 22:
            raise AudioAnalysisError("truncated WAVE_FORMAT_EXTENSIBLE format")
        valid_bits, channel_mask = struct.unpack_from("<HI", body, 18)
        if valid_bits != 16 or channel_mask not in (0, 3) or body[24:40] != pcm_guid:
            raise AudioAnalysisError("WAVE_FORMAT_EXTENSIBLE must describe stereo 16-bit PCM")
        format_name = "WAVE_FORMAT_EXTENSIBLE PCM"
    else:
        raise AudioAnalysisError("WAV encoding is not signed 16-bit PCM")

    data_offset, data_size = data
    if data_size == 0:
        raise AudioAnalysisError("WAV contains no sample frames")
    if data_size % FRAME_BYTES:
        raise AudioAnalysisError("WAV data ends in a partial stereo sample frame")
    return WavInfo(data_offset, data_size // FRAME_BYTES, file_size, format_name)


def make_segments(frames, active_specs=(), silence_specs=()):
    """Validate labelled half-open ranges and fill gaps with unlabelled segments."""
    if len(active_specs) + len(silence_specs) > MAX_INTERVALS:
        raise AudioAnalysisError(f"at most {MAX_INTERVALS} intervals are supported")
    intervals = []
    labels = set()
    for kind, specs in (("active", active_specs), ("intentional_silence", silence_specs)):
        for number, spec in enumerate(specs, 1):
            parts = spec.split(":")
            if len(parts) == 2:
                label = f"{kind}-{number}"
                start_text, end_text = parts
            elif len(parts) == 3:
                label, start_text, end_text = parts
            else:
                raise AudioAnalysisError(f"invalid interval {spec!r}: use [LABEL:]START:END")
            if (not label or len(label) > MAX_LABEL_LENGTH
                    or any(ord(char) < 32 or ord(char) == 127 for char in label)):
                raise AudioAnalysisError(f"invalid interval label in {spec!r}")
            if label in labels:
                raise AudioAnalysisError(f"duplicate interval label: {label!r}")
            labels.add(label)
            try:
                start, end = int(start_text, 10), int(end_text, 10)
            except ValueError as exc:
                raise AudioAnalysisError(f"invalid frame indices in {spec!r}") from exc
            if start < 0 or start >= end or end > frames:
                raise AudioAnalysisError(
                    f"interval {spec!r} must satisfy 0 <= START < END <= {frames}"
                )
            intervals.append((start, end, kind, label))

    intervals.sort()
    segments = []
    cursor = 0
    for start, end, kind, label in intervals:
        if start < cursor:
            raise AudioAnalysisError(f"overlapping intervals at frame {start}")
        if start > cursor:
            segments.append(Segment(len(segments), "unlabelled", None, cursor, start))
        segments.append(Segment(len(segments), kind, label, start, end))
        cursor = end
    if cursor < frames:
        segments.append(Segment(len(segments), "unlabelled", None, cursor, frames))
    return segments


_HANN = tuple(
    0.5 - 0.5 * math.cos(2.0 * math.pi * i / (SPECTRUM_WINDOW - 1))
    for i in range(SPECTRUM_WINDOW)
)
_BIT_REVERSE = tuple(int(f"{i:08b}"[::-1], 2) for i in range(SPECTRUM_WINDOW))
_FFT_STAGES = tuple(
    tuple(complex(math.cos(-2 * math.pi * j / size),
                  math.sin(-2 * math.pi * j / size))
          for j in range(size // 2))
    for size in (2, 4, 8, 16, 32, 64, 128, 256)
)


def spectral_flatness(samples):
    """A windowed power-spectrum indicator, not a noise classifier."""
    if sum(value * value for value in samples) < SPECTRUM_WINDOW:
        return None
    values = [complex(samples[i] * _HANN[i], 0) for i in _BIT_REVERSE]
    for twiddles in _FFT_STAGES:
        half = len(twiddles)
        size = half * 2
        for start in range(0, SPECTRUM_WINDOW, size):
            for j, twiddle in enumerate(twiddles):
                even = values[start + j]
                odd = values[start + j + half] * twiddle
                values[start + j] = even + odd
                values[start + j + half] = even - odd
    powers = [v.real * v.real + v.imag * v.imag
              for v in values[1:SPECTRUM_WINDOW // 2 + 1]]
    mean = sum(powers) / len(powers)
    if mean <= 0:
        return None
    floor = mean * 1e-12
    return math.exp(sum(math.log(max(power, floor)) for power in powers) / len(powers)
                    - math.log(mean))


class ChannelMetrics:
    def __init__(self):
        self.frames = 0
        self.sum_squares = 0
        self.peak = 0
        self.clipped = 0
        self.crossings = 0
        self.eligible_pairs = 0
        self.previous = None
        self.zero_gap = 0
        self.flatness_sum = 0.0
        self.flatness_windows = 0

    def add(self, sample):
        self.frames += 1
        self.sum_squares += sample * sample
        self.peak = max(self.peak, abs(sample))
        self.clipped += sample in (-32768, 32767)
        if sample:
            if self.previous is not None:
                self.eligible_pairs += 1
                self.crossings += (self.previous < 0) != (sample < 0)
            self.previous = sample
            self.zero_gap = 0
        else:
            self.zero_gap += 1
            if self.zero_gap > CROSSING_ZERO_GAP_MAX:
                self.previous = None

    def merge(self, other):
        self.frames += other.frames
        self.sum_squares += other.sum_squares
        self.peak = max(self.peak, other.peak)
        self.clipped += other.clipped
        self.crossings += other.crossings
        self.eligible_pairs += other.eligible_pairs
        self.flatness_sum += other.flatness_sum
        self.flatness_windows += other.flatness_windows

    def report(self, zeros):
        rms = math.sqrt(self.sum_squares / self.frames) if self.frames else 0.0
        return {
            "rms_s16": rms,
            "rms_dbfs": 20 * math.log10(rms / 32768) if rms else None,
            "peak_abs_s16": self.peak,
            "clipped_samples": self.clipped,
            "exact_zero": zeros.report(),
            "zero_crossings": self.crossings,
            "zero_crossing_eligible_pairs": self.eligible_pairs,
            "zero_crossing_rate": (
                self.crossings / self.eligible_pairs if self.eligible_pairs else None
            ),
            "spectral_flatness_mean": (
                self.flatness_sum / self.flatness_windows if self.flatness_windows else None
            ),
            "spectral_flatness_windows": self.flatness_windows,
        }


class ZeroRuns:
    def __init__(self, channel=None, on_run=None):
        self.channel = channel
        self.on_run = on_run
        self.start = None
        self.frames = 0
        self.count = 0
        self.longest = 0

    def add(self, is_zero, index):
        if is_zero:
            self.frames += 1
            if self.start is None:
                self.start = index
        elif self.start is not None:
            self.close(index, True)

    def close(self, end, bounded_right):
        if self.start is None:
            return
        start = self.start
        self.start = None
        length = end - start
        self.count += 1
        self.longest = max(self.longest, length)
        if self.on_run:
            self.on_run(self.channel, start, end, bounded_right)

    def report(self):
        return {"frames": self.frames, "runs": self.count, "longest_run_frames": self.longest}


class SegmentMetrics:
    def __init__(self, segment, emit):
        self.segment = segment
        self.emit = emit
        self.channels = [ChannelMetrics(), ChannelMetrics()]
        self.zeros = [ZeroRuns(name, self.on_run) for name in ("left", "right", "stereo")]
        self.holes = {length: 0 for length in HOLE_LENGTHS}
        self.near_pairs = 0
        self.near_same_length_pairs = 0
        self.previous_hole = None
        self.previous_by_length = {}
        self.window = [[], []]
        self.period_frames = 0
        self.period_all_zero = True
        self.periods_examined = 0
        self.silent_periods = 0

    @staticmethod
    def near(spacing):
        return (spacing is not None
                and abs(spacing - RECURRENCE_FRAMES) <= RECURRENCE_TOLERANCE)

    def on_run(self, channel, start, end, bounded_right):
        length = end - start
        bounded_left = start > self.segment.start
        if (channel != "stereo" or self.segment.kind != "active"
                or not bounded_left or not bounded_right or length not in HOLE_LENGTHS):
            return
        previous = self.previous_hole
        previous_same = self.previous_by_length.get(length)
        spacing = start - previous if previous is not None else None
        same_spacing = start - previous_same if previous_same is not None else None
        near = self.near(spacing)
        near_same = self.near(same_spacing)
        self.holes[length] += 1
        self.near_pairs += near
        self.near_same_length_pairs += near_same
        self.emit({
            "type": "hole_candidate", "segment": self.segment.number,
            "start_frame": start, "end_frame": end, "length_frames": length,
            "previous_candidate_start_frame": previous,
            "spacing_from_previous_frames": spacing,
            "near_2048_from_previous": near if spacing is not None else None,
            "previous_same_length_start_frame": previous_same,
            "spacing_from_previous_same_length_frames": same_spacing,
            "near_2048_from_previous_same_length": (
                near_same if same_spacing is not None else None
            ),
        })
        self.previous_hole = start
        self.previous_by_length[length] = start

    def add(self, index, left, right):
        self.channels[0].add(left)
        self.channels[1].add(right)
        self.zeros[0].add(left == 0, index)
        self.zeros[1].add(right == 0, index)
        self.zeros[2].add(left == 0 and right == 0, index)
        if self.segment.kind == "active":
            self.period_frames += 1
            self.period_all_zero = self.period_all_zero and left == 0 and right == 0
            if self.period_frames == SEQUENCER_PERIOD_FRAMES:
                self.periods_examined += 1
                if self.period_all_zero:
                    self.silent_periods += 1
                    self.emit({
                        "type": "silent_2048_period", "segment": self.segment.number,
                        "start_frame": index + 1 - SEQUENCER_PERIOD_FRAMES,
                        "end_frame": index + 1,
                    })
                self.period_frames = 0
                self.period_all_zero = True
        window_pos = (index - self.segment.start) % SPECTRUM_STRIDE
        if window_pos < SPECTRUM_WINDOW:
            self.window[0].append(left)
            self.window[1].append(right)
            if window_pos == SPECTRUM_WINDOW - 1:
                for channel, samples in zip(self.channels, self.window):
                    flatness = spectral_flatness(samples)
                    if flatness is not None:
                        channel.flatness_sum += flatness
                        channel.flatness_windows += 1
                    samples.clear()

    def finish(self):
        for zeros in self.zeros:
            zeros.close(self.segment.end, False)

    def report(self):
        return {
            "type": "segment_summary", **self.segment.fields(),
            "frames": self.segment.end - self.segment.start,
            "active_all_zero": (
                self.zeros[2].frames == self.segment.end - self.segment.start
                if self.segment.kind == "active" else None
            ),
            "channels": {
                "left": self.channels[0].report(self.zeros[0]),
                "right": self.channels[1].report(self.zeros[1]),
            },
            "stereo_exact_zero": self.zeros[2].report(),
            "active_2048_periods": (
                self.period_report() if self.segment.kind == "active" else None
            ),
            "hole_candidates": self.hole_report(),
        }

    def period_report(self):
        return {
            "periods_examined": self.periods_examined,
            "fully_silent_periods": self.silent_periods,
            "fully_silent_fraction": (
                self.silent_periods / self.periods_examined if self.periods_examined else None
            ),
            "excluded_partial_frames": self.period_frames,
        }

    def hole_report(self):
        return {
            "32_frames": self.holes[32], "256_frames": self.holes[256],
            "near_2048_pairs": self.near_pairs,
            "near_2048_same_length_pairs": self.near_same_length_pairs,
        }


def scan_wave(stream, info, segments, min_zero_run, emit, chunk_frames=CHUNK_FRAMES):
    if not isinstance(chunk_frames, int) or not 1 <= chunk_frames <= CHUNK_FRAMES:
        raise AudioAnalysisError(f"chunk size must be an integer from 1 to {CHUNK_FRAMES}")
    totals = [ChannelMetrics(), ChannelMetrics()]
    segment_starts = [segment.start for segment in segments]

    def on_zero_run(channel, start, end, bounded_right):
        if end - start >= min_zero_run:
            emit({
                "type": "zero_run", "channel": channel, "start_frame": start,
                "end_frame": end, "length_frames": end - start,
                "start_segment": segments[bisect_right(segment_starts, start) - 1].number,
                "end_segment": segments[bisect_right(segment_starts, end - 1) - 1].number,
                "bounded_left": start > 0, "bounded_right": bounded_right,
            })

    zeros = [ZeroRuns(name, on_zero_run) for name in ("left", "right", "stereo")]
    holes = {"32_frames": 0, "256_frames": 0,
             "near_2048_pairs": 0, "near_2048_same_length_pairs": 0}
    periods_examined = silent_periods = excluded_partial_frames = 0
    stream.seek(info.data_offset)
    for segment in segments:
        metrics = SegmentMetrics(segment, emit)
        index = segment.start
        while index < segment.end:
            count = min(chunk_frames, segment.end - index)
            block = stream.read(count * FRAME_BYTES)
            if len(block) != count * FRAME_BYTES:
                raise AudioAnalysisError("WAV data became truncated while reading")
            for left, right in struct.iter_unpack("<hh", block):
                metrics.add(index, left, right)
                zeros[0].add(left == 0, index)
                zeros[1].add(right == 0, index)
                zeros[2].add(left == 0 and right == 0, index)
                index += 1
        metrics.finish()
        emit(metrics.report())
        for total, channel in zip(totals, metrics.channels):
            total.merge(channel)
        for key, value in metrics.hole_report().items():
            holes[key] += value
        if segment.kind == "active":
            periods_examined += metrics.periods_examined
            silent_periods += metrics.silent_periods
            excluded_partial_frames += metrics.period_frames
    for run in zeros:
        run.close(info.frames, False)
    stream.seek(0, 2)
    if stream.tell() != info.file_size:
        raise AudioAnalysisError("WAV size changed while reading")
    result = {
        "type": "audio_summary",
        "frames": info.frames,
        "duration_seconds": info.frames / RATE,
        "channels": {
            "left": totals[0].report(zeros[0]),
            "right": totals[1].report(zeros[1]),
        },
        "stereo_exact_zero": zeros[2].report(),
        "active_2048_periods": {
            "periods_examined": periods_examined,
            "fully_silent_periods": silent_periods,
            "fully_silent_fraction": silent_periods / periods_examined if periods_examined else None,
            "excluded_partial_frames": excluded_partial_frames,
        },
        "hole_candidates": holes,
    }
    emit(result)
    return result


_UINT = r"\d+"
_SINT = r"-?\d+"
_FLOAT = r"-?\d+(?:\.\d+)?"
_SUMMARY_RE = re.compile(
    rf"^wav=(?P<wav>{_UINT}) played=(?P<played>{_UINT}) "
    rf"starves=(?P<starves>{_UINT}) starved_ms=(?P<starved_ms>{_FLOAT}) "
    rf"pending_ms=(?P<pending_ms>{_FLOAT}) deficit=(?P<deficit>{_SINT}) "
    rf"refused=(?P<refused>{_UINT}) gap_max_ms=(?P<gap_max_ms>{_FLOAT}) "
    rf"q_min=(?P<q_min>{_SINT}) trap_spells=(?P<trap_spells>{_UINT}) "
    rf"trap_long=(?P<trap_long>{_UINT}) trap_max_ms=(?P<trap_max_ms>{_FLOAT}) "
    rf"underruns=(?P<underruns>{_UINT})$"
)
_STARVE_RE = re.compile(
    rf"^starve wav=(?P<wav>{_UINT}) ms=(?P<ms>{_FLOAT}) "
    rf"queued=(?P<queued>{_SINT}) gap_ms=(?P<gap_ms>{_FLOAT}) trapped=(?P<trapped>[01])$"
)
_REFUSED_RE = re.compile(rf"^refused wav=(?P<wav>{_UINT}) queued=(?P<queued>{_SINT})$")
_TRAP_RE = re.compile(rf"^trap spell (?P<ms>{_FLOAT}) ms ending at wav=(?P<wav>{_UINT})$")
_UNDERRUN_RE = re.compile(rf"^underrun x(?P<underruns>{_UINT})$")
_IRQ_RE = re.compile(
    rf"^traps {_UINT} se_frames {_UINT} fe-trapped-frames {_UINT} "
    rf"underruns (?P<underruns>{_UINT})$"
)
_XA2_INIT_RE = re.compile(
    r"\[XA2\] XAudio2 initialized \(48000 Hz stereo 16-bit, "
    r"\d+ x \d+-sample buffers, \d+(?:\.\d+)? ms queued max\)"
)
_XA2_SELECTED_RE = re.compile(
    r"\[APU\] Using XAudio2 audio backend \(pacing: \d+\.\.\d+ samples queued\)"
)
_OTHER_BACKEND_RE = re.compile(
    r"\[APU\] (?:XAudio2 unavailable, falling back to waveOut|"
    r"waveOut(?:Open failed| audio output (?:initialized|shut down))|"
    r"Using (?!XAudio2\b)\S+ audio backend)|\[SDL2\]"
)
_FLOAT_FIELDS = {"starved_ms", "pending_ms", "gap_max_ms", "trap_max_ms", "ms", "gap_ms"}
_UINT32_FIELDS = {"starves", "refused", "trap_spells", "trap_long", "underruns"}
_UINT64_FIELDS = {"wav", "played"}
_INT32_FIELDS = {"q_min", "queued"}
_NONNEGATIVE_FIELDS = {"starved_ms", "gap_max_ms", "trap_max_ms", "ms", "gap_ms",
                       "q_min", "queued"}


def _parse_match(match):
    if not match:
        return None
    values = {}
    try:
        for name, text in match.groupdict().items():
            value = float(text) if name in _FLOAT_FIELDS else int(text)
            if isinstance(value, float):
                if not math.isfinite(value):
                    return None
            elif name in _UINT32_FIELDS:
                if not 0 <= value <= 0xFFFFFFFF:
                    return None
            elif name in _UINT64_FIELDS:
                if not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
                    return None
            elif name in _INT32_FIELDS:
                if not -(1 << 31) <= value < (1 << 31):
                    return None
            elif not -(1 << 63) <= value < (1 << 63):
                return None
            if name in _NONNEGATIVE_FIELDS and value < 0:
                return None
            values[name] = value
    except (ValueError, OverflowError):
        return None
    if "trap_long" in values and values["trap_long"] > values["trap_spells"]:
        return None
    return values


def read_host_log(stream, wav_frames, emit):
    """Read bounded lines from the actual XAudio2/APU diagnostic formats."""
    if stream is None:
        return {
            "type": "host_summary", "status": "inconclusive", "backend": "unknown",
            "reasons": ["no host log supplied"],
        }
    prefix = stream.read(4)
    stream.seek(0)
    encoding = "utf-16" if prefix.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
    text = io.TextIOWrapper(stream, encoding=encoding, errors="replace", newline=None)
    xa2 = other = False
    initializations = selections = 0
    snapshots = 0
    latest = None
    malformed = incomplete = overlong = discontinuities = undecodable = 0
    starve_lines = refused_lines = trap_lines = underrun_lines = irq_lines = 0
    underrun_max = irq_underrun_max = None
    observed_wav = observed_underruns = None
    device_stopped = 0
    issue_evidence = False

    def observe_wav(value):
        nonlocal observed_wav, discontinuities
        if observed_wav is not None and value < observed_wav:
            discontinuities += 1
        observed_wav = max(observed_wav or 0, value)

    def observe_underruns(value):
        nonlocal observed_underruns, discontinuities
        if observed_underruns is not None and value < observed_underruns:
            discontinuities += 1
        observed_underruns = max(observed_underruns or 0, value)

    try:
        while True:
            line = text.readline(MAX_LOG_LINE_CHARS + 1)
            if not line:
                break
            if len(line) > MAX_LOG_LINE_CHARS:
                overlong += 1
                while line and not line.endswith("\n"):
                    line = text.readline(MAX_LOG_LINE_CHARS + 1)
                continue
            if not line.endswith("\n"):
                incomplete += 1
                continue
            line = line.rstrip("\n")
            if "\ufffd" in line:
                undecodable += 1
            if _XA2_INIT_RE.search(line):
                xa2 = True
                initializations += 1
            if _XA2_SELECTED_RE.search(line):
                xa2 = True
                selections += 1
            if _OTHER_BACKEND_RE.search(line):
                other = True
            if "[APU] audio device stopped consuming" in line:
                device_stopped += 1
                issue_evidence = True
                emit({"type": "host_device_stopped"})

            if "[AUDIO-HOST]" in line:
                body = line.split("[AUDIO-HOST]", 1)[1].strip()
                if body.startswith("wav="):
                    values = _parse_match(_SUMMARY_RE.fullmatch(body))
                    if values is None or values["wav"] == 0:
                        malformed += 1
                        continue
                    observe_wav(values["wav"])
                    observe_underruns(values["underruns"])
                    if values["starves"] < starve_lines or values["refused"] < refused_lines:
                        discontinuities += 1
                    if latest is not None and any(
                        values[key] < latest[key]
                        for key in ("wav", "played", "starves", "starved_ms", "refused",
                                    "trap_spells", "trap_long", "underruns")
                    ):
                        discontinuities += 1
                    latest = values
                    snapshots += 1
                    issue_evidence |= any(values[key] > 0 for key in
                                          ("starves", "starved_ms", "refused", "underruns"))
                elif body.startswith("starve "):
                    values = _parse_match(_STARVE_RE.fullmatch(body))
                    if values is None:
                        malformed += 1
                        continue
                    observe_wav(values["wav"])
                    starve_lines += 1
                    issue_evidence = True
                    emit({"type": "host_starvation", "wav_frame": values["wav"],
                          "within_capture_bounds": values["wav"] <= wav_frames,
                          "starved_ms": values["ms"], "queued": values["queued"],
                          "gap_ms": values["gap_ms"], "trapped": bool(values["trapped"])})
                elif body.startswith("refused "):
                    values = _parse_match(_REFUSED_RE.fullmatch(body))
                    if values is None:
                        malformed += 1
                        continue
                    observe_wav(values["wav"])
                    refused_lines += 1
                    issue_evidence = True
                    emit({"type": "host_refused", "wav_end_frame": values["wav"],
                          "within_capture_bounds": values["wav"] <= wav_frames,
                          "queued": values["queued"]})
                elif body.startswith("trap spell "):
                    values = _parse_match(_TRAP_RE.fullmatch(body))
                    if values is None:
                        malformed += 1
                        continue
                    observe_wav(values["wav"])
                    trap_lines += 1
                    emit({"type": "host_trap_spell", "wav_end_frame": values["wav"],
                          "within_capture_bounds": values["wav"] <= wav_frames,
                          "duration_ms": values["ms"]})
                else:
                    malformed += 1
            elif "[XA2] underrun" in line:
                body = line.split("[XA2]", 1)[1].strip()
                values = _parse_match(_UNDERRUN_RE.fullmatch(body))
                if values is None or values["underruns"] == 0:
                    malformed += 1
                    continue
                count = values["underruns"]
                observe_underruns(count)
                if underrun_max is not None and count < underrun_max:
                    discontinuities += 1
                underrun_max = max(underrun_max or 0, count)
                underrun_lines += 1
                issue_evidence = True
                emit({"type": "host_underrun", "source": "XA2",
                      "cumulative_count": count})
            elif "[APU-IRQ]" in line:
                body = line.split("[APU-IRQ]", 1)[1].strip()
                values = _parse_match(_IRQ_RE.fullmatch(body))
                if values is None:
                    malformed += 1
                    continue
                count = values["underruns"]
                observe_underruns(count)
                irq_underrun_max = max(irq_underrun_max or 0, count)
                irq_lines += 1
                issue_evidence |= count > 0
    finally:
        text.detach()

    backend = ("ambiguous" if xa2 and other else "XAudio2" if xa2
               else "non-XAudio2" if other else "unknown")
    reasons = []
    if backend != "XAudio2":
        reasons.append(f"host backend is {backend}, not an unambiguous XAudio2 selection")
    if not snapshots:
        reasons.append("no usable periodic AUDIO-HOST counter snapshot")
    if latest is not None and latest["played"] == 0:
        reasons.append("the latest snapshot reports no device samples played")
    if latest is not None and latest["wav"] < wav_frames:
        reasons.append("the last periodic snapshot precedes the end of the WAV capture")
    if initializations > 1 or selections > 1:
        reasons.append("multiple XAudio2 initializations or selections were logged")
    if malformed:
        reasons.append("malformed or unsupported diagnostic lines")
    if incomplete:
        reasons.append("incomplete diagnostic lines")
    if overlong:
        reasons.append("overlong log lines were skipped")
    if undecodable:
        reasons.append("log contains undecodable text")
    if discontinuities:
        reasons.append("cumulative counters decreased; log may contain multiple runs")
    status = ("inconclusive" if reasons else "events_detected" if issue_evidence
              else "no_events_reported_in_snapshots")
    return {
        "type": "host_summary", "status": status, "backend": backend, "reasons": reasons,
        "log_encoding": encoding, "periodic_snapshots": snapshots, "latest_snapshot": latest,
        "starvation_event_lines": starve_lines, "refusal_event_lines": refused_lines,
        "trap_event_lines": trap_lines, "xa2_underrun_event_lines": underrun_lines,
        "xa2_underrun_max_counter": underrun_max, "apu_irq_lines": irq_lines,
        "apu_irq_underrun_max_counter": irq_underrun_max,
        "device_stopped_notices": device_stopped, "malformed_lines": malformed,
        "incomplete_lines": incomplete, "overlong_lines": overlong, "undecodable_lines": undecodable,
        "counter_discontinuities": discontinuities,
        "coverage": "periodic snapshots and logged events only; not a complete playback verdict",
    }


def analyze(wav_path, active_specs=(), silence_specs=(), host_log_path=None,
            min_zero_run=DEFAULT_MIN_ZERO_RUN, emit=None, chunk_frames=CHUNK_FRAMES):
    if not isinstance(min_zero_run, int) or isinstance(min_zero_run, bool) or min_zero_run < 1:
        raise AudioAnalysisError("minimum zero-run length must be a positive integer")
    if (not isinstance(chunk_frames, int) or isinstance(chunk_frames, bool)
            or not 1 <= chunk_frames <= CHUNK_FRAMES):
        raise AudioAnalysisError(f"chunk size must be an integer from 1 to {CHUNK_FRAMES}")
    if emit is None:
        emit = lambda record: None
    with ExitStack() as stack:
        wav = stack.enter_context(Path(wav_path).open("rb"))
        info = read_wav_info(wav)
        segments = make_segments(info.frames, active_specs, silence_specs)
        host = (stack.enter_context(Path(host_log_path).open("rb"))
                if host_log_path is not None else None)
        emit({
            "type": "audio_metadata", "format": info.format_name, "sample_rate": RATE,
            "channels": 2, "bits_per_sample": 16, "frames": info.frames,
            "duration_seconds": info.frames / RATE,
            "segments": [segment.fields() for segment in segments],
            "zero_run_report_min_frames": min_zero_run,
            "hole_lengths_frames": list(HOLE_LENGTHS),
            "recurrence_target_frames": RECURRENCE_FRAMES,
            "recurrence_tolerance_frames": RECURRENCE_TOLERANCE,
            "active_silent_period_frames": SEQUENCER_PERIOD_FRAMES,
            "zero_crossing_max_zero_gap_frames": CROSSING_ZERO_GAP_MAX,
            "spectral_window_frames": SPECTRUM_WINDOW,
            "spectral_stride_frames": SPECTRUM_STRIDE,
        })
        host_report = read_host_log(host, info.frames, emit)
        emit(host_report)
        audio_report = scan_wave(wav, info, segments, min_zero_run, emit, chunk_frames)
        return audio_report, host_report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", help="48 kHz, stereo, signed 16-bit PCM RIFF/WAVE file")
    parser.add_argument("--host-log", help="separate stderr log from the same run")
    parser.add_argument("--active", action="append", default=[], metavar="[LABEL:]START:END",
                        help="active half-open sample-frame interval; may be repeated")
    parser.add_argument("--silence", action="append", default=[], metavar="[LABEL:]START:END",
                        help="intentional-silence interval; may be repeated")
    parser.add_argument("--min-zero-run", type=int, default=DEFAULT_MIN_ZERO_RUN,
                        metavar="FRAMES", help="minimum reported zero run (default: 32)")
    args = parser.parse_args(argv)
    try:
        def emit(record):
            print(json.dumps(record, separators=(",", ":"), allow_nan=False))

        analyze(args.wav, args.active, args.silence, args.host_log, args.min_zero_run, emit)
    except (OSError, AudioAnalysisError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
