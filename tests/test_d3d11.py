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
    ("cpu", "gpu-direct-warp"),
    pytest.param("cpu", "gpu-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("cpu", "gpu-direct-hardware", marks=pytest.mark.skipif(
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


def render_frames(mode, output, frame_count):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), mode, str(output)],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode == 0, result.stderr
    data = output.read_bytes()
    frame_bytes = SIZE * SIZE * 4
    assert len(data) == frame_count * frame_bytes
    return [data[i:i + frame_bytes] for i in range(0, len(data), frame_bytes)], result


def rgb_at(frame, x, y):
    offset = (y * SIZE + x) * 4
    return frame[offset + 2], frame[offset + 1], frame[offset]


def rgba_at(frame, x, y):
    offset = (y * SIZE + x) * 4
    return frame[offset + 2], frame[offset + 1], frame[offset], frame[offset + 3]


@pytest.mark.parametrize("mode", [
    "gpu-clear-warp",
    "gpu-clear-vsh-warp",
    "gpu-clear-vertical-warp",
    pytest.param("gpu-clear-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("gpu-clear-vsh-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_rectangular_clears_preserve_channels_depth_stencil_and_draw_state(mode, tmp_path):
    frames, result = render_frames(mode, tmp_path / "clear.raw", 16)
    assert "rectangular clear frames 16" in result.stdout
    if "vsh" in mode:
        checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
        assert checks and all(int(differ) == 0 for _, differ in checks), result.stderr

    black, green = (0, 0, 0, 255), (0, 255, 0, 255)
    for y in range(SIZE):
        for x in range(SIZE):
            in_depth_rect = 16 <= x < 32 and 16 <= y < 32
            in_channels = 10 <= x < 20 and 20 <= y < 30
            in_green_channel = 20 <= x < 30 and 20 <= y < 30
            masked = ((0xCC, 0x20, 0x77, 0x66) if in_channels else
                      (0x10, 0x44, 0x30, 0x7F) if in_green_channel else
                      (0x10, 0x20, 0x30, 0x7F))
            small_window = x < 4 and y < 4
            red_rect = 10 <= x < 20 and 10 <= y < 20
            blue_rect = 40 <= x < 48 and 40 <= y < 48
            expected = [
                (0x11, 0x22, 0x33, 0xAA),
                black if y < 8 or y >= 56 else green,
                masked,
                (0x55, *masked[1:]),
                (0xF0, 0xA0, 0, 255) if x >= 60 and y >= 60 else (0x10, 0x10, 0x10, 255),
                green,
                black if in_depth_rect else green,
                green,
                green if in_depth_rect else black,
                black if in_depth_rect else green,
                (0xEE, 0xEE, 0xEE, 255) if 6 <= x < 14 and 10 <= y < 16
                else (0x22, 0x33, 0x44, 255),
                (0xEE, 0xEE, 0xEE, 255) if 6 <= x < 14 and 5 <= y < 8
                else (0x22, 0x33, 0x44, 255),
                (0, 0, 255, 255) if blue_rect else (0x33, 0x44, 0x55, 255),
                black if in_depth_rect else green,
                green if small_window else (255, 0, 0, 255) if red_rect else black,
                (255, 0, 0, 255) if small_window else (0, 0, 255, 255) if blue_rect else black,
            ]
            for frame, color in enumerate(expected):
                assert rgba_at(frames[frame], x, y) == color, (mode, frame, x, y, color)


@pytest.mark.parametrize(("cpu_mode", "gpu_mode"), [
    ("cpu-clip", "gpu-clip-warp"),
    ("cpu-clip-vsh", "gpu-clip-vsh-warp"),
    pytest.param("cpu-clip", "gpu-clip-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("cpu-clip-vsh", "gpu-clip-vsh-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_window_clip_on_base_extended_and_gpu_vertex_paths(tmp_path, cpu_mode, gpu_mode):
    cpu, _ = render_frames(cpu_mode, tmp_path / "cpu-clip.raw", 12)
    gpu, result = render_frames(gpu_mode, tmp_path / "gpu-clip.raw", 12)
    assert "window clip frames 12" in result.stdout
    if "vsh" in gpu_mode:
        checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
        assert checks and all(int(differ) == 0 for _, differ in checks), result.stderr

    black, white = (0, 0, 0), (255, 255, 255)
    for y in range(SIZE):
        for x in range(SIZE):
            first = 4 <= x <= 11 and 4 <= y <= 11
            second = 20 <= x <= 27 and 4 <= y <= 11
            eight = 20 <= y <= 22 and any(2 + 7 * i <= x <= 4 + 7 * i for i in range(8))
            expected = [
                white,
                (0x11, 0x22, 0x33),
                white if first else black,
                white if first or second else black,
                black if first or second else white,
                (255, 0, 0) if first else (0, 255, 0) if second else black,
                white if 8 <= x <= 15 and 5 <= y <= 7 else black,
                white if 8 <= x <= 15 and 10 <= y <= 15 else black,
                white if eight else black,
                black if eight else white,
                black,
                white,
            ]
            for frame, color in enumerate(expected):
                assert rgb_at(cpu[frame], x, y) == color, (cpu_mode, frame, x, y, color)
                assert rgb_at(gpu[frame], x, y) == color, (gpu_mode, frame, x, y, color)


@pytest.mark.parametrize("mode", [
    "gpu-depth-warp",
    "gpu-depth-vsh-warp",
    pytest.param("gpu-depth-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("gpu-depth-vsh-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_depth_alias_shadow_comparisons_and_layout_guards(mode, tmp_path):
    frames, result = render_frames(mode, tmp_path / "depth.raw", 45)
    assert "depth texture frames 45" in result.stdout
    assert "depth texture alias rejected" in result.stderr
    assert "has not been initialized from guest memory or a GPU depth clear" in result.stderr
    assert "depth texture stage mode 3 is unsupported" in result.stderr
    assert "depth texture stage mode 2 is unsupported on base draws" in result.stderr
    assert "depth cube textures are unsupported" in result.stderr
    if "vsh" in mode:
        checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
        assert checks and all(int(differ) == 0 for _, differ in checks), result.stderr
    white_frames = {0, 6, 7, 8, 9, 11, 13, 15, 17, 20, 21, 24, 25, 26, 28, 29, 43}
    for frame, image in enumerate(frames):
        expected = (255, 255, 255) if frame in white_frames else (0, 0, 0)
        expected_alpha = 255 if frame in white_frames or frame >= 30 else 0
        for y in range(2, SIZE - 2):
            for x in range(2, SIZE - 2):
                color = ((255, 255, 255) if (x >= SIZE // 2 if frame == 40
                                             else x < SIZE // 2) else (0, 0, 0)
                         ) if frame in (40, 41) else expected
                alpha = (255 if x >= SIZE // 2 else 0) if frame == 40 else expected_alpha
                assert rgb_at(image, x, y) == color, (mode, frame, x, y, color)
                assert image[(y * SIZE + x) * 4 + 3] == alpha, (mode, frame, x, y)


def test_invalid_depth_texture_option_is_rejected(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-depth-invalid", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_DEPTH_TEXTURE must be 0 or 1" in result.stderr


@pytest.mark.parametrize("mode", [
    "gpu-stencil-warp",
    "gpu-stencil-vsh-warp",
    pytest.param("gpu-stencil-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("gpu-stencil-vsh-hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_packed_depth_alias_tracks_gpu_stencil_and_all_color_channels(mode, tmp_path):
    frames, result = render_frames(mode, tmp_path / "stencil.raw", 13)
    assert "packed depth frames 13" in result.stdout
    assert "depth texture alias rejected" in result.stderr
    assert "has not been initialized from guest memory or a GPU depth clear" in result.stderr
    if "vsh" in mode:
        checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
        assert checks and int(checks[-1][0]) > 0
        assert all(int(differ) == 0 for _, differ in checks), result.stderr
    for y in range(2, SIZE - 2):
        for x in range(2, SIZE - 2):
            left = x < SIZE // 2
            expected = [
                (0x12, 0x34, 0x56, 0x00) if left else (0x89, 0xAB, 0xCD, 0x5F),
                (0xC0, 0x40, 0x80, 0x7B),
                (0xC0, 0x40, 0x80, 0x5F if left else 0x7B),
                (0xC0, 0x40, 0x80, 0x5F if left else 0x7B),
                (0xC0, 0x40, 0x80, 0x5F if left else 0x7B),
                (0xC0, 0x40, 0x80, 0x33 if left else 0x7B),
                (0xC0, 0x40, 0x80, 0x57),
                (0x11, 0x22, 0x33, 0x44),
                (0, 0, 0, 255),
                (0, 0, 0, 255),
                (0, 0, 0, 255),
                (0x44, 0x55, 0x66, 0xFF),
                (0x44, 0x55, 0x66, 0xFF),
            ]
            for frame, color in enumerate(expected):
                actual = rgba_at(frames[frame], x, y)
                assert max(abs(a - b) for a, b in zip(actual, color)) <= 1, (
                    mode, frame, x, y, actual, color
                )
                if frame != 6:
                    assert actual[3] == color[3], (mode, frame, x, y, actual, color)


def test_invalid_stencil_alias_option_is_rejected(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-stencil-invalid", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_STENCIL_ALIAS must be 0 or 1" in result.stderr


def test_invalid_direct_presentation_option_is_rejected(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-direct-invalid", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_DIRECT_PRESENT must be 0 or 1" in result.stderr


@pytest.mark.parametrize("mode", ["guard", "guard-invalid"])
def test_surface_guard_page_exclusion(mode, tmp_path):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("RECOMP_") and not key.startswith("BRUTEFORCE_SURFACE_GUARD")}
    report = tmp_path / "guard.csv"
    result = subprocess.run(
        [D3D11_EXE, str(XBE), mode, str(report)],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode == 0, result.stderr
    if mode == "guard-invalid":
        assert "BRUTEFORCE_SURFACE_GUARD_EXCLUDE_PAGE requires" in result.stderr
    else:
        assert "surface guard exclusion passed" in result.stdout
        contents = report.read_text()
        assert re.search(r"# EXCLUDED_PAGE,[0-9A-F]{8},4096: accesses on this page are not observed",
                         contents)
        assert re.search(r", [1-9]\d* faults,", contents), contents
        assert re.search(r",[^,\n]+\+0x[0-9a-f]+$", contents, re.MULTILINE), contents
