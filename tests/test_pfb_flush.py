import os
import subprocess

import pytest


PFB_FLUSH_EXE = os.environ.get("BRUTEFORCE_TEST_PFB_FLUSH")
pytestmark = pytest.mark.skipif(
    not PFB_FLUSH_EXE,
    reason="set BRUTEFORCE_TEST_PFB_FLUSH to the native PFB flush fixture",
)


def run_fixture(mode):
    return subprocess.run(
        [PFB_FLUSH_EXE, mode],
        capture_output=True,
        text=True,
        check=False,
        timeout=15,
    )


@pytest.mark.parametrize(
    "mode", ["disabled", "normal", "spurious", "timeout", "reuse"]
)
def test_flush_wait(mode):
    result = run_fixture(mode)
    assert result.returncode == 0, result.stderr


def test_invalid_switch_stops():
    result = run_fixture("invalid")
    assert result.returncode != 0
    assert "RECOMP_PFB_FLUSH_WAIT must be 0 or 1" in result.stderr
