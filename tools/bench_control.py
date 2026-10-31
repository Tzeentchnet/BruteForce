"""Benchmark readiness, isolated WAV capture, and process-exit checks."""

import json
from pathlib import Path
import re
import signal
import struct
import subprocess
import uuid


class BenchControlError(Exception):
    pass


class ManualReadiness:
    """A per-attempt operator assertion, not an inferred title-state signal."""

    def __init__(self, out_dir, state, timeout):
        self.out_dir = Path(out_dir)
        self.state = state
        self.timeout = timeout
        self.token = uuid.uuid4().hex
        self.ready_file = self.out_dir / "ready.txt"
        self.bad_since = None
        request = {
            "state": state, "token": self.token, "timeout_seconds": timeout,
            "ready_file": str(self.ready_file),
            "meaning": "Operator confirms this exact visible state; it is not machine verified.",
        }
        with (self.out_dir / "ready.request.json").open("x", encoding="utf-8") as stream:
            json.dump(request, stream, indent=2)
            stream.write("\n")

    def instruction(self):
        path = str(self.ready_file).replace("'", "''")
        return (f"Navigate to {self.state!r}, then in another PowerShell run:\n"
                f"  Set-Content -LiteralPath '{path}' -Value '{self.token}' -Encoding ascii")

    def poll(self, elapsed):
        if self.ready_file.is_file():
            try:
                token = self.ready_file.read_text(encoding="utf-8-sig").strip()
            except (OSError, UnicodeError):
                token = ""
            if token == self.token:
                return True
            if self.bad_since is None:
                self.bad_since = elapsed
            elif elapsed - self.bad_since >= 1:
                raise BenchControlError(
                    f"invalid readiness token in {self.ready_file}; use the printed command")
        else:
            self.bad_since = None
        if elapsed >= self.timeout:
            raise BenchControlError(
                f"readiness timed out after {self.timeout:g} s waiting for operator state "
                f"{self.state!r} in {self.ready_file}")
        return False


def audio_enabled(env):
    """The host checks variable presence, including a value of '0'."""
    return any(key.upper() == "RECOMP_AC97_READY" for key in env)


def reserve_audio_capture(out_dir, env):
    """Reserve a unique path before the runtime opens its WAV with fopen(wb)."""
    if not audio_enabled(env):
        return None
    for key in env:
        if key.upper() in ("RECOMP_AUDIO_WAV", "RECOMP_AUDIO_WAV_SECS"):
            raise BenchControlError(
                "managed audio capture owns RECOMP_AUDIO_WAV and RECOMP_AUDIO_WAV_SECS")
    path = Path(out_dir) / f"audio-{uuid.uuid4().hex}.wav"
    try:
        with path.open("xb"):
            pass
    except OSError as exc:
        raise BenchControlError(f"cannot reserve non-overwriting WAV path {path}: {exc}") from exc
    env["RECOMP_AUDIO_WAV"] = str(path)
    return path


def validate_wav_capture(path, stderr_path):
    """Require the runtime's close marker and an exact, nonempty PCM RIFF."""
    path, stderr_path = Path(path), Path(stderr_path)
    try:
        size = path.stat().st_size
        with path.open("rb") as stream:
            header = stream.read(44)
        log = stderr_path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise BenchControlError(f"missing or unreadable WAV capture: {exc}") from exc
    if len(header) != 44:
        raise BenchControlError(f"WAV capture has a missing or truncated header: {path}")
    (riff, riff_size, wave_tag, fmt, fmt_size, pcm, channels, rate, byte_rate,
     block_align, bits, data_tag, data_size) = struct.unpack("<4sI4s4sIHHIIHH4sI", header)
    if (riff, wave_tag, fmt, fmt_size, pcm, channels, rate, byte_rate,
            block_align, bits, data_tag) != (
            b"RIFF", b"WAVE", b"fmt ", 16, 1, 2, 48000, 192000, 4, 16, b"data"):
        raise BenchControlError(f"WAV capture is not 48 kHz stereo 16-bit PCM: {path}")
    if data_size == 0 or data_size % block_align:
        raise BenchControlError(f"WAV capture is empty or contains a partial sample frame: {path}")
    if size != 44 + data_size or riff_size != size - 8:
        raise BenchControlError(
            f"WAV capture has truncated data, a stale header, or unexpected trailing bytes: {path}")
    markers = re.findall(r"\[AUDIO-WAV\] closed: (\d+) samples\b", log)
    if "[AUDIO-WAV] writing " not in log or len(markers) != 1:
        raise BenchControlError(f"WAV capture lacks the runtime's explicit open/close markers: {path}")
    frames = data_size // block_align
    if int(markers[0]) != frames:
        raise BenchControlError(f"WAV close marker does not match the payload length: {path}")
    return {
        "path": str(path), "finalized": True, "sample_frames": frames,
        "seconds": round(frames / rate, 3), "rate": rate, "channels": channels,
        "bits": bits, "size_bytes": size,
        "scope": "APU output before host-device acceptance; playback is not verified",
    }


def shutdown_process(process, timeout=10):
    """Request the runtime's console handler, then record any forced cleanup."""
    result = {
        "method": None, "forced": False, "exit_code": None, "error": None,
        "scope": "console-control process exit; guest threads are not cleanly joined",
    }
    if process.poll() is not None:
        result.update(method="already_exited", exit_code=process.returncode,
                      error=f"process exited before the shutdown request (code {process.returncode})")
        return result
    event = getattr(signal, "CTRL_BREAK_EVENT", None)
    if event is None:
        result["error"] = "console-control shutdown is unavailable on this platform"
    else:
        try:
            process.send_signal(event)
            result["method"] = "ctrl_break"
        except (OSError, ValueError) as exc:
            result["error"] = f"cannot request console-control shutdown: {exc}"
    if result["error"] is None:
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            result["error"] = f"console-control shutdown timed out after {timeout:g} s"
    if process.poll() is None:
        result["forced"] = True
        process.kill()
        process.wait()
    result["exit_code"] = process.returncode
    if (result["error"] is None and
            process.returncode not in (0xC000013A, -1073741510)):
        result["error"] = f"unexpected console-control exit code {process.returncode}"
    return result
