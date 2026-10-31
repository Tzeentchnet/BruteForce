import io
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import wave

import pytest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import audio_analysis as audio  # noqa: E402


XA2 = (
    "[XA2] XAudio2 initialized (48000 Hz stereo 16-bit, 4 x 512-sample buffers, "
    "42.7 ms queued max)\n"
    "[APU] Using XAudio2 audio backend (pacing: 1024..2048 samples queued)\n"
)


def snapshot(wav=1024, played=768, starves=0, starved_ms=0, pending_ms=-1.2,
             deficit=-5, refused=0, underruns=0, trap_spells=0, trap_long=0):
    return (
        f"[AUDIO-HOST] wav={wav} played={played} starves={starves} "
        f"starved_ms={starved_ms:.1f} pending_ms={pending_ms:.1f} deficit={deficit} "
        f"refused={refused} gap_max_ms=2.3 q_min=256 trap_spells={trap_spells} "
        f"trap_long={trap_long} trap_max_ms=0.0 underruns={underruns}\n"
    )


def write_wave(path, samples):
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(b"".join(struct.pack("<hh", left, right) for left, right in samples))
    return path


def analyze(path, **kwargs):
    records = []
    summary, host = audio.analyze(path, emit=records.append, **kwargs)
    return records, summary, host


def records_of(records, kind):
    return [record for record in records if record["type"] == kind]


def riff(*chunks):
    body = b"WAVE"
    for tag, payload in chunks:
        body += tag + struct.pack("<I", len(payload)) + payload
        if len(payload) & 1:
            body += b"\0"
    return b"RIFF" + struct.pack("<I", len(body)) + body


def pcm_format(tag=1, channels=2, rate=48000, byte_rate=192000, align=4, bits=16):
    return struct.pack("<HHIIHH", tag, channels, rate, byte_rate, align, bits)


def test_silence_without_labels_or_host_is_not_a_pass(tmp_path):
    path = write_wave(tmp_path / "quiet.wav", [(0, 0)] * 100)
    records, summary, host = analyze(path)
    assert host["status"] == "inconclusive"
    assert host["reasons"] == ["no host log supplied"]
    assert records[0]["segments"] == [{
        "segment": 0, "kind": "unlabelled", "label": None, "start_frame": 0, "end_frame": 100,
    }]
    assert summary["frames"] == 100
    assert summary["duration_seconds"] == pytest.approx(100 / 48000)
    assert summary["active_2048_periods"] == {
        "periods_examined": 0, "fully_silent_periods": 0,
        "fully_silent_fraction": None, "excluded_partial_frames": 0,
    }
    for channel in summary["channels"].values():
        assert channel["rms_s16"] == 0
        assert channel["rms_dbfs"] is None
        assert channel["clipped_samples"] == 0
        assert channel["zero_crossing_rate"] is None
        assert channel["spectral_flatness_mean"] is None
        assert channel["exact_zero"] == {"frames": 100, "runs": 1, "longest_run_frames": 100}
    assert [(r["channel"], r["start_frame"], r["end_frame"])
            for r in records_of(records, "zero_run")] == [
                ("left", 0, 100), ("right", 0, 100), ("stereo", 0, 100),
            ]
    assert not records_of(records, "hole_candidate")


def test_periodic_holes_and_intentional_silence_are_separate(tmp_path):
    samples = [(1000, -1000)] * 16000 + [(0, 0)] * 4000
    starts = [100, 2148, 4196, 6244, 8292, 10340]
    sizes = [32, 32, 32, 256, 256, 256]
    for start, size in zip(starts, sizes):
        samples[start:start + size] = [(0, 0)] * size
    path = write_wave(tmp_path / "holes.wav", samples)
    records, summary, _ = analyze(
        path, active_specs=["music:0:16000"], silence_specs=["quiet:16000:20000"],
        chunk_frames=13,
    )
    holes = records_of(records, "hole_candidate")
    assert [(r["start_frame"], r["end_frame"]) for r in holes] == [
        (start, start + size) for start, size in zip(starts, sizes)
    ]
    assert [r["near_2048_from_previous"] for r in holes] == [None] + [True] * 5
    assert holes[4]["previous_same_length_start_frame"] == 6244
    assert holes[4]["near_2048_from_previous_same_length"]
    assert summary["hole_candidates"] == {
        "32_frames": 3, "256_frames": 3, "near_2048_pairs": 5,
        "near_2048_same_length_pairs": 4,
    }
    stereo_runs = [r for r in records_of(records, "zero_run") if r["channel"] == "stereo"]
    assert [(r["start_frame"], r["end_frame"]) for r in stereo_runs] == [
        *((start, start + size) for start, size in zip(starts, sizes)),
        (16000, 20000),
    ]
    assert summary["stereo_exact_zero"]["frames"] == sum(sizes) + 4000
    segments = records_of(records, "segment_summary")
    assert segments[0]["active_all_zero"] is False
    assert segments[1]["kind"] == "intentional_silence"
    assert segments[1]["channels"]["left"]["rms_s16"] == 0
    assert segments[1]["hole_candidates"]["32_frames"] == 0
    assert segments[1]["hole_candidates"]["256_frames"] == 0


