import hashlib
import json
from pathlib import Path
import sys
import zipfile

import pytest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import dependencies


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def make_dxc_archive(path, required, unsafe=False):
    with zipfile.ZipFile(path, "w") as archive:
        for relative in required:
            archive.writestr(relative.as_posix(), f"fixture:{relative}\n")
        if unsafe:
            archive.writestr("../outside.txt", "unsafe")


def configure_dxc_fixture(tmp_path, monkeypatch, unsafe=False):
    archive = tmp_path / "cache" / "dxc.zip"
    archive.parent.mkdir()
    required = (
        Path("bin/x64/dxc.exe"),
        Path("bin/x64/dxcompiler.dll"),
        Path("bin/x64/dxil.dll"),
        Path("inc/dxcapi.h"),
        Path("LICENSE.txt"),
    )
    make_dxc_archive(archive, required, unsafe)
    monkeypatch.setattr(dependencies, "DXC_DIR", tmp_path / "dxc")
    monkeypatch.setattr(dependencies, "DXC_ARCHIVE", archive)
    monkeypatch.setattr(dependencies, "DXC_ARCHIVE_NAME", archive.name)
    monkeypatch.setattr(dependencies, "DXC_REQUIRED", required)
    monkeypatch.setattr(dependencies, "DXC_SHA256", sha256(archive))
    return archive


def test_dxc_setup_extracts_only_verified_archive(tmp_path, monkeypatch):
    archive = configure_dxc_fixture(tmp_path, monkeypatch)
    installed = dependencies.setup_dxc(tmp_path / "dxc")
    assert dependencies.verify_dxc(installed) == installed
    marker = json.loads((installed / ".bruteforce-dependency.json").read_text())
    assert marker == {
        "tag": dependencies.DXC_TAG,
        "archive": archive.name,
        "sha256": sha256(archive),
    }
    executable = installed / dependencies.DXC_REQUIRED[0]
    original = executable.read_bytes()
    executable.write_bytes(b"tampered")
    with pytest.raises(dependencies.DependencyError, match="differs from the pinned archive"):
        dependencies.verify_dxc(installed)
    executable.write_bytes(original)
    archive.write_bytes(archive.read_bytes() + b"corrupt")
    with pytest.raises(dependencies.DependencyError, match="SHA-256 mismatch"):
        dependencies.verify_dxc(installed)


def test_dxc_setup_rejects_archive_traversal(tmp_path, monkeypatch):
    configure_dxc_fixture(tmp_path, monkeypatch, unsafe=True)
    with pytest.raises(dependencies.DependencyError, match="unsafe path"):
        dependencies.setup_dxc(tmp_path / "dxc")
    assert not (tmp_path / "outside.txt").exists()
