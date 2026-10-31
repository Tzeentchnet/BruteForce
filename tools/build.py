#!/usr/bin/env python3
"""Extract, recompile, build, and run a locally supplied Brute Force disc."""

import argparse
from collections import deque
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path, PureWindowsPath
import re
import subprocess
import sys
import time
import uuid

from toolkit import (
    ROOT, TOOLKIT_BASE, TOOLKIT_TREE, ToolkitError, codegen_tree, setup_toolkit, verify_toolkit,
)


GAME_DIR = ROOT / "game" / "disc"
ANALYSIS_DIR = ROOT / "analysis"
GENERATED_DIR = ROOT / "generated"
BUILD_DIR = ROOT / "build"
FINGERPRINT = ROOT / "fingerprints" / "retail.json"
SEED_FUNCTIONS = ROOT / "fingerprints" / "retail-seeds.json"
MANUAL_SOURCE = ROOT / "src" / "recomp_manual.c"
CHUNK_SIZE = 1024 * 1024


class BuildError(Exception):
    pass


def load_json(path):
    with Path(path).open("r", encoding="utf-8") as stream:
        return json.load(stream)


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def verify_xbe(path, fingerprint):
    path = Path(path)
    if not path.is_file():
        raise BuildError(f"Game executable not found: {path}")
    if path.stat().st_size != fingerprint["xbe_size"]:
        raise BuildError(f"Unsupported executable size: {path}")
    actual = sha256_file(path)
    if actual != fingerprint["xbe_sha256"]:
        raise BuildError(f"Unsupported executable SHA-256: {actual} ({path})")
    return actual


def validate_disc_paths(entries):
    files = set()
    directories = set()
    safe = []
    for directory, entry in entries:
        parts = (directory.split("/") if directory else []) + [entry.name]
        for part in parts:
            if (
                not part
                or part in (".", "..")
                or part.endswith((" ", "."))
                or any(ord(char) < 32 or char in '<>:"/\\|?*' for char in part)
                or PureWindowsPath(part).is_reserved()
            ):
                raise BuildError(f"Unsafe disc path component: {part!r}")
        normalized = tuple(part.casefold() for part in parts)
        if normalized in files:
            raise BuildError(f"Case-insensitive disc path collision: {'/'.join(parts)}")
        files.add(normalized)
        directories.update(normalized[:index] for index in range(1, len(normalized)))
        safe.append((parts, entry))
    collisions = files & directories
    if collisions:
        raise BuildError(f"Disc file and directory paths collide: {next(iter(collisions))}")
    return safe