def test_silent_2048_period_ratio_excludes_intentional_silence(tmp_path):
    period = audio.SEQUENCER_PERIOD_FRAMES
    active1 = (
        [(0, 0)] * period + [(100, -100)] * period + [(0, 0)] * period
        + [(0, 100)] * period + [(100, -100)] * period + [(0, 0)] * 100
    )
    intentional = [(0, 0)] * (2 * period)
    active2 = [(0, 0)] * period
    unlabelled = [(0, 0)] * period
    boundary1 = len(active1)
    boundary2 = boundary1 + len(intentional)
    boundary3 = boundary2 + len(active2)
    path = write_wave(tmp_path / "period-ratio.wav", active1 + intentional + active2 + unlabelled)
    records, summary, _ = analyze(
        path, active_specs=[f"first:0:{boundary1}", f"second:{boundary2}:{boundary3}"],
        silence_specs=[f"intentional:{boundary1}:{boundary2}"], chunk_frames=257,
    )
    segments = records_of(records, "segment_summary")
    first = next(r for r in segments if r["label"] == "first")
    assert first["active_2048_periods"] == {
        "periods_examined": 5, "fully_silent_periods": 2,
        "fully_silent_fraction": 2 / 5, "excluded_partial_frames": 100,
    }
    assert next(r for r in segments if r["label"] == "intentional")["active_2048_periods"] is None
    assert summary["active_2048_periods"] == {
        "periods_examined": 6, "fully_silent_periods": 3,
        "fully_silent_fraction": 3 / 6, "excluded_partial_frames": 100,
    }
    assert [(r["start_frame"], r["end_frame"]) for r in records_of(records, "silent_2048_period")] == [
        (0, period), (2 * period, 3 * period), (boundary2, boundary3),
    ]


def test_short_active_interval_has_no_2048_period_denominator(tmp_path):
    path = write_wave(tmp_path / "short-active.wav", [(0, 0)] * 2047)
    records, summary, _ = analyze(path, active_specs=["0:2047"])
    expected = {
        "periods_examined": 0, "fully_silent_periods": 0,
        "fully_silent_fraction": None, "excluded_partial_frames": 2047,
    }
    assert summary["active_2048_periods"] == expected
    assert records_of(records, "segment_summary")[0]["active_2048_periods"] == expected
    assert not records_of(records, "silent_2048_period")


def test_recurrence_tolerance_is_inclusive(tmp_path):
    starts = [100, 2116, 4196, 6211, 8292]
    samples = [(100, -100)] * 9000
    for start in starts:
        samples[start:start + 32] = [(0, 0)] * 32
    records, _, _ = analyze(write_wave(tmp_path / "tolerance.wav", samples),
                             active_specs=["0:9000"])
    holes = records_of(records, "hole_candidate")
    assert [h["spacing_from_previous_frames"] for h in holes] == [None, 2016, 2080, 2015, 2081]
    assert [h["near_2048_from_previous"] for h in holes] == [None, True, True, False, False]


def test_hole_events_are_independent_of_zero_run_output_threshold(tmp_path):
    samples = [(100, -100)] * 100
    samples[10:42] = [(0, 0)] * 32
    records, summary, _ = analyze(write_wave(tmp_path / "threshold-hole.wav", samples),
                                   active_specs=["0:100"], min_zero_run=1000)
    assert not records_of(records, "zero_run")
    assert [r["start_frame"] for r in records_of(records, "hole_candidate")] == [10]
    assert summary["stereo_exact_zero"]["runs"] == 1


