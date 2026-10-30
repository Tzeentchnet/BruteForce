import os
import subprocess

import pytest


CRASH_EXE = os.environ.get("BRUTEFORCE_TEST_CRASH")
pytestmark = pytest.mark.skipif(
    not CRASH_EXE, reason="set BRUTEFORCE_TEST_CRASH to the native crash fixture"
)


def test_unhandled_exception_is_reported_and_chained(monkeypatch):
    monkeypatch.delenv("BRUTEFORCE_CRASH_STACK", raising=False)
    result = subprocess.run(
        [CRASH_EXE], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0xC0000005
    assert "[CRASH] exception 0xC0000005" in result.stderr
    assert "read address 0x1234" in result.stderr
    assert "eax=ABCDEF12" in result.stderr
    assert "[esp+0] 0x00002345" in result.stderr
    assert "guest stack candidates (not a frame walk)" in result.stderr
    assert "raw guest stack" not in result.stderr
    assert "[TEST] previous exception filter called" in result.stderr
    frames = result.stderr.split("  native stack:\n", 1)[1].split(
        "  guest stack candidates", 1
    )[0].strip().splitlines()
    assert len(frames) >= 2
    assert frames[0].strip().split()[0] != frames[1].strip().split()[0]


def test_guest_stack_with_zero_memory_offset():
    result = subprocess.run(
        [CRASH_EXE, "--zero-offset"],
        capture_output=True, text=True, check=False, timeout=15,
    )
    assert result.returncode == 0xC0000005
    assert "[esp+0] 0x00002345" in result.stderr


def test_raw_guest_stack_is_opt_in(monkeypatch):
    monkeypatch.setenv("BRUTEFORCE_CRASH_STACK", "1")
    result = subprocess.run(
        [CRASH_EXE], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0xC0000005
    assert "raw guest stack (may contain guest data)" in result.stderr
    assert "[esp+0] 0x00002345" in result.stderr
    assert "[esp+4] 0x00000000" in result.stderr
