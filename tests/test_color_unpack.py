import os
import subprocess

import pytest


COLOR_UNPACK_EXE = os.environ.get("BRUTEFORCE_TEST_COLOR_UNPACK")
pytestmark = pytest.mark.skipif(
    not COLOR_UNPACK_EXE, reason="set BRUTEFORCE_TEST_COLOR_UNPACK to the native color fixture"
)


def test_color_unpack_matches_scalar_reference():
    result = subprocess.run(
        [COLOR_UNPACK_EXE], capture_output=True, text=True, check=False, timeout=30
    )
    assert result.returncode == 0, result.stderr
    assert "color unpack tests passed" in result.stdout