def test_intentional_quiet_can_contain_nonzero_pcm_and_silent_gaps(tmp_path):
    quiet = [(2, -2)] * 200
    quiet[50:82] = [(0, 0)] * 32
    samples = [(100, -100)] * 100 + quiet
    records, _, _ = analyze(
        write_wave(tmp_path / "intentionally-quiet.wav", samples),
        active_specs=["loud:0:100"], silence_specs=["quiet:100:300"],
    )
    quiet_report = next(r for r in records_of(records, "segment_summary") if r["label"] == "quiet")
    assert quiet_report["channels"]["left"]["rms_s16"] == pytest.approx(2 * math.sqrt(168 / 200))
    assert quiet_report["stereo_exact_zero"]["frames"] == 32
    assert not records_of(records, "hole_candidate")
    assert any(r["channel"] == "stereo" and r["start_frame"] == 150 and r["end_frame"] == 182
               for r in records_of(records, "zero_run"))


def test_candidate_requires_active_interval_and_nonzero_neighbors(tmp_path):
    samples = [(100, -100)] * 180
    for start in (0, 64, 128):
        samples[start:start + 32] = [(0, 0)] * 32
    path = write_wave(tmp_path / "boundaries.wav", samples)
    records, _, _ = analyze(
        path, active_specs=["edge:20:32", "active:40:120"],
        silence_specs=["intentional:120:180"], min_zero_run=1,
    )
    assert [r["start_frame"] for r in records_of(records, "hole_candidate")] == [64]
    summaries = records_of(records, "segment_summary")
    edge = next(r for r in summaries if r["label"] == "edge")
    assert edge["active_all_zero"]
    run = next(r for r in records_of(records, "zero_run")
               if r["end_segment"] == edge["segment"] and r["channel"] == "stereo")
    assert (run["start_frame"], run["end_frame"]) == (0, 32)
    assert not run["bounded_left"] and run["bounded_right"]
    assert edge["stereo_exact_zero"]["longest_run_frames"] == 12
    assert any(r["kind"] == "unlabelled" for r in summaries)


def test_channel_rms_clipping_and_zero_crossings(tmp_path):
    samples = [(32767, -32768), (0, 0), (-32768, 32767), (1000, -1000)]
    records, summary, _ = analyze(
        write_wave(tmp_path / "channels.wav", samples), active_specs=["0:4"], min_zero_run=1,
    )
    for channel in summary["channels"].values():
        assert channel["clipped_samples"] == 2
        assert channel["peak_abs_s16"] == 32768
        assert channel["rms_s16"] == pytest.approx(
            math.sqrt((32767 ** 2 + 32768 ** 2 + 1000 ** 2) / 4)
        )
        assert channel["zero_crossings"] == 2
        assert channel["zero_crossing_eligible_pairs"] == 2
        assert channel["zero_crossing_rate"] == 1
    runs = records_of(records, "zero_run")
    assert {(r["channel"], r["start_frame"], r["end_frame"]) for r in runs} == {
        ("left", 1, 2), ("right", 1, 2), ("stereo", 1, 2),
    }
    assert not records_of(records, "hole_candidate")


def test_zero_crossings_bridge_short_but_not_long_silence(tmp_path):
    samples = [(1, 1)] + [(0, 0)] * 31 + [(-1, -1)]
    samples += [(0, 0)] * 32 + [(1, 1)]
    _, summary, _ = analyze(write_wave(tmp_path / "crossing-gaps.wav", samples))
    for channel in summary["channels"].values():
        assert channel["zero_crossings"] == 1
        assert channel["zero_crossing_eligible_pairs"] == 1


def test_flatness_and_crossings_are_indicators_not_noise_verdicts(tmp_path):
    rng = random.Random(1999)
    samples = [
        (round(6000 * math.sin(2 * math.pi * 1000 * i / 48000)), rng.randint(-6000, 6000))
        for i in range(8192)
    ]
    records, summary, _ = analyze(write_wave(tmp_path / "tone-noise.wav", samples),
                                   active_specs=["0:8192"])
    tone = summary["channels"]["left"]
    noise = summary["channels"]["right"]
    assert tone["spectral_flatness_windows"] == noise["spectral_flatness_windows"] == 2
    assert 0 <= tone["spectral_flatness_mean"] < 0.1
    assert 0.2 < noise["spectral_flatness_mean"] <= 1
    assert 0.03 < tone["zero_crossing_rate"] < 0.05
    assert 0.45 < noise["zero_crossing_rate"] < 0.55
    assert not any("noise_defect" in record or "noise_verdict" in record for record in records)


