import os
import subprocess

import pytest


READ_TRACE_EXE = os.environ.get("BRUTEFORCE_TEST_READ_TRACE")
pytestmark = pytest.mark.skipif(
    not READ_TRACE_EXE, reason="set BRUTEFORCE_TEST_READ_TRACE to the native read fixture"
)


def run_read(mode, trace=False):
    env = os.environ.copy()
    env.pop("RECOMP_READ_TRACE", None)
    if trace:
        env["RECOMP_READ_TRACE"] = "1"
    return subprocess.run(
        [READ_TRACE_EXE, mode], env=env, capture_output=True, text=True, check=False, timeout=15
    )


def test_successful_reads_are_quiet_by_default():
    result = run_read("success")
    assert result.returncode == 0, result.stderr
    assert "[READ]" not in result.stderr


def test_successful_read_bytes_require_explicit_tracing():
    result = run_read("success", trace=True)
    assert result.returncode == 0, result.stderr
    assert "[READ]" in result.stderr
    assert "41 42 43 44" in result.stderr


def test_failed_reads_are_reported_without_guest_bytes():
    result = run_read("failure")
    assert result.returncode == 0, result.stderr
    assert "[READ]" in result.stderr
    assert "st=0xC0000001" in result.stderr
    assert "41 42 43 44" not in result.stderr
