import json
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
HOST_EXE = os.environ.get("BRUTEFORCE_TEST_HOST")
pytestmark = pytest.mark.skipif(not HOST_EXE, reason="set BRUTEFORCE_TEST_HOST to a synthetic fixture build")


def run_host(directory, save_root=None):
    env = os.environ.copy()
    env.pop("BRUTEFORCE_SAVE_ROOT", None)
    env.pop("RECOMP_SAVE_DIR", None)
    if save_root is not None:
        env["BRUTEFORCE_SAVE_ROOT"] = str(save_root)
    return subprocess.run(
        [HOST_EXE, str(directory)], capture_output=True, text=True, check=False, env=env
    )


def test_missing_executable_is_an_error(tmp_path):
    result = run_host(tmp_path)
    assert result.returncode != 0
    assert "Cannot open game executable" in result.stderr


def test_wrong_executable_size_is_an_error(tmp_path):
    (tmp_path / "default.xbe").write_bytes(b"not an XBE")
    result = run_host(tmp_path)
    assert result.returncode != 0
    assert "Unsupported default.xbe size" in result.stderr


def test_wrong_executable_hash_is_an_error(tmp_path):
    fingerprint = json.loads((ROOT / "fingerprints" / "retail.json").read_text(encoding="utf-8"))
    (tmp_path / "default.xbe").write_bytes(bytes(fingerprint["xbe_size"]))
    result = run_host(tmp_path)
    assert result.returncode != 0
    assert "Unsupported default.xbe SHA-256" in result.stderr


@pytest.mark.parametrize("kind", ["missing", "file"])
def test_save_root_must_be_an_existing_directory(tmp_path, kind):
    root = tmp_path / "save-root"
    if kind == "file":
        root.write_text("not a directory", encoding="utf-8")
    result = run_host(tmp_path, root)
    assert result.returncode != 0
    assert "BRUTEFORCE_SAVE_ROOT must be an existing" in result.stderr


def test_save_root_accepts_a_unicode_directory(tmp_path):
    root = tmp_path / "saves-\u65e5"
    root.mkdir()
    result = run_host(tmp_path, root)
    assert result.returncode != 0
    assert "Cannot open game executable" in result.stderr


def test_save_root_rejects_an_empty_value(tmp_path):
    result = run_host(tmp_path, "")
    assert result.returncode != 0
    assert "BRUTEFORCE_SAVE_ROOT must name an existing directory" in result.stderr


def test_save_root_rejects_paths_without_save_path_room(tmp_path):
    root = tmp_path / ("r" * 60) / ("s" * 60)
    root.mkdir(parents=True)
    result = run_host(tmp_path, root)
    assert result.returncode != 0
    assert "BRUTEFORCE_SAVE_ROOT must be an existing directory with room" in result.stderr
