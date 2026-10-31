import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tkinter as tk

import pytest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import launcher


@pytest.mark.skipif(sys.platform != "win32", reason="Windows batch launcher")
def test_batch_starts_launcher_from_another_directory(tmp_path):
    pythonw = Path(sys.executable).with_name("pythonw.exe")
    if not pythonw.is_file():
        pytest.skip("pythonw.exe is unavailable")

    root = tmp_path / "Brute Force launcher"
    root.mkdir()
    for name in ("Brute Force.bat", "Brute Force.cmd"):
        shutil.copyfile(ROOT / name, root / name)
    tools = root / "tools"
    tools.mkdir()
    marker = tmp_path / "launcher-started.json"
    (tools / "launcher.py").write_text(
        "import json, os\n"
        "from pathlib import Path\n"
        "Path(os.environ['BRUTEFORCE_BATCH_TEST_MARKER']).write_text("
        "json.dumps({'cwd': os.getcwd(), 'script': __file__}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    env = os.environ.copy()
    env["PATH"] = str(pythonw.parent) + os.pathsep + env.get("PATH", "")
    env["BRUTEFORCE_BATCH_TEST_MARKER"] = str(marker)
    result = subprocess.run(
        [env["COMSPEC"], "/d", "/c", str(root / "Brute Force.bat")],
        cwd=tmp_path, env=env, capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, f"{result.stdout}\n{result.stderr}"
    deadline = time.monotonic() + 10
    while not marker.exists() and time.monotonic() < deadline:
        time.sleep(0.01)
    assert marker.exists(), "The batch file did not start the launcher"
    started = json.loads(marker.read_text(encoding="utf-8"))
    assert Path(started["cwd"]).resolve() == root.resolve()
    assert Path(started["script"]).resolve() == (tools / "launcher.py").resolve()


def test_settings_round_trip_and_missing_defaults(tmp_path):
    path = tmp_path / "BruteForce" / "settings.json"
    assert launcher.load_settings(path) == launcher.DEFAULTS
    values = {**launcher.DEFAULTS, "renderer": "d3d11", "shader_cache": "1", "vsync": "0"}
    launcher.save_settings(path, values)
    assert launcher.load_settings(path) == values
    data = json.loads(path.read_text(encoding="utf-8"))
    assert data == {"version": 2, "settings": values}
    path.write_text('{"version": 1, "settings": {"renderer": "cpu"}}', encoding="utf-8")
    assert launcher.load_settings(path) == {**launcher.DEFAULTS, "renderer": "cpu"}
    path.write_text(
        '{"version": 1, "settings": {"program_near_zero": "inherit"}}', encoding="utf-8"
    )
    assert launcher.load_settings(path)["program_near_zero"] == "hud"


@pytest.mark.parametrize("data", [
    "not json",
    "[]",
    '{"version": 3, "settings": {}}',
    '{"version": true, "settings": {}}',
    '{"version": 1, "settings": {"unexpected": "1"}}',
    '{"version": 1, "settings": {"vsync": true}}',
    '{"version": 1, "settings": {"window_scale": "9"}}',
    '{"version": 1, "settings": {"program_near_zero": "invalid"}}',
])
def test_invalid_settings_are_reported_and_left_unchanged(tmp_path, data):
    path = tmp_path / "settings.json"
    path.write_text(data, encoding="utf-8")
    with pytest.raises(ValueError):
        launcher.load_settings(path)
    assert path.read_text(encoding="utf-8") == data


def test_settings_path_uses_local_app_data(tmp_path):
    assert launcher.settings_path({"LOCALAPPDATA": str(tmp_path)}) == (
        tmp_path / "BruteForce" / "settings.json"
    )
    with pytest.raises(launcher.LauncherError, match="LOCALAPPDATA"):
        launcher.settings_path({})


def test_launcher_maps_every_setting_and_preserves_unrelated_environment():
    values = {
        "config": "Debug", "renderer": "d3d11", "window": "fullscreen",
        "window_scale": "3", "present_filter": "point", "vsync": "0",
        "resolution": "1",
        "gpu_vsh": "0", "gpu_textures": "0", "shader_cache": "1",
        "window_clip": "1", "program_near_zero": "1", "visibility": "async", "depth_texture": "1",
        "stencil_alias": "1", "direct_present": "1",
    }
    original = {"RECOMP_GPU": "null", "RECOMP_PRESENT_VSYNC": "1", "UNRELATED": "keep"}
    env = launcher.game_environment(values, original)
    assert env == {
        "UNRELATED": "keep", "RECOMP_GPU": "d3d11", "RECOMP_FULLSCREEN": "1",
        "RECOMP_WINDOW_SCALE": "3", "RECOMP_PRESENT_FILTER": "point",
        "RECOMP_PRESENT_VSYNC": "0", "RECOMP_GPU_VSH": "0",
        "RECOMP_GPU_TEX_DECODE": "0", "RECOMP_GPU_PSCACHE": "1",
        "RECOMP_WINDOW_CLIP": "1", "RECOMP_PROGRAM_NEAR_ZERO": "1", "RECOMP_GPU_ZPASS": "async",
        "RECOMP_GPU_DEPTH_TEXTURE": "1", "RECOMP_GPU_STENCIL_ALIAS": "1",
        "RECOMP_GPU_SCALE": "1", "RECOMP_GPU_DIRECT_PRESENT": "1",
    }
    assert original == {"RECOMP_GPU": "null", "RECOMP_PRESENT_VSYNC": "1", "UNRELATED": "keep"}
    defaults = launcher.game_environment({}, {})
    assert defaults["RECOMP_GPU"] == "d3d11"
    assert "RECOMP_D3D11_WARP" not in defaults
    assert defaults["RECOMP_GPU_PSCACHE"] == "0"
    assert defaults["RECOMP_GPU_SCALE"] == "1"
    assert defaults["RECOMP_GPU_DIRECT_PRESENT"] == "0"
    assert defaults["RECOMP_GPU_STENCIL_ALIAS"] == "0"
    assert defaults["RECOMP_GPU_ZPASS"] == "sync"
    assert defaults["RECOMP_PROGRAM_NEAR_ZERO"] == "hud"
    native = launcher.game_environment({"resolution": "native", "direct_present": "0"}, {})
    assert native["RECOMP_GPU_SCALE"] == "native"
    assert native["RECOMP_GPU_DIRECT_PRESENT"] == "1"
    with pytest.raises(launcher.LauncherError, match="depth or stencil aliases"):
        launcher.game_environment({"resolution": "2.25", "depth_texture": "1"}, {})


def test_program_near_plane_defaults_to_hud_safe_and_can_inherit_or_override():
    original = {"RECOMP_PROGRAM_NEAR_ZERO": "1"}
    assert launcher.game_environment({}, original)["RECOMP_PROGRAM_NEAR_ZERO"] == "hud"
    assert launcher.game_environment(
        {"program_near_zero": "inherit"}, original
    )["RECOMP_PROGRAM_NEAR_ZERO"] == "1"
    assert launcher.game_environment(
        {"program_near_zero": "0"}, original
    )["RECOMP_PROGRAM_NEAR_ZERO"] == "0"
    assert original == {"RECOMP_PROGRAM_NEAR_ZERO": "1"}
    with pytest.raises(launcher.LauncherError, match="RECOMP_CLIP_NEAR_Z"):
        launcher.game_environment({"program_near_zero": "1"}, {"RECOMP_CLIP_NEAR_Z": "0"})
    with pytest.raises(launcher.LauncherError, match="RECOMP_CLIP_NEAR_Z"):
        launcher.game_environment({"program_near_zero": "hud"}, {"RECOMP_CLIP_NEAR_Z": "0"})


def test_start_uses_validated_tutorial_launch_and_separate_log(tmp_path, monkeypatch):
    calls = {}
    boot = tmp_path / "boot"
    boot.mkdir()

    def prepare(**kwargs):
        calls["prepare"] = kwargs
        return ["game.exe", "disc"], boot, kwargs["base_env"]

    def popen(command, **kwargs):
        calls["command"] = command
        calls["popen"] = kwargs
        kwargs["stdout"].write("game output\n")
        return "process"

    monkeypatch.setattr(launcher.build, "prepare_game_run", prepare)
    monkeypatch.setattr(launcher.subprocess, "Popen", popen)
    process, log_path = launcher.start_game(
        {"config": "Debug", "renderer": "d3d11", "shader_cache": "1"}, {"KEEP": "yes"}
    )
    assert process == "process"
    assert calls["prepare"]["config"] == "Debug"
    assert calls["prepare"]["experimental_tutorial"] is True
    assert calls["popen"]["env"]["RECOMP_GPU"] == "d3d11"
    assert calls["popen"]["env"]["RECOMP_GPU_PSCACHE"] == "1"
    assert calls["popen"]["env"]["KEEP"] == "yes"
    assert calls["command"] == ["game.exe", "disc"]
    assert calls["popen"]["cwd"] == boot
    assert log_path.read_text(encoding="utf-8").endswith("game output\n")


def test_start_failure_is_not_reported_as_success(tmp_path, monkeypatch):
    monkeypatch.setattr(
        launcher.build, "prepare_game_run", lambda **kwargs: (["missing.exe"], tmp_path, {})
    )

    def fail(*args, **kwargs):
        raise OSError("cannot start")

    monkeypatch.setattr(launcher.subprocess, "Popen", fail)
    with pytest.raises(OSError, match="cannot start"):
        launcher.start_game({}, {})


def test_window_controls_and_game_lifecycle(tmp_path, monkeypatch):
    try:
        root = tk.Tk()
    except tk.TclError as exc:
        pytest.skip(f"Tk display is unavailable: {exc}")
    root.withdraw()
    errors = []
    calls = []

    class Process:
        returncode = None

        def poll(self):
            return self.returncode

    process = Process()
    monkeypatch.setattr(launcher.messagebox, "showerror", lambda *a, **k: errors.append(a))
    monkeypatch.setattr(
        launcher, "start_game",
        lambda settings: (calls.append(settings.copy()) or process, tmp_path / "game.log"),
    )
    app = launcher.Launcher(root, tmp_path / "settings.json", launcher.DEFAULTS)
    try:
        assert app.variables["renderer"].get() == "Direct3D 11 (experimental)"
        assert app.variables["resolution"].get() == "480p (guest)"
        assert str(app.boxes["shader_cache"]["state"]) == "readonly"
        assert str(app.boxes["resolution"]["state"]) == "readonly"
        assert str(app.boxes["stencil_alias"]["state"]) == "readonly"
        app.variables["renderer"].set("CPU reference")
        app.refresh()
        assert str(app.boxes["shader_cache"]["state"]) == "disabled"
        assert str(app.boxes["resolution"]["state"]) == "disabled"
        assert str(app.boxes["stencil_alias"]["state"]) == "disabled"
        app.defaults()
        assert app.variables["renderer"].get() == "Direct3D 11 (experimental)"
        assert app.variables["resolution"].get() == "480p (guest)"
        app.variables["shader_cache"].set("On")
        app.variables["program_near_zero"].set("Screen-zero (HUD and glow)")
        assert str(app.boxes["shader_cache"]["state"]) == "readonly"
        assert str(app.boxes["stencil_alias"]["state"]) == "readonly"
        app.play()
        deadline = time.monotonic() + 5
        while app.process is None and time.monotonic() < deadline:
            root.update()
            time.sleep(0.01)
        assert app.process is process
        assert calls[0]["renderer"] == "d3d11"
        assert calls[0]["shader_cache"] == "1"
        assert calls[0]["program_near_zero"] == "1"
        assert launcher.load_settings(tmp_path / "settings.json") == calls[0]
        process.returncode = 7
        app.watch_game()
        assert app.process is None
        assert errors and "code 7" in errors[0][1]
        assert str(app.play_button["state"]) == "normal"
    finally:
        app.close()


def test_launcher_reports_save_and_start_failures(tmp_path, monkeypatch):
    try:
        root = tk.Tk()
    except tk.TclError as exc:
        pytest.skip(f"Tk display is unavailable: {exc}")
    root.withdraw()
    errors = []
    starts = []
    monkeypatch.setattr(launcher.messagebox, "showerror", lambda *a, **k: errors.append(a[1]))
    app = launcher.Launcher(root, tmp_path / "settings.json", launcher.DEFAULTS)
    original_save = launcher.save_settings
    try:
        def fail_save(*args):
            raise OSError("read-only settings")

        monkeypatch.setattr(launcher, "save_settings", fail_save)
        monkeypatch.setattr(launcher, "start_game", lambda settings: starts.append(settings))
        app.play()
        assert not starts and app.future is None
        assert "read-only settings" in errors[-1]

        monkeypatch.setattr(launcher, "save_settings", original_save)

        def fail_start(settings):
            raise launcher.build.BuildError("build is out of date")

        monkeypatch.setattr(launcher, "start_game", fail_start)
        app.play()
        deadline = time.monotonic() + 5
        while app.future is not None and time.monotonic() < deadline:
            root.update()
            time.sleep(0.01)
        assert app.future is None and app.process is None
        assert "build is out of date" in errors[-1]
        assert str(app.play_button["state"]) == "normal"
    finally:
        app.close()
