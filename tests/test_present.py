import os
import subprocess

import pytest


PRESENT_EXE = os.environ.get("BRUTEFORCE_TEST_PRESENT")
pytestmark = pytest.mark.skipif(
    not PRESENT_EXE, reason="set BRUTEFORCE_TEST_PRESENT to the native presentation fixture"
)


def test_presentation_snapshots_are_consistent():
    result = subprocess.run(
        [PRESENT_EXE], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0, result.stderr
