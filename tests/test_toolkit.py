from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import toolkit


def test_verify_checkout_option_is_read_only(monkeypatch, capsys):
    seen = []
    monkeypatch.setattr(toolkit, "verify_toolkit", lambda path: seen.append(path))
    monkeypatch.setattr(toolkit, "setup_toolkit", lambda: pytest.fail("unexpected setup"))
    assert toolkit.main(["--verify", "--checkout", "build\\isolated-toolkit"]) == 0
    assert seen == [Path("build\\isolated-toolkit")]
    assert "Verified xboxrecomp checkout" in capsys.readouterr().out


def test_verify_defaults_to_pinned_checkout(monkeypatch):
    seen = []
    monkeypatch.setattr(toolkit, "verify_toolkit", lambda path: seen.append(path))
    assert toolkit.main(["--verify"]) == 0
    assert seen == [toolkit.TOOLKIT_DIR]


def test_checkout_option_cannot_trigger_setup(monkeypatch):
    monkeypatch.setattr(toolkit, "setup_toolkit", lambda: pytest.fail("unexpected setup"))
    with pytest.raises(SystemExit) as exc:
        toolkit.main(["--checkout", "build\\isolated-toolkit"])
    assert exc.value.code == 2
