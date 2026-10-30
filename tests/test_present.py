import os
import subprocess

import pytest


PRESENT_EXE = os.environ.get("BRUTEFORCE_TEST_PRESENT")
SHARED_EXE = os.environ.get("BRUTEFORCE_TEST_GPU_SHARED_PRESENT")
HARDWARE = os.environ.get("BRUTEFORCE_TEST_D3D11_HARDWARE") == "1"


@pytest.mark.skipif(
    not PRESENT_EXE, reason="set BRUTEFORCE_TEST_PRESENT to the native presentation fixture"
)
def test_presentation_snapshots_are_consistent():
    result = subprocess.run(
        [PRESENT_EXE], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0, result.stderr


@pytest.mark.skipif(
    not SHARED_EXE,
    reason="set BRUTEFORCE_TEST_GPU_SHARED_PRESENT to the native shared presentation fixture",
)
@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_shared_gpu_presentation_preserves_frames_and_fallbacks(device):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [SHARED_EXE, device], capture_output=True, text=True, check=False, timeout=30, env=env
    )
    assert result.returncode == 0, result.stderr
    assert "shared presentation protocol passed" in result.stdout