def test_spectral_flatness_matches_independent_dft():
    rng = random.Random(43)
    samples = [rng.randint(-1000, 1000) for _ in range(audio.SPECTRUM_WINDOW)]
    windowed = [
        sample * (0.5 - 0.5 * math.cos(2 * math.pi * index / 255))
        for index, sample in enumerate(samples)
    ]
    powers = []
    for frequency in range(1, 129):
        real = sum(value * math.cos(2 * math.pi * frequency * index / 256)
                   for index, value in enumerate(windowed))
        imaginary = sum(value * math.sin(2 * math.pi * frequency * index / 256)
                        for index, value in enumerate(windowed))
        powers.append(real * real + imaginary * imaginary)
    mean = sum(powers) / len(powers)
    expected = math.exp(sum(math.log(max(power, mean * 1e-12)) for power in powers)
                        / len(powers) - math.log(mean))
    assert audio.spectral_flatness(samples) == pytest.approx(expected, rel=1e-10)
    assert audio.spectral_flatness([0] * 255 + [1]) is None


@pytest.mark.parametrize("specs", [
    {"active_specs": ["-1:10"]},
    {"active_specs": ["3:3"]},
    {"active_specs": ["0:101"]},
    {"active_specs": ["start:end"]},
    {"active_specs": ["a:1:2:3"]},
    {"active_specs": [":0:10"]},
    {"active_specs": ["same:0:20"], "silence_specs": ["same:20:40"]},
    {"active_specs": ["0:30"], "silence_specs": ["20:40"]},
])
def test_invalid_or_overlapping_intervals_fail(tmp_path, specs):
    path = write_wave(tmp_path / "intervals.wav", [(1, 1)] * 100)
    with pytest.raises(audio.AudioAnalysisError):
        analyze(path, **specs)


def test_interval_count_is_bounded():
    with pytest.raises(audio.AudioAnalysisError, match="at most"):
        audio.make_segments(5000, ["0:1"] * (audio.MAX_INTERVALS + 1))


@pytest.mark.parametrize("size", [0, -1, 1.5, True, audio.CHUNK_FRAMES + 1])
def test_invalid_or_unbounded_chunk_size_fails(tmp_path, size):
    path = write_wave(tmp_path / "chunk.wav", [(1, 1)])
    with pytest.raises(audio.AudioAnalysisError, match="chunk size"):
        analyze(path, chunk_frames=size)


@pytest.mark.parametrize("minimum", [0, -1, 1.5, True, float("nan")])
def test_invalid_zero_run_threshold_fails(tmp_path, minimum):
    path = write_wave(tmp_path / "threshold.wav", [(1, 1)])
    with pytest.raises(audio.AudioAnalysisError, match="minimum zero-run"):
        analyze(path, min_zero_run=minimum)


def test_riff_extra_chunks_padding_and_extensible_pcm(tmp_path):
    guid = bytes.fromhex("0100000000001000800000aa00389b71")
    extended = pcm_format(tag=0xFFFE) + struct.pack("<HHI", 22, 16, 3) + guid
    path = tmp_path / "extensible.wav"
    path.write_bytes(riff((b"JUNK", b"xyz"), (b"fmt ", extended),
                          (b"data", struct.pack("<hh", 123, -456)), (b"LIST", b"x")))
    records, summary, _ = analyze(path, min_zero_run=1)
    assert records[0]["format"] == "WAVE_FORMAT_EXTENSIBLE PCM"
    assert summary["frames"] == 1
    assert summary["channels"]["left"]["rms_s16"] == 123
    assert summary["channels"]["right"]["rms_s16"] == 456


