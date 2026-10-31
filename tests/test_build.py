import hashlib
import io
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build


def entry(name):
    return SimpleNamespace(name=name)


def test_build_command_batches_extra_targets():
    command = build.cmake_build_command("Release", 4, ("bruteforce_d3d11_test", "bruteforce_present_test"))
    assert command[command.index("--target"):] == [
        "--target", "bruteforce", "bruteforce_d3d11_test", "bruteforce_present_test",
    ]
    assert "--target" not in build.cmake_build_command("Release", 4)


def test_disc_paths_accept_safe_names():
    paths = build.validate_disc_paths([("", entry("default.xbe")), ("Data", entry("asset.bin"))])
    assert [parts for parts, _ in paths] == [["default.xbe"], ["Data", "asset.bin"]]


@pytest.mark.parametrize("name", ["..", ".", "a\\b", "C:foo", "bad?.bin", "NUL.txt", "a."])
def test_disc_paths_reject_unsafe_names(name):
    with pytest.raises(build.BuildError, match="Unsafe disc path"):
        build.validate_disc_paths([("", entry(name))])


def test_disc_paths_reject_case_collisions_and_file_directory_collisions():
    with pytest.raises(build.BuildError, match="collision"):
        build.validate_disc_paths([("", entry("data.bin")), ("", entry("DATA.BIN"))])
    with pytest.raises(build.BuildError, match="collide"):
        build.validate_disc_paths([("", entry("Data")), ("data", entry("file.bin"))])


def test_copy_or_verify_preserves_existing_files(tmp_path):
    root = tmp_path / "game"
    root.mkdir()
    destination = root / "data" / "asset.bin"
    build.copy_or_verify(io.BytesIO(b"headerpayload"), 6, 7, destination, root)
    assert destination.read_bytes() == b"payload"
    build.copy_or_verify(io.BytesIO(b"headerpayload"), 6, 7, destination, root)
    destination.write_bytes(b"changed")
    with pytest.raises(build.BuildError, match="not overwriting"):
        build.copy_or_verify(io.BytesIO(b"headerpayload"), 6, 7, destination, root)
    assert destination.read_bytes() == b"changed"


def test_copy_or_verify_removes_its_own_truncated_partial_file(tmp_path):
    root = tmp_path / "game"
    root.mkdir()
    destination = root / "asset.bin"
    with pytest.raises(build.BuildError, match="truncated"):
        build.copy_or_verify(io.BytesIO(b"short"), 0, 10, destination, root)
    assert not destination.exists()


def test_copy_or_verify_rejects_paths_outside_the_root(tmp_path):
    root = tmp_path / "game"
    root.mkdir()
    with pytest.raises(build.BuildError, match="leaves"):
        build.copy_or_verify(io.BytesIO(b"x"), 0, 1, tmp_path / "outside", root)


def synthetic_xiso(path):
    sector = 2048
    image = bytearray(36 * sector)
    descriptor = 32 * sector
    image[descriptor:descriptor + 20] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", image, descriptor + 20, 33, sector)
    offset = 33 * sector
    for name, file_sector, payload in (
        (b"default.xbe", 34, b"synthetic executable"),
        (b"data.bin", 35, b"synthetic asset"),
    ):
        struct.pack_into("<HHIIBB", image, offset, 0, 0, file_sector, len(payload), 0, len(name))
        image[offset + 14:offset + 14 + len(name)] = name
        image[file_sector * sector:file_sector * sector + len(payload)] = payload
        offset += (14 + len(name) + 3) & ~3
    path.write_bytes(image)
    return b"synthetic executable"


@pytest.mark.skipif(
    not (ROOT / ".deps" / "xboxrecomp" / "tools" / "xiso").is_dir(),
    reason="run tools/toolkit.py to install the XISO reader",
)
def test_extract_disc_end_to_end_and_reject_modified_assets(tmp_path, monkeypatch):
    image = tmp_path / "test.iso"
    executable = synthetic_xiso(image)
    fingerprint = tmp_path / "fingerprint.json"
    fingerprint.write_text(
        json.dumps({"xbe_size": len(executable), "xbe_sha256": hashlib.sha256(executable).hexdigest()}),
        encoding="utf-8",
    )
    monkeypatch.setattr(build, "FINGERPRINT", fingerprint)
    monkeypatch.setattr(build, "verify_toolkit", lambda: ROOT / ".deps" / "xboxrecomp")
    target = tmp_path / "disc"
    build.extract_disc(image, target)
    assert (target / "default.xbe").read_bytes() == executable
    assert (target / "data.bin").read_bytes() == b"synthetic asset"
    build.extract_disc(image, target)
    (target / "data.bin").write_bytes(b"X" * len(b"synthetic asset"))
    with pytest.raises(build.BuildError, match="not overwriting"):
        build.extract_disc(image, target)
    assert (target / "data.bin").read_bytes() == b"X" * len(b"synthetic asset")


