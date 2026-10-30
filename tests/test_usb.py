import os
import subprocess

import pytest


USB_EXE = os.environ.get("BRUTEFORCE_TEST_USB")
pytestmark = pytest.mark.skipif(
    not USB_EXE, reason="set BRUTEFORCE_TEST_USB to the native USB fixture"
)


@pytest.mark.parametrize(
    "mode",
    [
        "disabled",
        "gate-off",
        "safe-points-off",
        "force-on",
        "missing-memory",
        "enumeration-report",
        "missing-tib",
        "protect-first",
        "protect-second",
        "rollback-retry",
        "thread-retry",
        "lifecycle",
        "late-start",
        "gate-timeout",
        "gate-owned",
        "worker-timeout",
        "restore-retry",
        "worker-failure",
    ],
)
def test_usb_lifecycle(mode):
    result = subprocess.run(
        [USB_EXE, mode], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0, result.stderr