def copy_or_verify(source, offset, size, destination, root):
    destination = Path(destination)
    root = Path(root).resolve()
    if not destination.resolve().is_relative_to(root):
        raise BuildError(f"Extraction path leaves the game directory: {destination}")
    if destination.is_symlink():
        raise BuildError(f"Refusing a symbolic-link destination: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.parent.resolve().is_relative_to(root):
        raise BuildError(f"Extraction directory leaves the game directory: {destination}")

    exists = destination.exists()
    if exists and (not destination.is_file() or destination.stat().st_size != size):
        raise BuildError(f"Existing file differs from the disc; not overwriting: {destination}")
    source.seek(offset)
    created = False
    try:
        with destination.open("rb" if exists else "xb") as target:
            created = not exists
            remaining = size
            while remaining:
                count = min(CHUNK_SIZE, remaining)
                chunk = source.read(count)
                if len(chunk) != count:
                    raise BuildError(f"Disc image is truncated while reading {destination}")
                if exists:
                    if target.read(count) != chunk:
                        raise BuildError(
                            f"Existing file differs from the disc; not overwriting: {destination}"
                        )
                else:
                    target.write(chunk)
                remaining -= count
    except (OSError, BuildError):
        if created:
            destination.unlink(missing_ok=True)
        raise


def extract_disc(image, destination=GAME_DIR):
    toolkit = verify_toolkit()
    fingerprint = load_json(FINGERPRINT)
    image = Path(image).resolve()
    destination = Path(destination)
    if not image.is_file():
        raise BuildError(f"Disc image not found: {image}")

    sys.path.insert(0, str(toolkit))
    from tools.xiso.xdvdfs import SECTOR, Xiso, XisoError

    try:
        with Xiso(str(image)) as disc:
            executable = disc.find("default.xbe")
            if executable is None or executable.is_dir:
                raise BuildError("Disc image has no root default.xbe")
            if executable.size != fingerprint["xbe_size"]:
                raise BuildError("Disc image contains an unsupported executable size")
            if hashlib.sha256(disc.read(executable)).hexdigest() != fingerprint["xbe_sha256"]:
                raise BuildError("Disc image contains an unsupported executable")
            entries = validate_disc_paths(list(disc.walk()))
            destination.mkdir(parents=True, exist_ok=True)
            with image.open("rb") as source:
                for index, (parts, entry) in enumerate(entries, 1):
                    copy_or_verify(
                        source,
                        disc.base + entry.sector * SECTOR,
                        entry.size,
                        destination.joinpath(*parts),
                        destination,
                    )
                    if index % 50 == 0 or index == len(entries):
                        print(f"Extracted or verified {index}/{len(entries)} files", flush=True)
    except XisoError as exc:
        raise BuildError(f"Cannot read Xbox disc image: {exc}") from exc
    verify_xbe(destination / "default.xbe", fingerprint)


def check_python_dependencies():
    try:
        version = importlib.metadata.version("capstone")
    except importlib.metadata.PackageNotFoundError as exc:
        raise BuildError("Install the packages in tools\\requirements.txt first") from exc
    if version != "5.0.9":
        raise BuildError(
            f"Expected capstone 5.0.9, found {version}; install tools\\requirements.txt"
        )


def run_logged(name, command, cwd, log):
    print(f"[{name}] started; log: {log}", flush=True)
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUNBUFFERED"] = "1"
    started = time.monotonic()
    with Path(log).open("w", encoding="utf-8") as stream:
        stream.write(f"Command: {subprocess.list2cmdline([str(arg) for arg in command])}\n")
        stream.write(f"Working directory: {cwd}\n\n")
        stream.flush()
        result = subprocess.run(
            [str(arg) for arg in command],
            cwd=cwd,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=False,
        )
    elapsed = round(time.monotonic() - started, 1)
    if result.returncode:
        with Path(log).open("r", encoding="utf-8", errors="replace") as stream:
            tail = deque(stream, maxlen=20)
        sys.stderr.writelines(tail)
        raise BuildError(f"{name} failed with exit {result.returncode}; see {log}")
    print(f"[{name}] completed in {elapsed}s", flush=True)
    return elapsed


def check_translation(run_dir, gen_dir):
    summary = load_json(Path(run_dir) / "recomp" / "summary.json")
    if summary["failed"] != 0 or summary["translated"] <= 0:
        raise BuildError(
            f"Recompiler reported {summary['failed']} failed translations "
            f"and {summary['translated']} translated functions"
        )
    for required in ("recomp_types.h", "recomp_funcs.h", "recomp_dispatch.c"):
        if not (Path(gen_dir) / required).is_file():
            raise BuildError(f"Recompiler did not produce {required}")
    if not list(Path(gen_dir).glob("recomp_[0-9]*.c")):
        raise BuildError("Recompiler did not produce any function source files")
    unimplemented = sum(len(addresses) for addresses in summary.get("unimplemented", {}).values())
    unresolved = summary.get("unresolved_stubs", 0)
    if unimplemented or unresolved:
        print(
            f"WARNING: {unresolved} unresolved call targets and "
            f"{unimplemented} untranslated instruction sites remain; "
            "this is not evidence that the game is playable.",
            file=sys.stderr,
            flush=True,
        )
    return {
        "translated": summary["translated"],
        "unresolved_stubs": unresolved,
        "unimplemented_sites": unimplemented,
    }


def load_function_seeds():
    seeds = load_json(SEED_FUNCTIONS)
    if not isinstance(seeds, list):
        raise BuildError("Function seeds must be a JSON array")
    addresses = set()
    for seed in seeds:
        start = seed.get("start") if isinstance(seed, dict) else None
        if not isinstance(start, str) or re.fullmatch(r"0x[0-9a-fA-F]{8}", start) is None:
            raise BuildError(f"Invalid function seed: {seed!r}")
        address = int(start, 16)
        if address in addresses:
            raise BuildError(f"Duplicate function seed: {start}")
        addresses.add(address)
    return addresses


def check_seed_dispatch(gen_dir, addresses=None):
    if addresses is None:
        addresses = load_function_seeds()
    dispatch = (Path(gen_dir) / "recomp_dispatch.c").read_text(encoding="utf-8")
    generated = {
        int(address, 16)
        for address in re.findall(
            r"^\s*\{\s*(0x[0-9a-f]{8})u\s*,",
            dispatch,
            re.IGNORECASE | re.MULTILINE,
        )
    }
    missing = sorted(addresses - generated)
    if missing:
        raise BuildError(
            "Recompiler omitted seeded function targets from dispatch: "
            + ", ".join(f"0x{address:08X}" for address in missing)
        )


def check_usb_interrupt_sequence(gen_dir):
    cli_marker = 'RECOMP_UNIMPL("cli", 0x00384FA5u);'
    sti_marker = 'RECOMP_UNIMPL("sti", 0x00384FB6u);'
    cli_count = sti_count = valid = 0
    expected_loads = (
        "eax = MEM32(ecx + 8);",
        "eax = ZX16(MEM16(eax + 0x80));",
        "ebx = MEM32(-25157620);",
    )
    for path in Path(gen_dir).glob("recomp_[0-9]*.c"):
        text = path.read_text(encoding="utf-8")
        cli_count += text.count(cli_marker)
        sti_count += text.count(sti_marker)
        if cli_marker not in text or sti_marker not in text:
            continue
        start = text.index(cli_marker) + len(cli_marker)
        end = text.index(sti_marker)
        if end <= start:
            continue
        between = text[start:end].splitlines()
        if between and re.fullmatch(r"/\*[^*\r\n]*\*/", between[0].strip()):
            between.pop(0)
        loads = [line.strip() for line in between if line.strip()]
        after = text[end + len(sti_marker):].splitlines()
        if after and re.fullmatch(r"/\*[^*\r\n]*\*/", after[0].strip()):
            after.pop(0)
        following = next((line.strip() for line in after if line.strip()), "")
        if (
            tuple(loads) == expected_loads
            and following == "esi = eax;"
        ):
            valid += 1
    if cli_count != 1 or sti_count != 1 or valid != 1:
        raise BuildError(
            "Generated XPP interrupt-mask sequence does not match the guarded USB model"
        )


def generate():
    toolkit = verify_toolkit()
    check_python_dependencies()
    fingerprint = load_json(FINGERPRINT)
    xbe = GAME_DIR / "default.xbe"
    xbe_sha256 = verify_xbe(xbe, fingerprint)
    fingerprint_sha256 = sha256_file(FINGERPRINT)
    seed_sha256 = sha256_file(SEED_FUNCTIONS)
    seeds = load_function_seeds()
    manual_sha256 = sha256_file(MANUAL_SOURCE)
    run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    run_dir = ANALYSIS_DIR / "runs" / run_id
    gen_dir = GENERATED_DIR / run_id
    run_dir.mkdir(parents=True, exist_ok=False)
    gen_dir.mkdir(parents=True, exist_ok=False)
    disasm_dir = run_dir / "disasm"
    funcid_dir = run_dir / "funcid"
    abi_dir = run_dir / "abi"
    analysis_json = run_dir / "default_analysis.json"
    py = sys.executable
    stages = [
        ("parse", [py, "-m", "tools.xbe_parser", xbe, "--json", analysis_json]),
        (
            "disasm",
            [
                py, "-m", "tools.disasm", xbe, "--analysis-json", analysis_json,
                "--output", disasm_dir, "--seed-functions", SEED_FUNCTIONS, "--verbose",
            ],
        ),
        (
            "funcid",
            [
                py, "-m", "tools.func_id", xbe,
                "--functions", disasm_dir / "functions.json",
                "--strings", disasm_dir / "strings.json",
                "--xrefs", disasm_dir / "xrefs.json",
                "--output", funcid_dir, "--verbose",
            ],
        ),
        (
            "abi",
            [
                py, "-m", "tools.abi_analysis", xbe,
                "--disasm-dir", disasm_dir, "--func-id-dir", funcid_dir,
                "--output-dir", abi_dir, "--verbose",
            ],
        ),
        (
            "recomp",
            [
                py, "-m", "tools.recomp", xbe, "--all", "--split", "250",
                "--game-name", "Brute Force", "--disasm-dir", disasm_dir,
                "--func-id-dir", funcid_dir, "--abi-dir", abi_dir,
                "--output-dir", run_dir / "recomp", "--gen-dir", gen_dir,
                "--exclude-manual", MANUAL_SOURCE,
            ],
        ),
    ]
    manifest = {
        "run_id": run_id,
        "status": "running",
        "xbe_sha256": xbe_sha256,
        "fingerprint_sha256": fingerprint_sha256,
        "seed_sha256": seed_sha256,
        "manual_sha256": manual_sha256,
        "toolkit_base": TOOLKIT_BASE,
        "toolkit_tree": TOOLKIT_TREE,
        "toolkit_codegen_tree": current_codegen_tree(toolkit),
        "python": sys.version.split()[0],
        "capstone": importlib.metadata.version("capstone"),
        "steps": [],
    }
    manifest_path = run_dir / "manifest.json"
    write_json(manifest_path, manifest)
    try:
        for name, command in stages:
            elapsed = run_logged(name, command, toolkit, run_dir / f"{name}.log")
            manifest["steps"].append({"name": name, "seconds": elapsed})
            write_json(manifest_path, manifest)
            if name == "parse":
                analysis = load_json(analysis_json)
                if (
                    analysis["title_id"].lower() != fingerprint["title_id"].lower()
                    or analysis["entry_point"].lower() != fingerprint["entry_point"].lower()
                ):
                    raise BuildError("Parsed XBE identity does not match the fingerprint")
        if (sha256_file(FINGERPRINT) != fingerprint_sha256 or
                sha256_file(SEED_FUNCTIONS) != seed_sha256 or
                sha256_file(MANUAL_SOURCE) != manual_sha256):
            raise BuildError("Fingerprint, function seeds, or manual source changed during generation")
        manifest["translation"] = check_translation(run_dir, gen_dir)
        check_seed_dispatch(gen_dir, seeds)
        check_usb_interrupt_sequence(gen_dir)
    except (BuildError, OSError, subprocess.SubprocessError, json.JSONDecodeError, KeyError) as exc:
        manifest["status"] = "failed"
        manifest["error"] = str(exc)
        write_json(manifest_path, manifest)
        raise
    manifest["status"] = "generated"
    write_json(manifest_path, manifest)
    write_json(ANALYSIS_DIR / "latest.json", {"run_id": run_id})
    print(f"Generated sources: {gen_dir}", flush=True)


def current_codegen_tree(toolkit=None):
    try:
        return codegen_tree() if toolkit is None else codegen_tree(toolkit)
    except (OSError, subprocess.CalledProcessError):
        return None


def generation_toolkit_matches(manifest):
    """Accept generated code made by the same recompiler and runtime template.

    Runtime-only framework patches do not change generated code; build and run
    still require the exact toolkit tree through the build metadata.
    """
    if manifest.get("toolkit_tree") == TOOLKIT_TREE:
        return True
    current = current_codegen_tree()
    if current is None:
        return False
    recorded = manifest.get("toolkit_codegen_tree")
    if recorded is None and isinstance(manifest.get("toolkit_tree"), str):
        try:
            recorded = codegen_tree(tree=manifest["toolkit_tree"])
        except (OSError, subprocess.CalledProcessError):
            return False
    return recorded == current


def latest_generation():
    pointer = ANALYSIS_DIR / "latest.json"
    if not pointer.is_file():
        raise BuildError("No generated sources; run the generate command first")
    run_id = load_json(pointer)["run_id"]
    if (
        not isinstance(run_id, str)
        or re.fullmatch(r"[0-9]{8}-[0-9]{6}-[0-9a-f]{8}", run_id) is None
    ):
        raise BuildError("Invalid run ID in analysis\\latest.json")
    run_dir = ANALYSIS_DIR / "runs" / run_id
    manifest = load_json(run_dir / "manifest.json")
    if (
        manifest.get("status") != "generated"
        or manifest.get("toolkit_base") != TOOLKIT_BASE
        or not generation_toolkit_matches(manifest)
        or manifest.get("fingerprint_sha256") != sha256_file(FINGERPRINT)
        or manifest.get("seed_sha256") != sha256_file(SEED_FUNCTIONS)
        or manifest.get("manual_sha256") != sha256_file(MANUAL_SOURCE)
        or manifest.get("xbe_sha256") != verify_xbe(GAME_DIR / "default.xbe", load_json(FINGERPRINT))
    ):
        raise BuildError(
            "Latest generation does not match the game, fingerprint, function seeds, manual source, "
            "or pinned toolkit; run generate again"
        )
    gen_dir = GENERATED_DIR / run_id
    if not gen_dir.is_dir():
        raise BuildError(f"Generated source directory is missing: {gen_dir}")
    check_translation(run_dir, gen_dir)
    check_seed_dispatch(gen_dir)
    check_usb_interrupt_sequence(gen_dir)
    return run_dir, gen_dir


def cmake_build_command(config, parallel, extra_targets=()):
    command = ["cmake", "--build", BUILD_DIR, "--config", config, "--parallel", str(parallel)]
    if extra_targets:
        command.extend(["--target", "bruteforce", *extra_targets])
    return command


def build(config="Release", parallel=4, extra_targets=()):
    toolkit = verify_toolkit()
    run_dir, gen_dir = latest_generation()
    if parallel < 1:
        raise BuildError("Build parallelism must be at least one")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    configure = [
        "cmake", "-S", ROOT, "-B", BUILD_DIR,
        "-G", "Visual Studio 17 2022", "-A", "x64",
        f"-DXBOXRECOMP_DIR={toolkit}", f"-DBRUTEFORCE_GEN_DIR={gen_dir}",
        f"-DBRUTEFORCE_COMPILE_JOBS={parallel}",
    ]
    run_logged("configure", configure, ROOT, run_dir / "configure.log")
    run_logged(
        "build",
        cmake_build_command(config, parallel, extra_targets),
        ROOT,
        run_dir / f"build-{config}.log",
    )
    executable = BUILD_DIR / config / "bruteforce.exe"
    if not executable.is_file():
        raise BuildError(f"Build succeeded but the executable is missing: {executable}")
    write_json(
        BUILD_DIR / "latest.json",
        {
            "run_id": run_dir.name,
            "config": config,
            "xbe_sha256": load_json(FINGERPRINT)["xbe_sha256"],
            "toolkit_tree": TOOLKIT_TREE,
        },
    )
    print(f"Built: {executable}", flush=True)


def prepare_game_run(config="Release", graphics=False, experimental_tutorial=False,
                     experimental_audio=False, isolated_saves=None, base_env=None,
                     no_audio=False):
    """Validate the build and return (command, working directory, environment)."""
    if experimental_audio and no_audio:
        raise BuildError("--experimental-audio and --no-audio cannot be combined")
    save_root = None
    if isolated_saves is not None:
        save_root = Path(isolated_saves).resolve()
        if not save_root.is_dir() or next(save_root.iterdir(), None) is not None:
            raise BuildError("--isolated-saves requires an existing empty directory")
    fingerprint = load_json(FINGERPRINT)
    verify_xbe(GAME_DIR / "default.xbe", fingerprint)
    run_dir, _ = latest_generation()
    executable = BUILD_DIR / config / "bruteforce.exe"
    metadata = BUILD_DIR / "latest.json"
    if not executable.is_file() or not metadata.is_file():
        raise BuildError(f"No {config} executable; run the build command first")
    built = load_json(metadata)
    if (
        built.get("config") != config
        or built.get("xbe_sha256") != fingerprint["xbe_sha256"]
        or built.get("run_id") != run_dir.name
        or built.get("toolkit_tree") != TOOLKIT_TREE
    ):
        raise BuildError("Build metadata does not match the requested game and configuration")
    env = dict(os.environ if base_env is None else base_env)
    env.setdefault("RECOMP_VBLANK", "1")
    env.setdefault("RECOMP_STUB_LOG", "1")
    env.setdefault("RECOMP_AUDIO", "1")
    if experimental_tutorial:
        env.update({
            "RECOMP_USB": "1",
            "RECOMP_PB_WALK": "staged",
            "RECOMP_PGRAPH_IRQ": "1",
            "RECOMP_VBLANK": "1",
        })
        env.setdefault("RECOMP_KEYBOARD", "1")
    if experimental_audio:
        env["RECOMP_AUDIO"] = "1"
    if no_audio:
        env["RECOMP_AUDIO"] = "0"
    if save_root is not None:
        env["BRUTEFORCE_SAVE_ROOT"] = str(save_root)
        # The host derives RECOMP_SAVE_DIR from the wide path as UTF-8.
        env.pop("RECOMP_SAVE_DIR", None)
    if graphics or experimental_tutorial:
        env.update({
            "RECOMP_PB_EXEC": "1",
            "RECOMP_FB_WINDOW": "1",
            "RECOMP_D3D_PRESENT": "1",
        })
        env.setdefault("RECOMP_GPU", "d3d11")
    boot_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    boot_dir = run_dir / "boots" / boot_id
    boot_dir.mkdir(parents=True, exist_ok=False)
    print(f"Starting {executable} using {GAME_DIR}", flush=True)
    print(f"Runtime working directory: {boot_dir}", flush=True)
    if save_root is not None:
        print(f"Isolated partition and user-data directory: {save_root}", flush=True)
    return [str(executable), str(GAME_DIR)], boot_dir, env


def run_game(config="Release", graphics=False, experimental_tutorial=False,
             experimental_audio=False, isolated_saves=None, no_audio=False):
    command, boot_dir, env = prepare_game_run(
        config, graphics, experimental_tutorial, experimental_audio, isolated_saves,
        no_audio=no_audio,
    )
    return subprocess.run(command, cwd=boot_dir, env=env, check=False).returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("setup", help="install the pinned framework and fixes")
    extract = commands.add_parser("extract", help="extract or verify a disc image")
    extract.add_argument("--disc", required=True, help="path to the local XISO")
    commands.add_parser("generate", help="analyze and recompile the extracted executable")
    for name in ("build", "all"):
        command = commands.add_parser(name, help="build the native executable")
        command.add_argument("--config", choices=("Release", "Debug", "RelWithDebInfo"), default="Release")
        command.add_argument("--parallel", type=int, default=4)
        command.add_argument("--extra-target", action="append", default=[],
                             help="also build a named CMake target in the same build; repeatable")
        if name == "all":
            command.add_argument("--disc", required=True, help="path to the local XISO")
    run = commands.add_parser("run", help="run a previously built executable")
    run.add_argument("--config", choices=("Release", "Debug", "RelWithDebInfo"), default="Release")
    run.add_argument(
        "--graphics", action="store_true",
        help="enable graphics with the default Direct3D 11 renderer and presentation",
    )
    run.add_argument(
        "--experimental-tutorial", action="store_true",
        help="enable graphics, USB, and keyboard for the current tutorial path",
    )
    run.add_argument(
        "--experimental-audio", action="store_true",
        help=argparse.SUPPRESS,
    )
    run.add_argument(
        "--no-audio", action="store_true",
        help="disable the default-on APU and host audio path",
    )
    run.add_argument(
        "--isolated-saves", metavar="DIR", type=Path,
        help="use one existing empty directory for both partition and user data",
    )
    args = parser.parse_args(argv)
    try:
        if args.command == "setup":
            setup_toolkit()
        elif args.command == "extract":
            extract_disc(args.disc)
        elif args.command == "generate":
            generate()
        elif args.command == "build":
            build(args.config, args.parallel, args.extra_target)
        elif args.command == "run":
            return run_game(
                args.config, args.graphics, args.experimental_tutorial, args.experimental_audio,
                args.isolated_saves, args.no_audio,
            )
        else:
            setup_toolkit()
            extract_disc(args.disc)
            generate()
            build(args.config, args.parallel, args.extra_target)
    except (BuildError, ToolkitError, OSError, subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