def invalid_wavs():
    fmt = pcm_format()
    data = struct.pack("<hh", 1, -1)
    valid = riff((b"fmt ", fmt), (b"data", data))
    guid = bytes.fromhex("0100000000001000800000aa00389b71")
    return [
        b"not a wav",
        b"RIFX" + valid[4:],
        b"RIFF" + struct.pack("<I", 12) + b"WAVEfmt " + struct.pack("<I", 0xFFFFFFFF),
        valid[:-1],
        valid + b"stale header data",
        riff((b"fmt ", fmt)),
        riff((b"data", data)),
        riff((b"data", data), (b"fmt ", fmt)),
        riff((b"fmt ", fmt), (b"fmt ", fmt), (b"data", data)),
        riff((b"fmt ", fmt), (b"data", data), (b"data", data)),
        riff((b"fmt ", fmt), (b"data", b"")),
        riff((b"fmt ", fmt), (b"data", b"\0" * 7)),
        riff((b"fmt ", fmt[:15]), (b"data", data)),
        riff((b"fmt ", fmt + b"\0"), (b"data", data)),
        riff((b"fmt ", fmt + struct.pack("<H", 20)), (b"data", data)),
        riff((b"fmt ", pcm_format(channels=1)), (b"data", data)),
        riff((b"fmt ", pcm_format(rate=44100)), (b"data", data)),
        riff((b"fmt ", pcm_format(bits=8)), (b"data", data)),
        riff((b"fmt ", pcm_format(byte_rate=42)), (b"data", data)),
        riff((b"fmt ", pcm_format(align=2)), (b"data", data)),
        riff((b"fmt ", pcm_format(tag=3)), (b"data", data)),
        riff((b"fmt ", pcm_format(tag=0xFFFE)), (b"data", data)),
        riff((b"fmt ", pcm_format(tag=0xFFFE) + struct.pack("<HHI", 22, 15, 3) + guid),
             (b"data", data)),
        riff((b"fmt ", pcm_format(tag=0xFFFE) + struct.pack("<HHI", 22, 16, 12) + guid),
             (b"data", data)),
        riff((b"fmt ", pcm_format(tag=0xFFFE) + struct.pack("<HHI", 22, 16, 3) + b"x" * 16),
             (b"data", data)),
    ]


@pytest.mark.parametrize("contents", invalid_wavs())
def test_malformed_or_truncated_wavs_fail_explicitly(tmp_path, contents):
    path = tmp_path / "invalid.wav"
    path.write_bytes(contents)
    with pytest.raises(audio.AudioAnalysisError):
        analyze(path)


def test_pcm_reads_are_chunk_bounded(tmp_path):
    path = write_wave(tmp_path / "streamed.wav", [(500, -500)] * 32768)

    class LimitedReader:
        def __init__(self, stream):
            self.stream = stream
            self.max_read = 0

        def read(self, size=-1):
            assert 0 <= size <= 257 * audio.FRAME_BYTES
            self.max_read = max(self.max_read, size)
            return self.stream.read(size)

        def __getattr__(self, name):
            return getattr(self.stream, name)

    with path.open("rb") as stream:
        reader = LimitedReader(stream)
        info = audio.read_wav_info(reader)
        result = audio.scan_wave(reader, info, audio.make_segments(info.frames), 32,
                                 lambda record: None, chunk_frames=257)
    assert reader.max_read == 257 * audio.FRAME_BYTES
    assert result["frames"] == 32768


def test_results_do_not_depend_on_pcm_chunk_boundaries(tmp_path):
    rng = random.Random(5)
    samples = [(rng.choice((0, 0, 1, -1, 200)), rng.choice((0, 0, 1, -1, -200)))
               for _ in range(1200)]
    samples[300:332] = [(0, 0)] * 32
    path = write_wave(tmp_path / "chunk-boundaries.wav", samples)
    arguments = {
        "active_specs": ["first:50:700"],
        "silence_specs": ["second:700:1000"],
        "min_zero_run": 1,
    }
    expected, _, _ = analyze(path, chunk_frames=1, **arguments)
    for chunk_frames in (7, 31, 256, audio.CHUNK_FRAMES):
        actual, _, _ = analyze(path, chunk_frames=chunk_frames, **arguments)
        assert actual == expected


def test_wav_truncated_after_header_validation_fails(tmp_path):
    path = write_wave(tmp_path / "changing.wav", [(123, -456)] * 100)
    stream = io.BytesIO(path.read_bytes())
    info = audio.read_wav_info(stream)
    stream.truncate(info.data_offset + 8)
    with pytest.raises(audio.AudioAnalysisError, match="truncated while reading"):
        audio.scan_wave(stream, info, audio.make_segments(info.frames), 32,
                        lambda record: None, chunk_frames=8)


