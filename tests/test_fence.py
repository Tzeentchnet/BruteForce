import os
import subprocess

import pytest


FENCE_EXE = os.environ.get("BRUTEFORCE_TEST_FENCE")
pytestmark = pytest.mark.skipif(
    not FENCE_EXE, reason="set BRUTEFORCE_TEST_FENCE to the native fence fixture"
)


def run_fence(mode):
    return subprocess.run(
        [FENCE_EXE, mode], capture_output=True, text=True, check=False, timeout=15
    )


@pytest.mark.parametrize(
    "mode",
    ["disabled", "valid", "low-semaphore", "concurrent", "event-valid",
     "event-concurrent", "trap-disabled", "trap-matched", "trap-unmatched"],
)
def test_fence_registration(mode):
    result = run_fence(mode)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("null-device", "Invalid graphics device"),
        ("unaligned-device", "Invalid graphics device"),
        ("overflow-device", "Invalid graphics device"),
        ("unmapped-device", "Invalid graphics device"),
        ("null-semaphore", "Invalid GPU semaphore"),
        ("unaligned-semaphore", "Invalid GPU semaphore"),
        ("outside-semaphore", "Invalid GPU semaphore"),
        ("unmapped-semaphore", "Invalid GPU semaphore"),
        ("readonly-semaphore", "Invalid GPU semaphore"),
        ("changed-semaphore", "device reset with pending releases is unsupported"),
        ("event-wrong-walk", "requires RECOMP_PB_WALK=staged"),
        ("event-no-interrupt", "Invalid graphics interrupt"),
        ("event-wrong-miniport", "Fence event does not match"),
        ("trap-bad-stack", "Invalid software-trap stack"),
    ],
)
def test_invalid_fence_state_aborts(mode, message):
    result = run_fence(mode)
    assert result.returncode != 0
    assert message in result.stderr


@pytest.mark.parametrize(
    ("mode", "message"),
    [("icall", "guest return=00112233"), ("icall-bad-stack", "guest return unavailable")],
)
def test_indirect_call_failure_reports_guest_state(mode, message):
    result = run_fence(mode)
    assert result.returncode != 0
    assert "guest address 0x12345678" in result.stderr
    assert message in result.stderr
