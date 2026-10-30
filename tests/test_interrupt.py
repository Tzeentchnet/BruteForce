import os
import subprocess

import pytest


INTERRUPT_EXE = os.environ.get("BRUTEFORCE_TEST_INTERRUPT")
pytestmark = pytest.mark.skipif(
    not INTERRUPT_EXE, reason="set BRUTEFORCE_TEST_INTERRUPT to the native interrupt fixture"
)


def run_interrupt(mode):
    return subprocess.run(
        [INTERRUPT_EXE, mode], capture_output=True, text=True, check=False, timeout=15
    )


@pytest.mark.parametrize("mode", ["valid-dispatch", "valid-device"])
def test_guarded_interrupt_pair_defers_pending_irq(mode):
    result = run_interrupt(mode)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("disabled", "[UNIMPL]"),
        ("gate-off", "requires the strict gate"),
        ("safe-points-off", "requires the strict gate"),
        ("force-on", "requires the strict gate"),
        ("wrong-owner", "requires the strict gate"),
        ("wrong-irql", "requires the strict gate"),
        ("sti-unpaired", "Unexpected XPP STI state"),
        ("pre-masked", "Unexpected XPP CLI state"),
        ("duplicate-cli", "Unexpected XPP CLI state"),
        ("sti-flags-changed", "Unexpected XPP STI state"),
        ("other-address", "[UNIMPL]"),
        ("other-opcode", "[UNIMPL]"),
    ],
)
def test_unsupported_interrupt_context_aborts(mode, message):
    result = run_interrupt(mode)
    assert result.returncode != 0
    assert message in result.stderr
