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


@pytest.mark.parametrize("mode", ["gpu", "gpu-scale2-warp", "gpu-scale2-async"])
def test_gpu_scene_has_expected_content(tmp_path, mode):
    gpu = render(mode, tmp_path / "gpu.raw")

    def pixel(x, y):
        b, g, r, _ = gpu[4 * (y * SIZE + x):4 * (y * SIZE + x) + 4]
        return r, g, b

    assert pixel(1, 1) == (0x10, 0x20, 0x40), "cleared background"
    r, g, b = pixel(4, 60)
    assert r > 200 and g < 40, "render target's red, sampled without readback"
    assert pixel(46, 4) in ((255, 255, 255), (0, 0, 0)), "rewritten texture is black and white"
    assert pixel(46, 24) == (255, 0, 0), "original palette entry"
    assert pixel(56, 24) == (0, 255, 0), "palette rewrite in the same frame"


def render_compatibility_fixture(mode, output, **options):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env.update(options)
    return subprocess.run(
        [D3D11_EXE, str(XBE), mode, str(output)],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )


def fixture_pixel(data, x, y):
    b, g, r, a = data[4 * (y * SIZE + x):4 * (y * SIZE + x) + 4]
    return r, g, b, a


def assert_vsh_verification(result, expected_fetch=None):
    reports = re.findall(
        r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ; "
        r"(\d+) vertices, (\d+) differ, (\d+) skipped \(odd inputs\)",
        result.stderr,
    )
    assert reports, result.stderr
    for batches, bad_batches, vertices, bad_vertices, skipped in reports:
        assert int(batches) > 0 and int(vertices) > 0
        assert int(bad_batches) == int(bad_vertices) == int(skipped) == 0, result.stderr
    assert "differs in" not in result.stderr
    if expected_fetch:
        counts = re.search(
            r"programs on the GPU: (\d+) raw, (\d+) gathered; (\d+) declined",
            result.stderr,
        )
        assert counts, result.stderr
        raw, gathered, declined = map(int, counts.groups())
        assert declined == 0
        assert (raw > 0 and gathered == 0) if expected_fetch == "raw" else (gathered > 0 and raw == 0)


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("short_size", [3, 4])
@pytest.mark.parametrize("ubyte_size", [3, 4])
@pytest.mark.parametrize("fetch", ["raw", "gathered"])
def test_gpu_vertex_packed_formats_match_interpreter(
    tmp_path, device, short_size, ubyte_size, fetch
):
    suffix = f"vsh-packed-short{short_size}-ubyte{ubyte_size}"
    images = []
    for mode in (f"cpu-{suffix}", f"gpu-{suffix}-{device}"):
        output = tmp_path / f"{mode}.raw"
        result = render_compatibility_fixture(
            mode, output, RECOMP_GPU_VSH_RAW="0" if fetch == "gathered" else "1"
        )
        assert result.returncode == 0, result.stderr
        if mode.startswith("gpu"):
            assert_vsh_verification(result, fetch)
        images.append(output.read_bytes())
    for image in images:
        assert len(image) == SIZE * SIZE * 4
        assert fixture_pixel(image, 4, 8) == (32, 128, 224, 255)
        assert fixture_pixel(image, 40, 40) == (0, 0, 0, 255)


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("fetch", ["raw", "gathered"])
def test_gpu_vertex_relative_constants_match_interpreter(tmp_path, device, fetch):
    expected = [
        (255, 0, 255, 255), (255, 0, 0, 255), (0, 255, 0, 255),
        (0, 0, 255, 255), (255, 255, 0, 255), (0, 255, 255, 255),
        (255, 255, 255, 255), (255, 255, 255, 255),
    ]
    for mode in ("cpu-vsh-relative", f"gpu-vsh-relative-{device}"):
        output = tmp_path / f"{mode}.raw"
        result = render_compatibility_fixture(
            mode, output, RECOMP_GPU_VSH_RAW="0" if fetch == "gathered" else "1"
        )
        assert result.returncode == 0, result.stderr
        if mode.startswith("gpu"):
            assert_vsh_verification(result, fetch)
        image = output.read_bytes()
        assert len(image) == SIZE * SIZE * 4
        for i, color in enumerate(expected):
            assert fixture_pixel(image, 7 + (i % 4) * 16, 7 + (i // 4) * 20) == color


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("enabled", [False, True])
def test_gpu_vertex_culling_matches_cpu_and_strip_winding(tmp_path, device, enabled):
    expected = [(1, 1), (1, 0), (0, 1), (0, 1), (0, 0), (1, 1), (1, 0)]
    if not enabled:
        expected = [(1, 1)] * len(expected)
    switch = "on" if enabled else "off"
    modes = [
        f"cpu-vsh-cull-{switch}", f"gpu-cpuvsh-cull-{switch}-{device}",
        f"gpu-vsh-cull-{switch}-{device}",
    ]
    for mode in modes:
        output = tmp_path / f"{mode}.raw"
        result = render_compatibility_fixture(mode, output)
        assert result.returncode == 0, result.stderr
        if mode.startswith("gpu-vsh"):
            assert_vsh_verification(result, "raw")
            skipped = result.stderr.count("[VSH-VERIFY] skipped fully culled batch:")
            assert skipped == (3 if enabled else 0), result.stderr
        images = output.read_bytes()
        frame_bytes = SIZE * SIZE * 4
        assert len(images) == frame_bytes * len(expected)
        for i, (clockwise, counter) in enumerate(expected):
            frame = images[i * frame_bytes:(i + 1) * frame_bytes]
            assert fixture_pixel(frame, 8, 8) == ((255, 0, 0, 255) if clockwise else (0, 0, 0, 255))
            assert fixture_pixel(frame, 40, 8) == ((0, 255, 0, 255) if counter else (0, 0, 0, 255))
            strip = (0, 0, 255, 255) if clockwise else (0, 0, 0, 255)
            assert fixture_pixel(frame, 8, 40) == strip
            assert fixture_pixel(frame, 20, 52) == strip


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("decoder", ["compute", "software-texture"])
def test_texture_cache_observes_same_frame_and_unsampled_rewrites(tmp_path, device, decoder):
    images = []
    for mode in ("cpu-cache-rewrite", f"gpu-cache-rewrite-{decoder}-{device}"):
        output = tmp_path / f"{mode}.raw"
        result = render_compatibility_fixture(mode, output)
        assert result.returncode == 0, result.stderr
        images.append(output.read_bytes())
    for image in images:
        assert fixture_pixel(image, 5, 5) == (255, 0, 0, 255)
        assert fixture_pixel(image, 19, 5) == (0, 255, 0, 255)
        assert fixture_pixel(image, 5, 21) == (0, 255, 0, 255)
        assert fixture_pixel(image, 19, 21) == (0, 0, 255, 255)


def test_approximate_cache_is_opt_in_and_warns_about_stale_textures(tmp_path):
    output = tmp_path / "approximate-cache.raw"
    result = render_compatibility_fixture(
        "gpu-cache-rewrite", output, RECOMP_GPU_TEX_CACHE_APPROX="1"
    )
    assert result.returncode == 0, result.stderr
    assert "experimental approximate texture cache enabled" in result.stderr
    image = output.read_bytes()
    assert fixture_pixel(image, 19, 5) == (255, 0, 0, 255)
    assert fixture_pixel(image, 19, 21) == (0, 255, 0, 255)


def test_invalid_approximate_cache_option_is_rejected(tmp_path):
    result = render_compatibility_fixture(
        "gpu-cache-rewrite", tmp_path / "unused.raw", RECOMP_GPU_TEX_CACHE_APPROX="invalid"
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_TEX_CACHE_APPROX must be 0 or 1" in result.stderr


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("decoder", ["compute", "software-texture"])
@pytest.mark.parametrize("approximate", ["0", "1"])
def test_unsupported_texture_cache_preserves_other_texture_paths(
        tmp_path, device, decoder, approximate):
    output = tmp_path / "unsupported-cache.raw"
    result = render_compatibility_fixture(
        f"gpu-unsupported-cache-{decoder}-{device}", output,
        RECOMP_GPU_TEX_CACHE_APPROX=approximate,
    )
    assert result.returncode == 0, result.stderr
    assert result.stderr.count("cannot decode texture format 0x2E") == 1
    image = output.read_bytes()
    assert fixture_pixel(image, 5, 5) == fixture_pixel(image, 19, 5)
    assert fixture_pixel(image, 35, 5) == (0, 255, 0, 255)
    assert fixture_pixel(image, 49, 5) == (0, 0, 255, 255)


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("approximate", ["0", "1"])
def test_failed_texture_uploads_remain_retryable_without_using_cache_slots(
        tmp_path, device, approximate):
    output = tmp_path / "failed-upload-cache.raw"
    result = render_compatibility_fixture(
        f"gpu-failed-upload-cache-{device}", output,
        RECOMP_GPU_TEX_CACHE_APPROX=approximate, RECOMP_GPU_TEXLOG="1",
    )
    assert result.returncode == 0, result.stderr
    assert result.stderr.count("texture upload failed") == 4
    assert result.stderr.count("discarded failed upload (1 cache entries)") == 4
    image = output.read_bytes()
    assert fixture_pixel(image, 5, 5) == (0, 255, 0, 255)
    assert fixture_pixel(image, 19, 5) == (0, 255, 0, 255)


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("decoder", ["compute", "software-texture"])
@pytest.mark.parametrize("stage", [0, 1])
def test_short_p8_palettes_end_at_guard_page(tmp_path, device, decoder, stage):
    suffix = f"-stage{stage}" if stage else ""
    images = []
    for mode in (f"cpu-p8{suffix}", f"gpu-p8{suffix}-{decoder}-{device}"):
        output = tmp_path / f"{mode}.raw"
        result = render_compatibility_fixture(mode, output)
        assert result.returncode == 0, result.stderr
        images.append(output.read_bytes())
    expected = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255),
                (255, 255, 0, 255)]
    for image in images:
        assert [fixture_pixel(image, 5 + i * 14, 5) for i in range(4)] == expected


@pytest.mark.parametrize("mode,diagnostic", [
    ("gpu-p8-invalid-index", "P8 index 32 exceeds 32-entry palette"),
    ("gpu-p8-stage1-invalid-index", "P8 index 32 exceeds 32-entry palette"),
    ("gpu-p8-short-allocation", "P8 palette allocation is too short"),
    ("gpu-p8-rewrite-invalid-index", "P8 index 32 exceeds 32-entry palette"),
    ("gpu-p8-rewrite-stage1-invalid-index", "P8 index 32 exceeds 32-entry palette"),
    ("gpu-p8-rewrite-shrink", "P8 index 32 exceeds 32-entry palette"),
    ("gpu-p8-rewrite-stage1-shrink", "P8 index 32 exceeds 32-entry palette"),
    ("gpu-p8-bad-range-unreadable-palette", "P8 palette span at"),
    ("gpu-p8-bad-range-unreadable-texture", "P8 texture span at"),
    ("gpu-p8-bad-range-short-texture", "P8 texture allocation is too short"),
])
def test_invalid_p8_palette_is_reported_before_read(tmp_path, mode, diagnostic):
    result = render_compatibility_fixture(mode, tmp_path / "unused.raw")
    assert result.returncode != 0
    assert diagnostic in result.stderr
    if "unreadable" in mode:
        assert "is not readable" in result.stderr


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("decoder", ["compute", "software-texture"])
@pytest.mark.parametrize("stage", [0, 1])
@pytest.mark.parametrize("approximate", ["0", "1"])
@pytest.mark.parametrize("protected", ["texture", "palette"])
def test_cached_p8_ranges_are_rechecked_after_protection_changes(
        tmp_path, device, decoder, stage, approximate, protected):
    result = render_compatibility_fixture(
        f"gpu-p8-reprotect-{protected}-stage{stage}-{decoder}-{device}",
        tmp_path / "unused.raw", RECOMP_GPU_TEX_CACHE_APPROX=approximate,
    )
    assert result.returncode != 0
    assert f"P8 {protected} span at" in result.stderr
    assert "is not readable" in result.stderr


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("decoder", ["compute", "software-texture"])
@pytest.mark.parametrize("stage", [0, 1])
def test_short_p8_indices_and_palette_rewrite_in_same_frame(tmp_path, device, decoder, stage):
    suffix = f"-stage{stage}" if stage else ""
    for mode in (f"cpu-p8-rewrite{suffix}", f"gpu-p8-rewrite{suffix}-{decoder}-{device}"):
        output = tmp_path / f"{mode}.raw"
        result = render_compatibility_fixture(mode, output)
        assert result.returncode == 0, result.stderr
        image = output.read_bytes()
        assert [fixture_pixel(image, x, 5) for x in (5, 19, 33)] == [
            (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)
        ]


@pytest.mark.parametrize("mode", ["device-alive", "device-unrelated"])
def test_device_health_check_does_not_report_loss_for_unrelated_results(tmp_path, mode):
    output = tmp_path / "device-marker"
    result = render_compatibility_fixture(mode, output)
    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == b"pending completed"
    assert "device lost" not in result.stderr


@pytest.mark.parametrize("mode", [
    "device-result-removed", "device-result-reset", "device-result-hung",
    "device-result-driver", "device-reason-hung", "device-reason-invalid", "device-pending",
])
def test_device_loss_fails_before_synthetic_work_is_completed(tmp_path, mode):
    output = tmp_path / "device-marker"
    result = render_compatibility_fixture(mode, output)
    assert result.returncode != 0
    assert "device lost during synthetic pending GPU work" in result.stderr
    assert "recovery is not implemented" in result.stderr
    assert output.read_bytes() == b"pending"


@pytest.mark.parametrize(("mode", "completed"), [
    ("device-poll-healthy", True),
    ("device-poll-pending", False),
])
def test_visibility_poll_preserves_completion_state(tmp_path, mode, completed):
    output = tmp_path / "device-marker"
    result = render_compatibility_fixture(mode, output)
    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == (b"pending completed" if completed else b"pending")


@pytest.mark.parametrize("mode", ["device-poll-lost", "device-poll-result-removed"])
def test_visibility_poll_reports_loss_without_completing_work(tmp_path, mode):
    output = tmp_path / "device-marker"
    result = render_compatibility_fixture(mode, output)
    assert result.returncode != 0
    assert "device lost during synthetic visibility poll" in result.stderr
    assert output.read_bytes() == b"pending"


def render_with_shader_cache(tmp_path, device, cache, index, enabled="1", vertex_program=False):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env["RECOMP_GPU_PSCACHE"] = enabled
    env["RECOMP_GPU_PSCACHE_DIR"] = str(cache)
    output = tmp_path / f"shader-cache-{index}.raw"
    mode = "gpu-vsh" if vertex_program else "gpu"
    if device == "hardware":
        mode += "-hardware"
    result = subprocess.run(
        [D3D11_EXE, str(XBE), mode, str(output)],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode == 0, result.stderr
    return output.read_bytes(), result.stderr


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_persistent_pixel_shader_cache_preserves_frames_and_recovers_damage(tmp_path, device):
    cache = tmp_path / "shader-\u65e5"
    cache.mkdir()
    baseline, off_log = render_with_shader_cache(tmp_path, device, cache, "off", enabled="0")
    assert "[GPU-PSCACHE]" not in off_log
    assert not list(cache.iterdir())
    cold, cold_log = render_with_shader_cache(tmp_path, device, cache, "cold")
    entries = sorted(cache.glob("ps_*.bin"))
    assert entries and "[GPU-PSCACHE] compiled in " in cold_log
    warm, warm_log = render_with_shader_cache(tmp_path, device, cache, "warm")
    assert "[GPU-PSCACHE] hit in " in warm_log
    assert "[GPU-PSCACHE] compiled in " not in warm_log
    assert baseline == cold == warm

    for index, damage in enumerate(("truncated", "header", "key", "checksum")):
        original = entries[0].read_bytes()
        if damage == "truncated":
            changed = original[:10]
        else:
            changed = bytearray(original)
            offset = {"header": 0, "key": 8, "checksum": -1}[damage]
            changed[offset] ^= 0xFF
        entries[0].write_bytes(changed)
        recovered, recovery_log = render_with_shader_cache(tmp_path, device, cache, f"recovery-{index}")
        assert recovered == baseline
        assert "cache entry; recompiling" in recovery_log or "cache bytecode; recompiling" in recovery_log
        assert "[GPU-PSCACHE] compiled in " in recovery_log
        repaired, repaired_log = render_with_shader_cache(tmp_path, device, cache, f"repaired-{index}")
        assert repaired == baseline
        assert "[GPU-PSCACHE] hit in " in repaired_log
        assert "[GPU-PSCACHE] compiled in " not in repaired_log
    assert not list(cache.glob("*.tmp"))


@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_shader_cache_preserves_gpu_vertex_program_frames(tmp_path, device):
    cache = tmp_path / "cache"
    cache.mkdir()
    baseline, _ = render_with_shader_cache(
        tmp_path, device, cache, "vertex-off", enabled="0", vertex_program=True
    )
    cold, cold_log = render_with_shader_cache(
        tmp_path, device, cache, "vertex-cold", vertex_program=True
    )
    warm, warm_log = render_with_shader_cache(
        tmp_path, device, cache, "vertex-warm", vertex_program=True
    )
    assert baseline == cold == warm
    assert "[GPU-PSCACHE] compiled in " in cold_log
    assert "[GPU-PSCACHE] hit in " in warm_log
    assert "[GPU-PSCACHE] compiled in " not in warm_log


def test_shader_cache_keys_cover_all_compiler_inputs(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env["RECOMP_GPU_PSCACHE"] = "1"
    env["RECOMP_GPU_PSCACHE_DIR"] = str(tmp_path)
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-cache-keys", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode == 0, result.stderr
    assert "shader cache keys passed" in result.stdout


@pytest.mark.parametrize("block", ["locked", "readonly"])
@pytest.mark.parametrize("entry", ["same", "different", "corrupt"])
def test_shader_cache_existing_entry_during_blocked_publication(tmp_path, block, entry):
    cache = tmp_path / "cache"
    cache.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env.update({"RECOMP_GPU_PSCACHE": "1", "RECOMP_GPU_PSCACHE_DIR": str(cache)})
    mode = f"gpu-cache-publish-{block}-{entry}"
    result = subprocess.run(
        [D3D11_EXE, str(XBE), mode, str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode == 0, result.stderr
    assert "shader cache publication fixture passed" in result.stdout
    assert list(cache.glob("ps_*.bin"))
    assert not list(cache.glob("*.tmp"))
    if entry != "same":
        assert "[GPU-PSCACHE] cannot publish cache entry" in result.stderr
        assert "identical entry already present" not in result.stderr
        if entry == "corrupt":
            assert "invalid or truncated cache entry" in result.stderr
    else:
        assert "[GPU-PSCACHE] cannot publish cache entry" not in result.stderr
        assert "identical entry already present" in result.stderr


@pytest.mark.parametrize("exhausted", [False, True])
def test_shader_cache_temporary_name_collisions(tmp_path, exhausted):
    cache = tmp_path / "cache"
    cache.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env.update({"RECOMP_GPU_PSCACHE": "1", "RECOMP_GPU_PSCACHE_DIR": str(cache)})
    mode = "gpu-cache-temp-exhausted" if exhausted else "gpu-cache-temp-retry"
    result = subprocess.run(
        [D3D11_EXE, str(XBE), mode, str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode == 0, result.stderr
    assert "shader cache temporary collision fixture passed" in result.stdout
    assert not list(cache.glob("*.tmp"))
    if exhausted:
        assert "[GPU-PSCACHE] cannot create cache entry" in result.stderr
    else:
        assert "[GPU-PSCACHE] cannot create cache entry" not in result.stderr
        assert "[GPU-PSCACHE] cannot publish cache entry" not in result.stderr


def test_shader_cache_default_directory_and_concurrent_writers(tmp_path):
    local = tmp_path / "local-\u65e5"
    local.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env.update({"RECOMP_GPU_PSCACHE": "1", "LOCALAPPDATA": str(local)})
    processes = []
    outputs = [tmp_path / "concurrent-1.raw", tmp_path / "concurrent-2.raw"]
    for output in outputs:
        processes.append(subprocess.Popen(
            [D3D11_EXE, str(XBE), "gpu", str(output)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env,
        ))
    for process in processes:
        _, stderr = process.communicate(timeout=120)
        assert process.returncode == 0, stderr
        assert "cannot publish cache entry" not in stderr
    cache = local / "BruteForce" / "shader-cache"
    assert list(cache.glob("ps_*.bin"))
    assert not list(cache.glob("*.tmp"))
    assert outputs[0].read_bytes() == outputs[1].read_bytes()
    warm, log = render_with_shader_cache(tmp_path, "warp", cache, "concurrent-warm")
    assert warm == outputs[0].read_bytes()
    assert "[GPU-PSCACHE] hit in " in log
    assert "[GPU-PSCACHE] compiled in " not in log


def test_shader_cache_invalid_setting_and_unavailable_directory_are_reported(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    env["RECOMP_GPU_PSCACHE"] = "invalid"
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu", str(tmp_path / "invalid.raw")],
        capture_output=True, text=True, check=False, timeout=120, env=env,
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_PSCACHE must be 0 or 1" in result.stderr

    baseline, _ = render_with_shader_cache(tmp_path, "warp", tmp_path / "missing", "base", enabled="0")
    fallback, log = render_with_shader_cache(tmp_path, "warp", tmp_path / "missing", "fallback")
    assert fallback == baseline
    assert "RECOMP_GPU_PSCACHE_DIR must be an existing directory" in log
    assert "cache unavailable; shaders will compile normally" in log


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
    assert "short P8 cube cases 4" in result.stdout


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


def render_frames(mode, output, frame_count, extra_env=None):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    if extra_env:
        env.update(extra_env)
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


@pytest.mark.skipif(not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1")
@pytest.mark.parametrize("vertex", ["vsh", "cpuvsh"])
def test_programmable_near_plane_can_hide_hud_and_glow_together(vertex, tmp_path):
    results = {}
    for enabled in (False, True):
        frames, result = render_frames(
            f"gpu-near-hud-{vertex}-hardware", tmp_path / f"near-{enabled}.raw", 2,
            {"RECOMP_PROGRAM_NEAR_ZERO": "1" if enabled else "0"},
        )
        assert "HUD and glow near-plane frames 2" in result.stdout
        assert "[GPU-D3D11] hardware device ready" in result.stderr
        if vertex == "vsh":
            checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
            assert checks and all(int(differ) == 0 for _, differ in checks), result.stderr
        results[enabled] = frames

    for enabled, frames in results.items():
        for index, frame in enumerate(frames):
            visible = enabled or index == 1
            background = (0xA0, 0xA0, 0xA0) if visible else (0x60, 0x60, 0x60)
            assert rgb_at(frame, 32, 32) == background
            assert rgb_at(frame, 10, 10) == ((255, 0, 0) if visible else background)
            assert rgb_at(frame, 50, 10) == (0, 255, 0)
            assert rgb_at(frame, 50, 50) == (
                (0, 0, 255) if not enabled and index == 1 else background
            )


@pytest.mark.parametrize("vertex", ["vsh", "cpuvsh"])
@pytest.mark.parametrize("device", [
    "warp",
    pytest.param("hardware", marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
def test_hud_safe_near_plane_suppresses_zero_depth_additive_glow(vertex, device, tmp_path):
    frames, result = render_frames(
        f"gpu-near-hud-{vertex}-{device}", tmp_path / f"near-hud-{vertex}-{device}.raw", 2,
        {"RECOMP_PROGRAM_NEAR_ZERO": "hud"},
    )
    assert "HUD and glow near-plane frames 2" in result.stdout
    assert "HUD-safe programmable near plane enabled" in result.stderr
    if device == "hardware":
        assert "[GPU-D3D11] hardware device ready" in result.stderr
    for frame in frames:
        assert rgb_at(frame, 32, 32) == (0x60, 0x60, 0x60)
        assert rgb_at(frame, 10, 10) == (255, 0, 0)
        assert rgb_at(frame, 50, 10) == (0, 255, 0)
        assert rgb_at(frame, 50, 50) == (0x60, 0x60, 0x60)


@pytest.mark.parametrize("mode", [
    "gpu-clear-warp",
    "gpu-clear-scale2-warp",
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
    ("cpu-clip", "gpu-clip-scale2-warp"),
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


GLARE_CASES = [
    ((0x20, 0x20, 0x20), 0xFF, 0xC0, False),
    ((0xA0, 0xA0, 0xA0), 0xFF, 0xC0, False),
    ((0xA0, 0xA0, 0xA0), 0xFF, 0x40, True),
    ((0xE0, 0xE0, 0xE0), 0xFF, 0xC0, True),
    ((0xA0, 0xA0, 0xA0), 0xFF, 0xA1, False),
    ((0xA0, 0xA0, 0xA0), 0xFF, 0x9F, True),
    ((0xE0, 0x20, 0x10), 0xFF, 0x60, False),
    ((0xE0, 0x20, 0x10), 0xFF, 0x50, True),
    ((0xA0, 0xA0, 0xA0), 0x80, 0x40, True),
]


def glare_pixels(color, alpha, passes):
    bright = tuple((value * alpha + 127) // 255 if passes else 0 for value in color)
    composite = tuple(min(255, base + glow) for base, glow in zip(color, bright))
    return bright, composite


@pytest.mark.parametrize(("device", "vertex_program"), [
    ("warp", False),
    ("warp", True),
    pytest.param("hardware", False, marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
    pytest.param("hardware", True, marks=pytest.mark.skipif(
        not HARDWARE, reason="set BRUTEFORCE_TEST_D3D11_HARDWARE=1 for a hardware device")),
])
@pytest.mark.parametrize("depth_texture", [False, True])
def test_traced_glare_threshold_uses_gpu_stencil_only_with_alias(
    device, vertex_program, depth_texture, tmp_path
):
    vertex = "-vsh" if vertex_program else ""
    depth = "-depth" if depth_texture else ""
    baseline, _ = render_frames(
        f"gpu-glare{depth}{vertex}-{device}", tmp_path / "baseline.raw", 18
    )
    aliased, result = render_frames(
        f"gpu-glare-stencil{depth}{vertex}-{device}", tmp_path / "alias.raw", 18
    )
    assert "glare threshold frames 18" in result.stdout
    if vertex_program:
        checks = re.findall(r"\[VSH-VERIFY\] (\d+) batches checked, (\d+) differ", result.stderr)
        assert checks and all(int(differ) == 0 for _, differ in checks), result.stderr
    for case, (color, alpha, stencil, passes) in enumerate(GLARE_CASES):
        for frames, uses_gpu_stencil in ((baseline, False), (aliased, True)):
            bright, composite = glare_pixels(color, alpha, not uses_gpu_stencil or passes)
            for y in range(2, SIZE - 2):
                for x in range(2, SIZE - 2):
                    for frame, expected in ((2 * case, bright), (2 * case + 1, composite)):
                        actual = rgb_at(frames[frame], x, y)
                        assert max(abs(value - want) for value, want in zip(actual, expected)) <= 1, (
                            device, vertex_program, depth_texture, uses_gpu_stencil, case,
                            stencil, frame, x, y, actual, expected,
                        )


@pytest.mark.parametrize("vertex_program", [False, True])
def test_traced_glare_cpu_does_not_observe_stencil_clear(vertex_program, tmp_path):
    vertex = "-vsh" if vertex_program else ""
    frames, result = render_frames(f"cpu-glare{vertex}", tmp_path / "cpu.raw", 18)
    assert "glare threshold frames 18" in result.stdout
    for case, (color, alpha, _, _) in enumerate(GLARE_CASES):
        bright, composite = glare_pixels(color, alpha, True)
        for frame, expected in ((2 * case, bright), (2 * case + 1, composite)):
            for x, y in ((10, 15), (30, 32), (50, 45)):
                actual = rgb_at(frames[frame], x, y)
                assert max(abs(value - want) for value, want in zip(actual, expected)) <= 2, (
                    vertex_program, case, frame, x, y, actual, expected,
                )


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


def test_invalid_render_scale_is_rejected(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-scale-invalid", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_SCALE must be native or a number from 0.5 to 10" in result.stderr


def test_invalid_native_output_scale_is_rejected(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-scale-output-invalid", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "RECOMP_GPU_OUTPUT produces a native scale outside" in result.stderr


def test_scaled_rendering_rejects_disabled_shared_presentation(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-scale2-direct-off", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "scaled rendering requires shared GPU presentation" in result.stderr


def test_scaled_depth_aliases_are_rejected_explicitly(tmp_path):
    env = {key: value for key, value in os.environ.items() if not key.startswith("RECOMP_")}
    result = subprocess.run(
        [D3D11_EXE, str(XBE), "gpu-scale2-depth", str(tmp_path / "unused.raw")],
        capture_output=True, text=True, check=False, timeout=30, env=env,
    )
    assert result.returncode != 0
    assert "scaled depth/stencil texture aliases are not supported" in result.stderr


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
