#!/usr/bin/env python3
"""Windows launcher for Brute Force."""

from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import sys
import tkinter as tk
from tkinter import messagebox, ttk

import build


SETTINGS_VERSION = 4


@dataclass(frozen=True)
class Setting:
    key: str
    tab: str
    label: str
    description: str
    choices: tuple[tuple[str, str], ...]
    default: str
    gpu_only: bool = False


SETTINGS = (
    Setting(
        "config", "Display", "Build configuration",
        "Use the configuration of the most recently built executable.",
        (("Release", "Release"), ("Debug", "Debug"), ("RelWithDebInfo", "RelWithDebInfo")),
        "Release",
    ),
    Setting(
        "window", "Display", "Window mode",
        "Alt+Enter or F11 also toggles borderless fullscreen while playing.",
        (("windowed", "Window"), ("fullscreen", "Borderless fullscreen")), "windowed",
    ),
    Setting(
        "window_scale", "Display", "Initial window size",
        "Scales the guest frame for display; the window shrinks to fit the desktop.",
        (("1", "1x guest frame"), ("2", "2x guest frame"), ("3", "3x guest frame")),
        "2",
    ),
    Setting(
        "present_filter", "Display", "Display scaling",
        "Smooth or sharp pixels for the CPU renderer and scale-1 presentation fallback.",
        (("linear", "Smooth (linear)"), ("point", "Sharp (point)")), "linear",
    ),
    Setting(
        "vsync", "Display", "Presentation VSync",
        "Wait for the display when presenting; this does not unlock the game frame rate.",
        (("1", "On"), ("0", "Off")), "1",
    ),
    Setting(
        "audio", "Audio", "Game audio",
        "Play the title's music, dialogue, effects, menu, and cinematic audio.",
        (("1", "On"), ("0", "Off")), "1",
    ),
    Setting(
        "audio_dsp", "Audio", "Sound processor",
        "Simple is the recommended wide-stereo path. Accurate runs the Xbox GP/EP programs but remains experimental.",
        (("fold", "Simple (recommended)"), ("xemu", "Accurate (experimental Xbox DSP)")), "fold",
    ),
    Setting(
        "renderer", "Graphics", "Renderer",
        "Direct3D 11 is the default hardware renderer. CPU remains available as the reference path.",
        (("cpu", "CPU reference"), ("d3d11", "Direct3D 11 (default)")), "d3d11",
    ),
    Setting(
        "resolution", "Graphics", "Internal resolution",
        "Render D3D11 surfaces at the guest resolution, a fixed picture height, or the window's native 4:3 picture.",
        (("native", "Native (match output)"), ("4.5", "2160p (4K)"), ("3", "1440p"),
         ("2.25", "1080p"), ("1.5", "720p"), ("1", "480p (guest)")), "1", True,
    ),
    Setting(
        "gpu_vsh", "Graphics", "Vertex programs",
        "Use GPU vertex programs, or keep their transformation on the CPU for comparison.",
        (("1", "GPU"), ("0", "CPU")), "1", True,
    ),
    Setting(
        "gpu_textures", "Graphics", "Texture decoding",
        "Decode supported textures on the GPU, or use the CPU decoder for comparison.",
        (("1", "GPU"), ("0", "CPU")), "1", True,
    ),
    Setting(
        "shader_cache", "Graphics", "Persistent pixel-shader cache",
        "Reuse compiled combiner shaders on later launches. Experimental; off by default.",
        (("0", "Off"), ("1", "On")), "0", True,
    ),
    Setting(
        "window_clip", "Experimental", "Guest window clipping",
        "Apply the guest's window clipping on either renderer. Gameplay coverage is limited.",
        (("0", "Off"), ("1", "On")), "0",
    ),
    Setting(
        "program_near_zero", "Experimental", "Programmable near-plane clipping",
        "HUD-safe keeps UI visible while clipping zero-depth additive glow. Legacy and screen-zero are diagnostics.",
        (("hud", "HUD without additive bloom (recommended)"),
         ("inherit", "Legacy (inherit environment; may flicker)"),
         ("0", "Legacy (force)"), ("1", "Screen-zero (HUD and glow)")), "hud",
    ),
    Setting(
        "visibility", "Experimental", "Visibility reports",
        "Async reports may change timing and have limited gameplay validation.",
        (("sync", "Synchronous"), ("async", "Asynchronous")), "sync", True,
    ),
    Setting(
        "depth_texture", "Experimental", "Depth texture aliases",
        "Allow supported exact D24S8 depth aliases. Other alias layouts are not supported.",
        (("0", "Off"), ("1", "On")), "0", True,
    ),
    Setting(
        "stencil_alias", "Experimental", "Packed depth/stencil aliases",
        "Use GPU stencil for supported exact aliases. May darken glow; gameplay fidelity is unverified.",
        (("0", "Off"), ("1", "On")), "0", True,
    ),
    Setting(
        "direct_present", "Experimental", "Shared GPU presentation",
        "Present a GPU snapshot when eligible. Internal resolutions above 480p enable this automatically.",
        (("0", "Off"), ("1", "On")), "0", True,
    ),
)
BY_KEY = {setting.key: setting for setting in SETTINGS}
DEFAULTS = {setting.key: setting.default for setting in SETTINGS}