@pytest.mark.skipif(
    not (ROOT / ".deps" / "xboxrecomp" / "tools" / "xiso").is_dir(),
    reason="run tools/toolkit.py to install the XISO reader",
)
def test_extract_rejects_an_unsupported_xbe_before_writing(tmp_path, monkeypatch):
    image = tmp_path / "test.iso"
    executable = synthetic_xiso(image)
    fingerprint = tmp_path / "fingerprint.json"
    fingerprint.write_text(
        json.dumps({"xbe_size": len(executable), "xbe_sha256": "0" * 64}),
        encoding="utf-8",
    )
    monkeypatch.setattr(build, "FINGERPRINT", fingerprint)
    monkeypatch.setattr(build, "verify_toolkit", lambda: ROOT / ".deps" / "xboxrecomp")
    target = tmp_path / "disc"
    with pytest.raises(build.BuildError, match="unsupported executable"):
        build.extract_disc(image, target)
    assert not target.exists()


def configure_generation(tmp_path, monkeypatch, failed=0):
    game = tmp_path / "game"
    game.mkdir()
    executable = b"test binary"
    (game / "default.xbe").write_bytes(executable)
    fingerprint = tmp_path / "fingerprint.json"
    fingerprint.write_text(
        json.dumps({
            "xbe_size": len(executable),
            "xbe_sha256": hashlib.sha256(executable).hexdigest(),
            "title_id": "0x4D53001E",
            "entry_point": "0x002F5D1F",
        }),
        encoding="utf-8",
    )
    monkeypatch.setattr(build, "GAME_DIR", game)
    monkeypatch.setattr(build, "FINGERPRINT", fingerprint)
    seeds = tmp_path / "seeds.json"
    seeds.write_text(json.dumps([{"start": "0x00001000", "observed": True}]), encoding="utf-8")
    monkeypatch.setattr(build, "SEED_FUNCTIONS", seeds)
    manual = tmp_path / "recomp_manual.c"
    manual.write_text("void fixture(void) {}\n", encoding="utf-8")
    monkeypatch.setattr(build, "MANUAL_SOURCE", manual)
    monkeypatch.setattr(build, "ANALYSIS_DIR", tmp_path / "analysis")
    monkeypatch.setattr(build, "GENERATED_DIR", tmp_path / "generated")
    monkeypatch.setattr(build, "verify_toolkit", lambda: tmp_path / "toolkit")
    monkeypatch.setattr(build, "check_python_dependencies", lambda: None)
    monkeypatch.setattr(build, "check_usb_interrupt_sequence", lambda _: None)
    monkeypatch.setattr(build.importlib.metadata, "version", lambda _: "5.0.9")
    commands = []

    def fake_run(name, command, cwd, log):
        commands.append((name, command))
        if name == "parse":
            Path(command[-1]).write_text(
                json.dumps({"title_id": "0x4D53001E", "entry_point": "0x002F5D1F"}),
                encoding="utf-8",
            )
        if name == "recomp":
            gen = Path(command[command.index("--gen-dir") + 1])
            for filename in ("recomp_types.h", "recomp_funcs.h", "recomp_dispatch.c", "recomp_0000.c"):
                contents = (
                    "{ 0x00001000u, (recomp_func_t)sub_00001000 },\n"
                    if filename == "recomp_dispatch.c" else ""
                )
                (gen / filename).write_text(contents, encoding="utf-8")
            output = Path(command[command.index("--output-dir") + 1])
            output.mkdir()
            (output / "summary.json").write_text(
                json.dumps({"translated": 1, "failed": failed, "unresolved_stubs": 0, "unimplemented": {}}),
                encoding="utf-8",
            )
        return 0.1

    monkeypatch.setattr(build, "run_logged", fake_run)
    return commands


