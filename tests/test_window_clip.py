import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
WINDOW_CLIP_EXE = os.environ.get("BRUTEFORCE_TEST_WINDOW_CLIP")
pytestmark = pytest.mark.skipif(
    not WINDOW_CLIP_EXE, reason="set BRUTEFORCE_TEST_WINDOW_CLIP to the native clip fixture"
)


def run_clip(mode, trace=None):
    xbe = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"
    env = os.environ.copy()
    env.pop("RECOMP_FRAME_TRACE", None)
    env.pop("RECOMP_FRAME_TRACE_METHODS", None)
    if trace is not None:
        env["RECOMP_FRAME_TRACE"] = str(trace)
    return subprocess.run(
        [WINDOW_CLIP_EXE, str(xbe), mode], capture_output=True, text=True,
        check=False, timeout=30, env=env,
    )


@pytest.mark.parametrize("mode", ["enabled", "disabled"])
def test_window_clip_and_rasterization(mode):
    result = run_clip(mode)
    assert result.returncode == 0, result.stderr
    assert "window clipping tests passed" in result.stdout


def test_dxt_cache_decodes_updated_blocks():
    result = run_clip("texture-cache")
    assert result.returncode == 0, result.stderr
    assert "texture cache tests passed" in result.stdout


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("invalid", "RECOMP_WINDOW_CLIP must be 0 or 1"),
        ("backend-before", "registered render back ends do not receive window clip state"),
        ("backend-after", "registered render back ends do not receive window clip state"),
    ],
)
def test_unsupported_configuration_fails_explicitly(mode, message):
    result = run_clip(mode)
    assert result.returncode != 0
    assert message in result.stderr


@pytest.mark.parametrize("mode", ["cull-on", "cull-off", "cull-unset", "cull-clip"])
def test_face_culling_and_triangle_strips(mode):
    result = run_clip(mode)
    assert result.returncode == 0, result.stderr
    assert "face culling tests passed" in result.stdout


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("cull-invalid", "RECOMP_CULL_FACE must be 0 or 1"),
        ("cull-invalid-face", "invalid cull face"),
        ("cull-invalid-front", "invalid front face"),
        ("cull-backend-before", "registered render back ends are unsupported"),
        ("cull-backend-after", "registered render back ends are unsupported"),
    ],
)
def test_unsupported_face_culling_fails_explicitly(mode, message):
    result = run_clip(mode)
    assert result.returncode != 0
    assert message in result.stderr


@pytest.mark.parametrize(
    "mode",
    ["near-on", "near-hud", "near-off", "near-unset", "near-no-z", "near-clip", "near-cull"],
)
def test_programmable_near_plane(mode):
    result = run_clip(mode)
    assert result.returncode == 0, result.stderr
    assert "programmable near-plane tests passed" in result.stdout


@pytest.mark.parametrize(("mode", "plane"), [("near-trace-off", "0.5"), ("near-trace-on", "0")])
def test_programmable_near_plane_trace(mode, plane, tmp_path):
    trace = tmp_path / "trace.flag"
    trace.write_text("trace", encoding="ascii")
    result = run_clip(mode, trace)
    assert result.returncode == 0, result.stderr
    assert f"[FTRACE]     near c59_z 0.5 selected_z {plane}" in result.stderr
    assert "[FTRACE] end of frame" in result.stderr


@pytest.mark.parametrize(
    ("mode", "message"),
    [
        ("near-invalid", "RECOMP_PROGRAM_NEAR_ZERO must be 0, 1, or hud"),
        ("near-conflict", "RECOMP_PROGRAM_NEAR_ZERO requires RECOMP_CLIP_NEAR_Z"),
        ("near-backend-before", "requires the software rasterizer or a capable extended"),
        ("near-backend-after", "requires the software rasterizer or a capable extended"),
    ],
)
def test_unsupported_programmable_near_plane_fails_explicitly(mode, message):
    result = run_clip(mode)
    assert result.returncode != 0
    assert message in result.stderr
