import os
import subprocess

import pytest


PUSHBUFFER_EXE = os.environ.get("BRUTEFORCE_TEST_PUSHBUFFER")
pytestmark = pytest.mark.skipif(
    not PUSHBUFFER_EXE, reason="set BRUTEFORCE_TEST_PUSHBUFFER to the native pushbuffer fixture"
)


def test_segment_packets_and_bounds():
    result = subprocess.run(
        [PUSHBUFFER_EXE], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0, result.stderr
    assert "pushbuffer segment decoding passed" in result.stdout
    assert "[PB] bad target" in result.stderr
    assert "[PB] incomplete segment packet" in result.stderr
    assert "without RETURN" in result.stderr
    assert "[PB] invalid segment" in result.stderr
    assert "has no observed JUMP" in result.stderr


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("nested", "nested CALL"),
        ("orphan-return", "RETURN without CALL"),
        ("bad-jump", "bad target"),
        ("bad-return", "bad target"),
        ("bad-put", "PUT 0x04000000 is outside physical RAM"),
    ],
)
def test_invalid_dma_stream_stops_without_consuming_put(mode, message):
    result = subprocess.run(
        [PUSHBUFFER_EXE, mode], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode != 0
    assert message in result.stderr
    assert "pusher stopped without consuming PUT" in result.stderr
