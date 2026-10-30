import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
MEMORY_STATS_EXE = os.environ.get("BRUTEFORCE_TEST_MEMORY_STATS")
pytestmark = pytest.mark.skipif(
    not MEMORY_STATS_EXE, reason="set BRUTEFORCE_TEST_MEMORY_STATS to the native memory fixture"
)


@pytest.mark.parametrize("mode", ["retail", "devkit", "extended", "retained"])
def test_guest_memory_statistics(mode):
    xbe = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"
    result = subprocess.run(
        [MEMORY_STATS_EXE, str(xbe), mode],
        capture_output=True, text=True, check=False, timeout=30,
    )
    assert result.returncode == 0, result.stderr
    assert "guest memory statistics passed" in result.stdout
