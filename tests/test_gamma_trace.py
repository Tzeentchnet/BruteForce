import os
import re
import subprocess

import pytest


FENCE_EXE = os.environ.get("BRUTEFORCE_TEST_FENCE")
pytestmark = pytest.mark.skipif(
    not FENCE_EXE, reason="set BRUTEFORCE_TEST_FENCE to the native fence fixture"
)


def run_gamma(mode):
    env = os.environ.copy()
    env.pop("BRUTEFORCE_DAC_TRACE", None)
    return subprocess.run(
        [FENCE_EXE, mode], capture_output=True, text=True, check=False, timeout=15, env=env
    )


def expected_ramp():
    return bytes(range(256)) + bytes(reversed(range(256))) + bytes(
        (i * 17 + 3) & 255 for i in range(256)
    )


def parse_trace(line):
    match = re.fullmatch(
        r"\[DAC-TRACE\] seq=(\d+) t_us=(\d+) miniport_va=([0-9A-F]{8}) base=([0-9A-F]{8}) "
        r"ramp_va=([0-9A-F]{8}) planar_rgb=([0-9a-f]{1536})",
        line,
    )
    assert match, line
    return int(match[1]), int(match[2]), match[3], match[4], match[5], bytes.fromhex(match[6])


@pytest.mark.parametrize("mode", ["gamma-unset", "gamma-disabled", "gamma-disabled-guard"])
def test_disabled_trace_does_not_read_guest_memory(mode):
    result = run_gamma(mode)
    assert result.returncode == 0, result.stderr
    assert "[DAC-TRACE]" not in result.stderr


def test_trace_records_exact_planar_ramp_and_preserves_guest_state():
    result = run_gamma("gamma-valid")
    assert result.returncode == 0, result.stderr
    sequence, timestamp, miniport_va, base, ramp_va, ramp = parse_trace(result.stderr.strip())
    assert sequence == 1
    assert timestamp > 0
    assert miniport_va == "0037E7C8"
    assert base == "FD000000"
    assert ramp_va == "0037F000"
    assert ramp == expected_ramp()


def test_concurrent_traces_are_complete_and_sequenced():
    result = run_gamma("gamma-concurrent")
    assert result.returncode == 0, result.stderr
    traces = [parse_trace(line) for line in result.stderr.splitlines()]
    assert len(traces) == 8
    assert [trace[0] for trace in traces] == list(range(1, 9))
    assert [trace[1] for trace in traces] == sorted(trace[1] for trace in traces)
    assert all(
        trace[2:] == ("0037E7C8", "FD000000", "0037F000", expected_ramp())
        for trace in traces
    )


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("gamma-invalid-switch", "BRUTEFORCE_DAC_TRACE must be 0 or 1"),
        ("gamma-invalid-miniport", "invalid miniport guest range"),
        ("gamma-unmapped-ramp", "cannot read ramp"),
        ("gamma-partial-ramp", "cannot read ramp"),
        ("gamma-outside-ramp", "invalid ramp guest range"),
        ("gamma-guarded-ramp", "guest memory is inaccessible or guarded"),
    ],
)
def test_invalid_trace_inputs_abort_explicitly(mode, message):
    result = run_gamma(mode)
    assert result.returncode != 0
    assert message in result.stderr