def test_actual_host_counters_and_event_positions(tmp_path):
    path = write_wave(tmp_path / "host.wav", [(100, -100)] * 3000)
    host = tmp_path / "host.log"
    host.write_text(
        XA2
        + "[AUDIO-HOST] starve wav=512 ms=12.3 queued=0 gap_ms=14.0 trapped=1\n"
        + "[AUDIO-HOST] refused wav=1024 queued=2048\n"
        + "[AUDIO-HOST] trap spell 23.1 ms ending at wav=1280\n"
        + "[XA2] underrun x2\n"
        + "[APU-IRQ] traps 4 se_frames 100 fe-trapped-frames 2 underruns 2\n"
        + snapshot(wav=3072, played=1000, starves=3, starved_ms=32.1,
                   refused=4, underruns=2, trap_spells=1, trap_long=1)
        + "[XA2] underrun x3\n"
        + "[AUDIO-HOST] refused wav=4096 queued=2048\n",
        encoding="utf-8",
    )
    records, _, report = analyze(path, host_log_path=host)
    assert report["backend"] == "XAudio2"
    assert report["status"] == "events_detected"
    assert report["periodic_snapshots"] == 1
    assert report["latest_snapshot"]["starves"] == 3
    assert report["latest_snapshot"]["starved_ms"] == pytest.approx(32.1)
    assert report["latest_snapshot"]["refused"] == 4
    assert report["latest_snapshot"]["underruns"] == 2
    assert report["latest_snapshot"]["pending_ms"] == pytest.approx(-1.2)
    assert report["starvation_event_lines"] == 1
    assert report["refusal_event_lines"] == 2
    assert report["xa2_underrun_event_lines"] == 2
    assert report["xa2_underrun_max_counter"] == 3
    assert report["apu_irq_underrun_max_counter"] == 2
    assert [(r["wav_frame"], r["starved_ms"]) for r in records_of(records, "host_starvation")] == [
        (512, 12.3)
    ]
    refused = records_of(records, "host_refused")
    assert [(r["wav_end_frame"], r["within_capture_bounds"]) for r in refused] == [
        (1024, True), (4096, False),
    ]
    assert [r["cumulative_count"] for r in records_of(records, "host_underrun")] == [2, 3]
    assert records_of(records, "host_trap_spell")[0]["wav_end_frame"] == 1280


@pytest.mark.parametrize("encoding", ["utf-8", "utf-16"])
def test_zero_host_counters_are_snapshots_not_counted_as_events(tmp_path, encoding):
    path = write_wave(tmp_path / "zero-counters.wav", [(5, -5)] * 2000)
    host = tmp_path / "zero-counters.log"
    host.write_text(XA2 + snapshot(wav=2048), encoding=encoding)
    _, _, report = analyze(path, host_log_path=host)
    assert report["status"] == "no_events_reported_in_snapshots"
    assert report["latest_snapshot"]["underruns"] == 0
    assert report["xa2_underrun_event_lines"] == 0
    assert report["xa2_underrun_max_counter"] is None
    assert report["log_encoding"] == ("utf-16" if encoding == "utf-16" else "utf-8-sig")
    assert "not a complete playback verdict" in report["coverage"]


