import os
import subprocess

import pytest


TEXTURE_MATH_EXE = os.environ.get("BRUTEFORCE_TEST_TEXTURE_MATH")
pytestmark = pytest.mark.skipif(
    not TEXTURE_MATH_EXE, reason="set BRUTEFORCE_TEST_TEXTURE_MATH to the native texture fixture"
)


def test_texture_math_matches_scalar_reference():
    result = subprocess.run(
        [TEXTURE_MATH_EXE], capture_output=True, text=True, check=False, timeout=30
    )
    assert result.returncode == 0, result.stderr
    assert "texture math tests passed" in result.stdout