def test_generation_records_all_stages_and_uses_fresh_output(tmp_path, monkeypatch):
    commands = configure_generation(tmp_path, monkeypatch)
    build.generate()
    first = build.load_json(build.ANALYSIS_DIR / "latest.json")["run_id"]
    manifest = build.load_json(build.ANALYSIS_DIR / "runs" / first / "manifest.json")
    assert manifest["status"] == "generated"
    assert manifest["manual_sha256"] == build.sha256_file(build.MANUAL_SOURCE)
    assert manifest["fingerprint_sha256"] == build.sha256_file(build.FINGERPRINT)
    assert manifest["seed_sha256"] == build.sha256_file(build.SEED_FUNCTIONS)
    assert [stage["name"] for stage in manifest["steps"]] == ["parse", "disasm", "funcid", "abi", "recomp"]
    assert "--text-only" not in commands[1][1]
    assert commands[1][1][commands[1][1].index("--seed-functions") + 1] == build.SEED_FUNCTIONS
    assert "--all" in commands[4][1]
    assert "--abi-dir" in commands[4][1]
    assert commands[4][1][commands[4][1].index("--exclude-manual") + 1] == build.MANUAL_SOURCE
    build.generate()
    second = build.load_json(build.ANALYSIS_DIR / "latest.json")["run_id"]
    assert first != second
    assert (build.GENERATED_DIR / first).is_dir()
    assert (build.GENERATED_DIR / second).is_dir()


