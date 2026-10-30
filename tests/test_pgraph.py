import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
PGRAPH_EXE = os.environ.get("BRUTEFORCE_TEST_PGRAPH")
pytestmark = pytest.mark.skipif(
    not PGRAPH_EXE, reason="set BRUTEFORCE_TEST_PGRAPH to the native PGRAPH fixture"
)


def run_pgraph(mode):
    xbe = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"
    return subprocess.run(
        [PGRAPH_EXE, str(xbe), mode], capture_output=True, text=True,
        check=False, timeout=30,
    )


@pytest.mark.parametrize("mode", ["disabled", "normal", "masked-graph", "masked-pmc",
                                  "declined", "stress"])
def test_interrupt_and_event_lifecycle(mode):
    result = run_pgraph(mode)
    assert result.returncode == 0, result.stderr
    if mode != "disabled":
        assert "PGRAPH interrupt and event tests passed" in result.stdout


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("invalid-trap", "unsupported software trap"),
        ("invalid-completion", "completed in an invalid state"),
        ("no-isr", "requires the strict IRQ gate"),
        ("no-gate", "requires the strict IRQ gate"),
        ("no-tib", "timer thread has no TIB"),
        ("uncallable-isr", "connected GPU ISR cannot be called"),
        ("bad-ack", "invalid software-trap acknowledgement"),
        ("nested", "nested software trap"),
    ],
)
def test_invalid_interrupt_state_stops(mode, message):
    result = run_pgraph(mode)
    assert result.returncode != 0
    assert message in result.stderr
