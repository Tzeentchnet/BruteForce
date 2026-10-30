import os
from pathlib import Path
import re
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
D3D11_EXE = os.environ.get("BRUTEFORCE_TEST_D3D11")
HARDWARE = os.environ.get("BRUTEFORCE_TEST_D3D11_HARDWARE") == "1"
pytestmark = pytest.mark.skipif(
    not D3D11_EXE, reason="set BRUTEFORCE_TEST_D3D11 to the native D3D11 parity fixture"
)
XBE = ROOT / ".deps" / "xboxrecomp" / "tools" / "conformance" / "test.xbe"
SIZE = 64


def render(mode, output):
    env = os.environ.copy()
    for key in [k for k in env if k.startswith("RECOMP_")]:
        del env[key]
    result = subprocess.run([D3D11_EXE, str(XBE), mode, str(output)], capture_output=True,
                            text=True, check=False, timeout=120, env=env)
    assert result.returncode == 0, result.stderr
    if mode == "gpu-vsh":
        checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
        assert checks and int(checks[-1][0]) >= 3, "every GPU program batch is verified"
        assert all(int(differ) == 0 for _, differ in checks), result.stderr
    data = output.read_bytes()
    assert len(data) == SIZE * SIZE * 4
    zpass = re.findall(r"^zpass (\d+)$", result.stdout, re.MULTILINE)
    assert zpass == ["1296"], "visibility test counts the 36x36 quad's pixels"
    counts = "16 32 16 16 0 1120" if mode.startswith("gpu") else "20 40 20 20 0 1400"
    assert f"visibility {counts}" in result.stdout
    return data


@pytest.mark.parametrize(("cpu_mode", "gpu_mode"), [
    ("cpu", "gpu"),                  # fixed function, transformed on the CPU
    ("cpu-vsh", "gpu-cpuvsh"),       # vertex program on the CPU, drawn on the GPU
    ("cpu-vsh", "gpu-vsh"),          # vertex program on the GPU (draw_vsh)
    ("cpu", "gpu-software-texture"),
    ("cpu", "gpu-async"),
    pytest.param("cpu", "gpu-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("cpu", "gpu-async-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_gpu_matches_cpu_reference(tmp_path, cpu_mode, gpu_mode):
    cpu = render(cpu_mode, tmp_path / "cpu.raw")
    gpu = render(gpu_mode, tmp_path / "gpu.raw")
    diffs = []
    for i in range(SIZE * SIZE):
        d = max(abs(a - b) for a, b in zip(cpu[4 * i:4 * i + 3], gpu[4 * i:4 * i + 3]))
        if d > 2:
            diffs.append((i % SIZE, i // SIZE))
    # Known difference: pixels whose centre lies exactly on a triangle edge
    # (the CPU rasteriser includes both sides; D3D's top-left rule does not).
    # The fixture has two such diagonal edges, about 50 pixels; everything
    # else, including the textured, blended, depth-tested, render-to-texture
    # and rewritten-texture regions, must match within 2/255.
    assert len(diffs) <= 64, diffs
    for x, y in diffs:
        on_triangle_edge = x + y == 85               # the blended triangle
        on_target_edge = 6 <= x <= 22 and 42 <= y <= 62   # the sampled render target
        assert on_triangle_edge or on_target_edge, (x, y)


def test_gpu_scene_has_expected_content(tmp_path):
    gpu = render("gpu", tmp_path / "gpu.raw")

    def pixel(x, y):
        b, g, r, _ = gpu[4 * (y * SIZE + x):4 * (y * SIZE + x) + 4]
        return r, g, b

    assert pixel(1, 1) == (0x10, 0x20, 0x40), "cleared background"
    r, g, b = pixel(4, 60)
    assert r > 200 and g < 40, "render target's red, sampled without readback"
    assert pixel(46, 4) in ((255, 255, 255), (0, 0, 0)), "rewritten texture is black and white"
    assert pixel(46, 24) == (255, 0, 0), "original palette entry"
    assert pixel(56, 24) == (0, 255, 0), "palette rewrite in the same frame"


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_gpu_texture_decode_matches_cpu(device, tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), f"textures-{device}", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode == 0, result.stderr
    assert "texture cases 140" in result.stdout


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_native_bc_upload_matches_cpu_with_stale_pitch_and_cube_mips(device, tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    images = []
    for mode in ("cpu-bc", f"gpu-bc-{device}"):
        output = tmp_path / f"{mode}.raw"
        result = subprocess.run(
            [D3D11_EXE, str(XBE), mode, str(output)],
            capture_output=True, text=True, check=False, timeout=120, env=env,
        )
        assert result.returncode == 0, result.stderr
        assert "BC scene drawn" in result.stdout
        data = output.read_bytes()
        assert len(data) == SIZE * SIZE * 4
        images.append(data)
        if mode.startswith("gpu"):
            for fmt in ("0C", "0E", "0F"):
                assert re.search(rf"\[GPU-TEX\] new BC .*format=0x{fmt} "
                                 r"16x16 pitch=0 faces=1 levels=1", result.stderr), result.stderr
                assert re.search(rf"\[GPU-TEX\] new BC .*format=0x{fmt} "
                                 r"16x16 pitch=0 faces=6 levels=5", result.stderr), result.stderr

    colors = [(255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255),
              (255, 255, 0), (255, 0, 255), (0, 255, 255)]
    for row, alpha in enumerate((255, 170, 128)):
        for column, rgb in enumerate(colors):
            left = 1 if column == 0 else 10 + (column - 1) * 9
            for y in range(3 + row * 12, 8 + row * 12):
                for x in range(left + 1, left + 6):
                    offset = (y * SIZE + x) * 4
                    pixels = [(data[offset + 2], data[offset + 1], data[offset], data[offset + 3])
                              for data in images]
                    assert pixels[0] == (*rgb, alpha), (row, column, x, y, pixels)
                    assert pixels[1] == pixels[0], (row, column, x, y, pixels)