@pytest.mark.parametrize("source", ["MANUAL_SOURCE", "FINGERPRINT", "SEED_FUNCTIONS"])
def test_generation_rejects_changed_codegen_input(tmp_path, monkeypatch, source):
    configure_generation(tmp_path, monkeypatch)
    build.generate()
    path = getattr(build, source)
    path.write_text(path.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    with pytest.raises(build.BuildError, match="manual source"):
        build.latest_generation()


def rewrite_manifest(**changes):
    run_id = build.load_json(build.ANALYSIS_DIR / "latest.json")["run_id"]
    path = build.ANALYSIS_DIR / "runs" / run_id / "manifest.json"
    manifest = build.load_json(path)
    manifest.update(changes)
    build.write_json(path, manifest)


def test_generation_survives_runtime_only_toolkit_changes(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    build.generate()
    rewrite_manifest(toolkit_tree="0" * 40, toolkit_codegen_tree="tools=a,templates=b")
    monkeypatch.setattr(build, "current_codegen_tree", lambda toolkit=None: "tools=a,templates=b")
    build.latest_generation()


def test_generation_rejects_changed_recompiler_or_template(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    build.generate()
    rewrite_manifest(toolkit_tree="0" * 40, toolkit_codegen_tree="tools=a,templates=b")
    monkeypatch.setattr(build, "current_codegen_tree", lambda toolkit=None: "tools=a,templates=c")
    with pytest.raises(build.BuildError, match="pinned toolkit"):
        build.latest_generation()
    monkeypatch.setattr(build, "current_codegen_tree", lambda toolkit=None: None)
    with pytest.raises(build.BuildError, match="pinned toolkit"):
        build.latest_generation()


def test_run_rejects_an_executable_built_against_another_toolkit(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    build.generate()
    run_id = build.load_json(build.ANALYSIS_DIR / "latest.json")["run_id"]
    build_dir = tmp_path / "build"
    executable = build_dir / "Release" / "bruteforce.exe"
    executable.parent.mkdir(parents=True)
    executable.write_bytes(b"fixture")
    build.write_json(
        build_dir / "latest.json",
        {
            "run_id": run_id,
            "config": "Release",
            "xbe_sha256": build.load_json(build.FINGERPRINT)["xbe_sha256"],
            "toolkit_tree": "0" * 40,
        },
    )
    monkeypatch.setattr(build, "BUILD_DIR", build_dir)
    with pytest.raises(build.BuildError, match="Build metadata"):
        build.run_game()


def test_input_change_during_generation_does_not_replace_latest(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    build.write_json(build.ANALYSIS_DIR / "latest.json", {"run_id": "previous"})
    fake_run = build.run_logged

    def change_manual_after_recomp(name, command, cwd, log):
        elapsed = fake_run(name, command, cwd, log)
        if name == "recomp":
            build.MANUAL_SOURCE.write_text("void changed(void) {}\n", encoding="utf-8")
        return elapsed

    monkeypatch.setattr(build, "run_logged", change_manual_after_recomp)
    with pytest.raises(build.BuildError, match="changed during generation"):
        build.generate()
    assert build.load_json(build.ANALYSIS_DIR / "latest.json") == {"run_id": "previous"}
    manifests = list((build.ANALYSIS_DIR / "runs").glob("*/manifest.json"))
    assert build.load_json(manifests[0])["status"] == "failed"


def test_generation_rejects_a_seed_missing_from_dispatch(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    fake_run = build.run_logged

    def omit_seed_after_recomp(name, command, cwd, log):
        elapsed = fake_run(name, command, cwd, log)
        if name == "recomp":
            gen = Path(command[command.index("--gen-dir") + 1])
            (gen / "recomp_dispatch.c").write_text("", encoding="utf-8")
        return elapsed

    monkeypatch.setattr(build, "run_logged", omit_seed_after_recomp)
    with pytest.raises(build.BuildError, match="omitted seeded function targets"):
        build.generate()
    assert not (build.ANALYSIS_DIR / "latest.json").exists()


def test_usb_interrupt_sequence_validates_shadow_and_critical_region(tmp_path):
    source = tmp_path / "recomp_0000.c"
    sequence = (
        'RECOMP_UNIMPL("cli", 0x00384FA5u); /* cli */\n'
        "eax = MEM32(ecx + 8);\n"
        "eax = ZX16(MEM16(eax + 0x80));\n"
        "ebx = MEM32(-25157620);\n"
        'RECOMP_UNIMPL("sti", 0x00384FB6u); /* sti */\n'
        "esi = eax;\n"
    )
    source.write_text(sequence, encoding="utf-8")
    build.check_usb_interrupt_sequence(tmp_path)
    for invalid in (
        sequence.replace("esi = eax;", "some_call();\nesi = eax;"),
        sequence.replace("ebx = MEM32(-25157620);", "some_call();"),
        sequence.replace("MEM32(ecx + 8);", "MEM32(ecx + 8) + some_call();"),
        sequence.replace("ecx + 8", "ecx + 12"),
        sequence.replace("eax + 0x80", "eax + 0x84"),
        sequence.replace("-25157620", "-25157616"),
        sequence.replace(
            "ebx = MEM32(-25157620);",
            "loc_00384FB0: ;\nebx = MEM32(-25157620);",
        ),
        sequence.replace("/* sti */", "/* sti */ some_call();"),
        sequence + sequence,
        sequence.replace("0x00384FB6u", "0x00384FB7u"),
    ):
        source.write_text(invalid, encoding="utf-8")
        with pytest.raises(build.BuildError, match="interrupt-mask sequence"):
            build.check_usb_interrupt_sequence(tmp_path)


@pytest.mark.parametrize(
    "seeds",
    [
        {"start": "0x00001000"},
        [{"start": "0x1000"}],
        [{"start": "0x00001000"}, {"start": "0x00001000"}],
    ],
)
def test_seed_validation_rejects_invalid_or_duplicate_entries(tmp_path, monkeypatch, seeds):
    configure_generation(tmp_path, monkeypatch)
    build.SEED_FUNCTIONS.write_text(json.dumps(seeds), encoding="utf-8")
    with pytest.raises(build.BuildError, match="Function seeds|Invalid function seed|Duplicate function seed"):
        build.generate()
    assert not build.ANALYSIS_DIR.exists()


@pytest.mark.skipif(
    not (ROOT / ".deps" / "xboxrecomp" / "tools" / "recomp").is_dir(),
    reason="run tools/toolkit.py to install the recompiler",
)
def test_title_entry_hooks_are_discovered_by_the_recompiler(monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / ".deps" / "xboxrecomp"))
    from tools.recomp.manual_scan import entry_hooks

    assert entry_hooks(str(ROOT / "src" / "recomp_manual.c")) == {0x00371AB0, 0x0037834E}


@pytest.mark.skipif(
    not (ROOT / ".deps" / "xboxrecomp" / "tools" / "recomp").is_dir(),
    reason="run tools/toolkit.py to install the recompiler",
)
def test_pgraph_wrapper_is_discovered_by_the_recompiler(monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / ".deps" / "xboxrecomp"))
    from tools.recomp.manual_scan import scan

    skipped, wrapped, _ = scan(str(ROOT / "src" / "recomp_manual.c"))
    assert 0x00377F80 in skipped
    assert 0x00377F80 in wrapped


def test_failed_translation_does_not_replace_latest_generation(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch, failed=1)
    build.write_json(build.ANALYSIS_DIR / "latest.json", {"run_id": "previous"})
    with pytest.raises(build.BuildError, match="failed translations"):
        build.generate()
    assert build.load_json(build.ANALYSIS_DIR / "latest.json") == {"run_id": "previous"}
    manifests = list((build.ANALYSIS_DIR / "runs").glob("*/manifest.json"))
    assert len(manifests) == 1
    assert build.load_json(manifests[0])["status"] == "failed"


def test_translation_reports_unresolved_and_unimplemented_sites(tmp_path, capsys):
    run = tmp_path / "run"
    gen = tmp_path / "gen"
    (run / "recomp").mkdir(parents=True)
    gen.mkdir()
    (run / "recomp" / "summary.json").write_text(
        json.dumps({
            "translated": 10,
            "failed": 0,
            "unresolved_stubs": 2,
            "unimplemented": {"insn": [1, 2, 3]},
        }),
        encoding="utf-8",
    )
    for filename in ("recomp_types.h", "recomp_funcs.h", "recomp_dispatch.c", "recomp_0000.c"):
        (gen / filename).write_text("", encoding="utf-8")
    result = build.check_translation(run, gen)
    assert result == {"translated": 10, "unresolved_stubs": 2, "unimplemented_sites": 3}
    assert "2 unresolved call targets and 3 untranslated instruction sites" in capsys.readouterr().err


def test_run_rejects_an_executable_from_an_older_generation(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    build.generate()
    build_dir = tmp_path / "build"
    executable = build_dir / "Release" / "bruteforce.exe"
    executable.parent.mkdir(parents=True)
    executable.write_bytes(b"fixture")
    fingerprint = build.load_json(build.FINGERPRINT)
    build.write_json(
        build_dir / "latest.json",
        {"run_id": "older", "config": "Release", "xbe_sha256": fingerprint["xbe_sha256"]},
    )
    monkeypatch.setattr(build, "BUILD_DIR", build_dir)
    with pytest.raises(build.BuildError, match="Build metadata"):
        build.run_game()


def test_run_isolates_each_runtime_working_directory(tmp_path, monkeypatch):
    configure_generation(tmp_path, monkeypatch)
    build.generate()
    run_id = build.load_json(build.ANALYSIS_DIR / "latest.json")["run_id"]
    build_dir = tmp_path / "build"
    executable = build_dir / "Release" / "bruteforce.exe"
    executable.parent.mkdir(parents=True)
    executable.write_bytes(b"fixture")
    fingerprint = build.load_json(build.FINGERPRINT)
    build.write_json(
        build_dir / "latest.json",
        {
            "run_id": run_id,
            "config": "Release",
            "xbe_sha256": fingerprint["xbe_sha256"],
            "toolkit_tree": build.TOOLKIT_TREE,
        },
    )
    monkeypatch.setattr(build, "BUILD_DIR", build_dir)
    calls = []
    monkeypatch.setenv("RECOMP_D3D_PRESENT", "0")
    monkeypatch.setenv("RECOMP_VBLANK", "0")
    for name in (
        "RECOMP_USB", "RECOMP_PB_WALK", "RECOMP_PGRAPH_IRQ", "RECOMP_KEYBOARD",
        "RECOMP_AC97_READY", "RECOMP_AUDIO",
    ):
        monkeypatch.delenv(name, raising=False)

    def fake_run(command, **kwargs):
        boot_dir = Path(kwargs["cwd"])
        assert boot_dir.is_dir()
        (boot_dir / "xbox_kernel.log").write_text("runtime log", encoding="utf-8")
        calls.append((command, boot_dir, kwargs["env"]))
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(build.subprocess, "run", fake_run)
    assert build.run_game() == 0
    assert build.run_game(graphics=True) == 0
    assert build.run_game(experimental_tutorial=True) == 0
    monkeypatch.setenv("RECOMP_KEYBOARD", "0")
    assert build.run_game(experimental_tutorial=True) == 0
    assert build.run_game(experimental_tutorial=True, experimental_audio=True) == 0
    monkeypatch.setenv("RECOMP_AC97_READY", "existing")
    assert build.run_game(experimental_tutorial=True) == 0
    assert build.run_game(no_audio=True) == 0
    save_root = tmp_path / "isolated-\u65e5"
    save_root.mkdir()
    monkeypatch.setenv("BRUTEFORCE_SAVE_ROOT", "previous partition root")
    monkeypatch.setenv("RECOMP_SAVE_DIR", "previous user-data root")
    assert build.run_game() == 0
    assert build.run_game(experimental_tutorial=True, isolated_saves=save_root) == 0
    assert len({cwd for _, cwd, _ in calls}) == len(calls)
    assert all(cwd.parent == build.ANALYSIS_DIR / "runs" / run_id / "boots" for _, cwd, _ in calls)
    assert all(command == [str(executable), str(build.GAME_DIR)] for command, _, _ in calls)
    assert all(
        (cwd / "xbox_kernel.log").read_text(encoding="utf-8") == "runtime log"
        for _, cwd, _ in calls
    )
    assert calls[0][2]["RECOMP_D3D_PRESENT"] == "0"
    graphics_flags = ("RECOMP_PB_EXEC", "RECOMP_FB_WINDOW", "RECOMP_D3D_PRESENT")
    assert all(calls[1][2][name] == "1" for name in graphics_flags)
    assert all(calls[2][2][name] == "1" for name in graphics_flags)
    assert all(
        name not in calls[0][2] and name not in calls[1][2]
        for name in ("RECOMP_USB", "RECOMP_PB_WALK", "RECOMP_PGRAPH_IRQ", "RECOMP_KEYBOARD")
    )
    assert calls[2][2]["RECOMP_USB"] == "1"
    assert calls[2][2]["RECOMP_PB_WALK"] == "staged"
    assert calls[2][2]["RECOMP_PGRAPH_IRQ"] == "1"
    assert calls[0][2]["RECOMP_VBLANK"] == "0"
    assert calls[2][2]["RECOMP_VBLANK"] == "1"
    assert calls[2][2]["RECOMP_KEYBOARD"] == "1"
    assert calls[3][2]["RECOMP_KEYBOARD"] == "0"
    assert all(calls[i][2]["RECOMP_AUDIO"] == "1" for i in range(6))
    assert calls[6][2]["RECOMP_AUDIO"] == "0"
    assert all("RECOMP_AC97_READY" not in calls[i][2] for i in range(5))
    assert calls[5][2]["RECOMP_AC97_READY"] == "existing"
    assert calls[7][2]["BRUTEFORCE_SAVE_ROOT"] == "previous partition root"
    assert calls[7][2]["RECOMP_SAVE_DIR"] == "previous user-data root"
    assert calls[8][2]["BRUTEFORCE_SAVE_ROOT"] == str(save_root.resolve())
    assert "RECOMP_SAVE_DIR" not in calls[8][2]
    assert build.os.environ["RECOMP_D3D_PRESENT"] == "0"
    assert "RECOMP_USB" not in build.os.environ
    assert build.os.environ["RECOMP_AC97_READY"] == "existing"
    assert build.os.environ["BRUTEFORCE_SAVE_ROOT"] == "previous partition root"
    assert build.os.environ["RECOMP_SAVE_DIR"] == "previous user-data root"


def test_audio_compatibility_flag_conflicts_with_explicit_off():
    with pytest.raises(build.BuildError, match="cannot be combined"):
        build.run_game(experimental_audio=True, no_audio=True)


@pytest.mark.parametrize("kind", ["missing", "file", "nonempty"])
def test_isolated_saves_requires_an_existing_empty_directory(tmp_path, kind):
    root = tmp_path / kind
    if kind == "file":
        root.write_text("not a directory", encoding="utf-8")
    elif kind == "nonempty":
        root.mkdir()
        (root / "existing-data").write_text("preserve", encoding="utf-8")
    with pytest.raises(build.BuildError, match="requires an existing empty directory"):
        build.run_game(isolated_saves=root)
    if kind == "nonempty":
        assert (root / "existing-data").read_text(encoding="utf-8") == "preserve"


def test_run_cli_passes_graphics_tutorial_audio_and_isolation_options(monkeypatch):
    calls = []
    monkeypatch.setattr(
        build, "run_game",
        lambda config, graphics, tutorial, audio, saves, no_audio:
            calls.append((config, graphics, tutorial, audio, saves, no_audio)) or 0,
    )
    assert build.main(["run", "--config", "Debug", "--graphics"]) == 0
    assert build.main(["run", "--experimental-tutorial"]) == 0
    assert build.main(["run", "--experimental-tutorial", "--experimental-audio"]) == 0
    assert build.main(["run", "--no-audio"]) == 0
    assert build.main(["run", "--experimental-tutorial", "--isolated-saves", "fresh"]) == 0
    assert calls == [
        ("Debug", True, False, False, None, False),
        ("Release", False, True, False, None, False),
        ("Release", False, True, True, None, False),
        ("Release", False, False, False, None, True),
        ("Release", False, True, False, Path("fresh"), False),
    ]