class LauncherError(ValueError):
    pass


def settings_path(env=None):
    env = os.environ if env is None else env
    local_app_data = env.get("LOCALAPPDATA")
    if not local_app_data:
        raise LauncherError("LOCALAPPDATA is not set; the launcher cannot save its settings")
    return Path(local_app_data) / "BruteForce" / "settings.json"


def validate_settings(values):
    if not isinstance(values, dict):
        raise LauncherError("Settings must be a JSON object")
    unknown = set(values) - set(BY_KEY)
    if unknown:
        raise LauncherError(f"Unknown settings: {', '.join(sorted(unknown))}")
    result = DEFAULTS.copy()
    for key, value in values.items():
        if not isinstance(value, str) or value not in dict(BY_KEY[key].choices):
            raise LauncherError(f"Invalid value for {key}: {value!r}")
        result[key] = value
    return result


def load_settings(path):
    path = Path(path)
    if not path.exists():
        return DEFAULTS.copy()
    with path.open("r", encoding="utf-8") as stream:
        data = json.load(stream)
    if (not isinstance(data, dict) or set(data) != {"version", "settings"}
            or type(data["version"]) is not int
            or data["version"] not in (1, 2, 3, SETTINGS_VERSION)):
        raise LauncherError("Unsupported settings file format or version")
    settings = data["settings"]
    if data["version"] < SETTINGS_VERSION and isinstance(settings, dict):
        settings = dict(settings)
        if data["version"] == 1 and settings.get("program_near_zero", "inherit") == "inherit":
            settings["program_near_zero"] = "hud"
        if settings.get("audio_dsp", "xemu") == "xemu":
            settings["audio_dsp"] = "fold"
    return validate_settings(settings)


def save_settings(path, values):
    build.write_json(path, {"version": SETTINGS_VERSION, "settings": validate_settings(values)})