@pytest.mark.parametrize(("contents", "backend"), [
    ("", "unknown"),
    ("ordinary unrelated output\n", "unknown"),
    (XA2, "XAudio2"),
    (XA2 + snapshot(), "XAudio2"),
    (snapshot(), "unknown"),
    ("[SDL2] audio initialized: driver dummy\n" + snapshot(), "non-XAudio2"),
    ("[APU] XAudio2 unavailable, falling back to waveOut\n" + snapshot(), "non-XAudio2"),
    (XA2 + "[APU] waveOut audio output initialized (48kHz stereo 16-bit, 4 buffers)\n"
     + snapshot(), "ambiguous"),
    (XA2 + "[SDL2] underrun x1\n" + snapshot(wav=2048), "ambiguous"),
    (XA2 + snapshot(played=0), "XAudio2"),
    (XA2 + snapshot().replace("underruns=0", "underruns=bad"), "XAudio2"),
    (XA2 + snapshot(wav=2048).replace("q_min=256", "q_min=2147483648"), "XAudio2"),
    (XA2 + snapshot(wav=2048).replace("underruns=0", "underruns=4294967296"), "XAudio2"),
    (XA2 + snapshot(wav=2048).replace("deficit=-5", "deficit=9223372036854775808"), "XAudio2"),
    (XA2 + snapshot().rstrip("\n"), "XAudio2"),
    (XA2 + snapshot(wav=2048) + "truncated output", "XAudio2"),
    (XA2 + "[XA2] underrun x2\n" + snapshot(wav=2048, underruns=0), "XAudio2"),
    (XA2 + "[AUDIO-HOST] starve wav=512 ms=1.0 queued=0 gap_ms=2.0 trapped=0\n"
     + snapshot(wav=2048, starves=0), "XAudio2"),
    (XA2 + XA2 + snapshot(wav=2048), "XAudio2"),
    (XA2 + snapshot(wav=2000, played=1000, underruns=2)
     + snapshot(wav=1000, played=500, underruns=0), "XAudio2"),
    (XA2 + "[XA2] underrun x1\n", "XAudio2"),
])
def test_missing_invalid_or_non_xaudio_host_is_inconclusive(tmp_path, contents, backend):
    path = write_wave(tmp_path / "partial-host.wav", [(1, -1)] * 2048)
    host = tmp_path / "partial-host.log"
    host.write_text(contents, encoding="utf-8")
    _, _, report = analyze(path, host_log_path=host)
    assert report["status"] == "inconclusive"
    assert report["backend"] == backend
    assert report["reasons"]


def test_overlong_log_lines_are_skipped_and_inconclusive(tmp_path):
    path = write_wave(tmp_path / "long-host.wav", [(1, -1)] * 2048)
    host = tmp_path / "long-host.log"
    host.write_text(XA2 + snapshot(wav=2048) + "x" * (audio.MAX_LOG_LINE_CHARS * 3) + "\n",
                    encoding="utf-8")
    _, _, report = analyze(path, host_log_path=host)
    assert report["overlong_lines"] == 1
    assert report["status"] == "inconclusive"


def test_undecodable_host_text_is_inconclusive(tmp_path):
    path = write_wave(tmp_path / "bad-encoding.wav", [(1, -1)] * 2048)
    host = tmp_path / "bad-encoding.log"
    host.write_bytes((XA2 + snapshot(wav=2048)).encode("utf-8") + b"\xff\n")
    _, _, report = analyze(path, host_log_path=host)
    assert report["undecodable_lines"] == 1
    assert report["status"] == "inconclusive"


def test_device_stall_is_evidence_even_with_zero_snapshot_counters(tmp_path):
    path = write_wave(tmp_path / "stall.wav", [(1, -1)] * 2048)
    host = tmp_path / "stall.log"
    host.write_text(
        XA2 + snapshot(wav=2048) + "[APU] audio device stopped consuming (2048 samples queued "
        "for 200 ms): the host clock paces the APU and periods are dropped\n",
        encoding="utf-8",
    )
    records, _, report = analyze(path, host_log_path=host)
    assert report["status"] == "events_detected"
    assert report["device_stopped_notices"] == 1
    assert len(records_of(records, "host_device_stopped")) == 1


def run_cli(*args):
    return subprocess.run(
        [sys.executable, str(ROOT / "tools" / "audio_analysis.py"), *map(str, args)],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )


def test_cli_streams_json_lines_and_checks_arguments(tmp_path):
    path = write_wave(tmp_path / "cli.wav", [(100, -100)] * 40)
    result = run_cli(path, "--active", "music:0:40", "--min-zero-run", "1")
    assert result.returncode == 0, result.stderr
    records = [json.loads(line) for line in result.stdout.splitlines()]
    assert records[0]["type"] == "audio_metadata"
    assert records_of(records, "host_summary")[0]["status"] == "inconclusive"
    assert records[-1]["type"] == "audio_summary"
    assert records[-1]["frames"] == 40

    for args in ((path, "--active", "0:41"), (path, "--min-zero-run", "0"),
                 (path, "--host-log", tmp_path / "missing.log"), (tmp_path / "missing.wav",)):
        failed = run_cli(*args)
        assert failed.returncode != 0
        assert "Error:" in failed.stderr
        assert not failed.stdout
