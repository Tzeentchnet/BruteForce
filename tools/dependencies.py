#!/usr/bin/env python3
"""Install and verify pinned Vulkan build dependencies."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import Request, urlopen
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEPS_DIR = ROOT / ".deps"

VULKAN_HEADERS_DIR = DEPS_DIR / "vulkan-headers"
VULKAN_HEADERS_URL = "https://github.com/KhronosGroup/Vulkan-Headers.git"
VULKAN_HEADERS_TAG = "vulkan-sdk-1.4.363.0"
VULKAN_HEADERS_COMMIT = "6802bb4733b63ed5efd3adb308a6c885ef180ea1"
VULKAN_HEADERS_TREE = "a808531b8090c3c1b2d52c2b010c7f9f68bf169d"

DXC_DIR = DEPS_DIR / "dxc"
DXC_TAG = "v1.9.2609"
DXC_ARCHIVE_NAME = "dxc_2026_09_29.zip"
DXC_URL = (
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/"
    f"{DXC_TAG}/{DXC_ARCHIVE_NAME}"
)
DXC_SHA256 = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"
DXC_ARCHIVE = DEPS_DIR / "cache" / DXC_ARCHIVE_NAME
DXC_EXECUTABLE = Path("bin/x64/dxc.exe")
DXC_REQUIRED = (
    DXC_EXECUTABLE,
    Path("bin/x64/dxcompiler.dll"),
    Path("bin/x64/dxil.dll"),
    Path("inc/dxcapi.h"),
    Path("LICENCE-MIT.txt"),
    Path("LICENSE-LLVM.txt"),
    Path("LICENSE-MS.txt"),
)


class DependencyError(Exception):
    pass


def _git(path, *args):
    return subprocess.run(
        ["git", "-C", str(path), *args],
        check=True,
        text=True,
        capture_output=True,
        stdin=subprocess.DEVNULL,
    ).stdout.strip()


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def _stream_sha256(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def verify_vulkan_headers(path=VULKAN_HEADERS_DIR):
    path = Path(path)
    if not (path / ".git").is_dir():
        raise DependencyError(f"Not a Vulkan-Headers checkout: {path}")
    head = _git(path, "rev-parse", "HEAD")
    tree = _git(path, "write-tree")
    if head != VULKAN_HEADERS_COMMIT or tree != VULKAN_HEADERS_TREE:
        raise DependencyError(
            f"Unexpected Vulkan-Headers revision or tree in {path} "
            f"(HEAD {head}, tree {tree})"
        )
    if subprocess.run(
        ["git", "-C", str(path), "diff", "--quiet"],
        check=False,
        stdin=subprocess.DEVNULL,
    ).returncode:
        raise DependencyError(f"Vulkan-Headers working files differ from the pin in {path}")
    status = _git(path, "status", "--porcelain", "--untracked-files=all")
    if status:
        raise DependencyError(f"Unexpected files in Vulkan-Headers checkout: {path}")
    return path


def setup_vulkan_headers(path=VULKAN_HEADERS_DIR):
    path = Path(path)
    if path.exists():
        return verify_vulkan_headers(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vulkan-headers-", dir=path.parent) as temp:
        checkout = Path(temp) / "checkout"
        subprocess.run(
            ["git", "clone", "--filter=blob:none", "--no-checkout",
             VULKAN_HEADERS_URL, str(checkout)],
            check=True,
            stdin=subprocess.DEVNULL,
        )
        subprocess.run(
            ["git", "-C", str(checkout), "checkout", "--detach", VULKAN_HEADERS_COMMIT],
            check=True,
            stdin=subprocess.DEVNULL,
        )
        verify_vulkan_headers(checkout)
        shutil.copytree(checkout, path)
    return verify_vulkan_headers(path)


def _download(url, destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    request = Request(url, headers={"User-Agent": "BruteForce dependency setup"})
    with urlopen(request, timeout=120) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def _verify_dxc_archive(path=None):
    path = Path(DXC_ARCHIVE if path is None else path)
    if not path.is_file():
        raise DependencyError(f"Missing pinned DXC archive: {path}")
    actual = _sha256(path)
    if actual != DXC_SHA256:
        raise DependencyError(
            f"DXC archive SHA-256 mismatch: expected {DXC_SHA256}, found {actual}"
        )
    return path


def verify_dxc(path=DXC_DIR, archive=None):
    path = Path(path)
    archive = DXC_ARCHIVE if archive is None else archive
    archive = _verify_dxc_archive(archive)
    marker = path / ".bruteforce-dependency.json"
    if not marker.is_file():
        raise DependencyError(f"Missing DXC dependency marker: {marker}")
    try:
        metadata = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DependencyError(f"Cannot read DXC dependency marker: {exc}") from exc
    expected = {"tag": DXC_TAG, "archive": DXC_ARCHIVE_NAME, "sha256": DXC_SHA256}
    if metadata != expected:
        raise DependencyError(f"Unexpected DXC dependency metadata in {marker}")
    missing = [str(relative) for relative in DXC_REQUIRED if not (path / relative).is_file()]
    if missing:
        raise DependencyError(f"DXC extraction is incomplete: {', '.join(missing)}")
    with zipfile.ZipFile(archive) as source:
        for relative in DXC_REQUIRED:
            name = relative.as_posix()
            try:
                with source.open(name) as archived:
                    archived_digest = _stream_sha256(archived)
            except KeyError as exc:
                raise DependencyError(f"Pinned DXC archive is missing {name}") from exc
            if _sha256(path / relative) != archived_digest:
                raise DependencyError(
                    f"DXC extraction differs from the pinned archive: {relative}"
                )
    return path


def setup_dxc(path=DXC_DIR):
    path = Path(path)
    if path.exists():
        return verify_dxc(path)
    if not DXC_ARCHIVE.is_file():
        partial = DXC_ARCHIVE.with_suffix(DXC_ARCHIVE.suffix + ".part")
        try:
            _download(DXC_URL, partial)
            if _sha256(partial) != DXC_SHA256:
                raise DependencyError("Downloaded DXC archive failed SHA-256 verification")
            partial.replace(DXC_ARCHIVE)
        finally:
            if partial.exists():
                partial.unlink()
    _verify_dxc_archive()
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="dxc-", dir=path.parent) as temp:
        extracted = Path(temp) / "extracted"
        extracted.mkdir()
        with zipfile.ZipFile(DXC_ARCHIVE) as archive:
            root = extracted.resolve()
            for member in archive.infolist():
                target = (extracted / member.filename).resolve()
                if target != root and root not in target.parents:
                    raise DependencyError(
                        f"DXC archive contains an unsafe path: {member.filename}"
                    )
            archive.extractall(extracted)
        (extracted / ".bruteforce-dependency.json").write_text(
            json.dumps(
                {"tag": DXC_TAG, "archive": DXC_ARCHIVE_NAME, "sha256": DXC_SHA256},
                indent=2,
            ) + "\n",
            encoding="utf-8",
        )
        shutil.copytree(extracted, path)
    return verify_dxc(path)


def setup_dependencies():
    headers = setup_vulkan_headers()
    dxc = setup_dxc()
    print(f"Installed Vulkan-Headers {VULKAN_HEADERS_TAG}")
    print(f"Installed DirectX Shader Compiler {DXC_TAG}")
    return headers, dxc / DXC_EXECUTABLE


def verify_dependencies(vulkan_headers=None, dxc=None, dxc_archive=None):
    vulkan_headers = VULKAN_HEADERS_DIR if vulkan_headers is None else vulkan_headers
    dxc = DXC_DIR if dxc is None else dxc
    dxc_archive = DXC_ARCHIVE if dxc_archive is None else dxc_archive
    headers = verify_vulkan_headers(vulkan_headers)
    dxc_dir = verify_dxc(dxc, dxc_archive)
    print(f"Verified Vulkan-Headers {VULKAN_HEADERS_TAG}")
    print(f"Verified DirectX Shader Compiler {DXC_TAG}")
    return headers, dxc_dir / DXC_EXECUTABLE


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="verify without installing")
    parser.add_argument("--vulkan-headers-dir", type=Path, default=VULKAN_HEADERS_DIR,
                        help="Vulkan-Headers checkout to verify")
    parser.add_argument("--dxc-dir", type=Path, default=DXC_DIR,
                        help="extracted DXC directory to verify")
    args = parser.parse_args(argv)
    try:
        if args.verify:
            verify_dependencies(args.vulkan_headers_dir, args.dxc_dir)
        else:
            if (args.vulkan_headers_dir != VULKAN_HEADERS_DIR or
                    args.dxc_dir != DXC_DIR):
                parser.error("custom dependency directories require --verify")
            setup_dependencies()
    except (DependencyError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