def game_environment(values, base_env=None):
    settings = validate_settings(values)
    if settings["resolution"] != "1" and (
        settings["depth_texture"] == "1" or settings["stencil_alias"] == "1"
    ):
        raise LauncherError(
            "Scaled internal resolutions cannot be combined with depth or stencil aliases"
        )
    env = dict(os.environ if base_env is None else base_env)
    env.update({
        "RECOMP_GPU": settings["renderer"],
        "RECOMP_FULLSCREEN": "1" if settings["window"] == "fullscreen" else "0",
        "RECOMP_WINDOW_SCALE": settings["window_scale"],
        "RECOMP_PRESENT_FILTER": settings["present_filter"],
        "RECOMP_PRESENT_VSYNC": settings["vsync"],
        "RECOMP_AUDIO": settings["audio"],
        "RECOMP_APU_DSP": settings["audio_dsp"],
        "RECOMP_GPU_VSH": settings["gpu_vsh"],
        "RECOMP_GPU_TEX_DECODE": settings["gpu_textures"],
        "RECOMP_GPU_PSCACHE": settings["shader_cache"],
        "RECOMP_WINDOW_CLIP": settings["window_clip"],
        "RECOMP_GPU_ZPASS": settings["visibility"],
        "RECOMP_GPU_DEPTH_TEXTURE": settings["depth_texture"],
        "RECOMP_GPU_STENCIL_ALIAS": settings["stencil_alias"],
        "RECOMP_GPU_SCALE": settings["resolution"],
        "RECOMP_GPU_DIRECT_PRESENT": (
            "1" if settings["resolution"] != "1" else settings["direct_present"]
        ),
    })
    if settings["program_near_zero"] != "inherit":
        if settings["program_near_zero"] in ("1", "hud") and env.get(
            "RECOMP_CLIP_NEAR_Z", ""
        ).startswith("0"):
            raise LauncherError(
                "HUD-safe and screen-zero clipping require RECOMP_CLIP_NEAR_Z to remain enabled"
            )
        env["RECOMP_PROGRAM_NEAR_ZERO"] = settings["program_near_zero"]
    return env


def start_game(values, base_env=None):
    settings = validate_settings(values)
    command, boot_dir, env = build.prepare_game_run(
        config=settings["config"],
        experimental_tutorial=True,
        base_env=game_environment(settings, base_env),
    )
    log_path = boot_dir / "launcher.log"
    with log_path.open("x", encoding="utf-8") as log:
        log.write("Brute Force launcher: game stdout and stderr\n")
        log.flush()
        process = subprocess.Popen(
            command, cwd=boot_dir, env=env, stdin=subprocess.DEVNULL,
            stdout=log, stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    return process, log_path


class Launcher:
    def __init__(self, root, path, values):
        self.root = root
        self.path = Path(path)
        self.executor = ThreadPoolExecutor(max_workers=1)
        self.future = None
        self.process = None
        self.log_path = None
        self.variables = {}
        self.boxes = {}
        settings = validate_settings(values)

        root.title("Brute Force Launcher")
        root.geometry("820x650")
        root.minsize(700, 540)
        root.protocol("WM_DELETE_WINDOW", self.close)

        header = ttk.Frame(root, padding=(16, 12, 16, 4))
        header.pack(fill="x")
        ttk.Label(header, text="Brute Force", font=("Segoe UI", 18, "bold")).pack(anchor="w")
        ttk.Label(
            header,
            text="Current bring-up path. Some rendering, input, audio, and gameplay behavior remains unverified.",
            wraplength=760,
        ).pack(anchor="w")

        notebook = ttk.Notebook(root)
        notebook.pack(fill="both", expand=True, padx=12, pady=8)
        frames = {}
        rows = {}
        for tab in ("Display", "Audio", "Graphics", "Experimental"):
            frame = ttk.Frame(notebook, padding=12)
            frame.columnconfigure(0, weight=1)
            notebook.add(frame, text=tab)
            frames[tab] = frame
            rows[tab] = 0

        for setting in SETTINGS:
            frame = frames[setting.tab]
            row = rows[setting.tab]
            labels = dict(setting.choices)
            variable = tk.StringVar(value=labels[settings[setting.key]])
            self.variables[setting.key] = variable
            ttk.Label(frame, text=setting.label).grid(row=row, column=0, sticky="w", pady=(5, 0))
            box = ttk.Combobox(
                frame, textvariable=variable, values=list(labels.values()),
                state="readonly", width=28,
            )
            box.grid(row=row, column=1, sticky="e", padx=(12, 0), pady=(5, 0))
            ttk.Label(frame, text=setting.description, wraplength=690).grid(
                row=row + 1, column=0, columnspan=2, sticky="w", pady=(0, 5)
            )
            self.boxes[setting.key] = box
            rows[setting.tab] += 2
        self.boxes["renderer"].bind("<<ComboboxSelected>>", self.refresh)

        footer = ttk.Frame(root, padding=(12, 4, 12, 12))
        footer.pack(fill="x")
        ttk.Button(footer, text="Restore defaults", command=self.defaults).pack(side="left")
        ttk.Button(footer, text="Save settings", command=self.save).pack(side="left", padx=8)
        self.log_button = ttk.Button(footer, text="Open last log", command=self.open_log, state="disabled")
        self.log_button.pack(side="left")
        self.play_button = ttk.Button(footer, text="Play", command=self.play)
        self.play_button.pack(side="right")
        self.status = ttk.Label(root, text="Settings are saved when you press Play.", padding=(12, 0, 12, 6))
        self.status.pack(fill="x")
        self.refresh()

    def current_settings(self):
        result = {}
        for setting in SETTINGS:
            label = self.variables[setting.key].get()
            values = {text: value for value, text in setting.choices}
            if label not in values:
                raise LauncherError(f"Invalid selection for {setting.label}: {label!r}")
            result[setting.key] = values[label]
        return validate_settings(result)

    def refresh(self, _event=None):
        gpu = self.variables["renderer"].get() == dict(BY_KEY["renderer"].choices)["d3d11"]
        for setting in SETTINGS:
            if setting.gpu_only:
                self.boxes[setting.key].configure(state="readonly" if gpu else "disabled")

    def defaults(self):
        for setting in SETTINGS:
            self.variables[setting.key].set(dict(setting.choices)[setting.default])
        self.refresh()
        self.status.configure(text="Defaults restored; save them or press Play.")

    def save(self):
        try:
            save_settings(self.path, self.current_settings())
        except (OSError, LauncherError) as exc:
            messagebox.showerror("Brute Force", f"Could not save settings:\n{exc}", parent=self.root)
            return False
        self.status.configure(text=f"Settings saved to {self.path}")
        return True

    def play(self):
        if self.future is not None or self.process is not None or not self.save():
            return
        self.play_button.configure(state="disabled")
        self.status.configure(text="Validating the build and starting the game...")
        self.future = self.executor.submit(start_game, self.current_settings())
        self.root.after(100, self.check_start)

    def check_start(self):
        if not self.future.done():
            self.root.after(100, self.check_start)
            return
        error = self.future.exception()
        if error is not None:
            self.future = None
            self.play_button.configure(state="normal")
            self.status.configure(text="The game did not start.")
            messagebox.showerror(
                "Brute Force", f"Could not start the game:\n{type(error).__name__}: {error}",
                parent=self.root,
            )
            return
        self.process, self.log_path = self.future.result()
        self.future = None
        self.log_button.configure(state="normal")
        self.status.configure(text=f"Game running. Output: {self.log_path}")
        self.root.withdraw()
        self.root.after(500, self.watch_game)

    def watch_game(self):
        code = self.process.poll()
        if code is None:
            self.root.after(500, self.watch_game)
            return
        self.process = None
        self.root.deiconify()
        self.play_button.configure(state="normal")
        self.status.configure(text=f"Game exited with code {code}. Output: {self.log_path}")
        if code:
            messagebox.showerror(
                "Brute Force", f"The game exited with code {code}.\n\nLog: {self.log_path}",
                parent=self.root,
            )

    def open_log(self):
        try:
            os.startfile(self.log_path)
        except OSError as exc:
            messagebox.showerror("Brute Force", f"Could not open the log:\n{exc}", parent=self.root)

    def close(self):
        if self.future is not None and not self.future.done():
            messagebox.showinfo("Brute Force", "Please wait for the launch to finish.", parent=self.root)
            return
        self.executor.shutdown(wait=False)
        self.root.destroy()


def main():
    root = tk.Tk()
    try:
        path = settings_path()
        values = load_settings(path)
    except (OSError, ValueError) as exc:
        root.withdraw()
        messagebox.showerror(
            "Brute Force",
            f"Could not load launcher settings:\n{exc}\n\n"
            "The existing settings file was not changed. Correct or remove it before retrying.",
            parent=root,
        )
        root.destroy()
        return 1
    Launcher(root, path, values)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
