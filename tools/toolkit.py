#!/usr/bin/env python3
"""Install and verify the pinned xboxrecomp revision and selected fixes."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
TOOLKIT_DIR = ROOT / ".deps" / "xboxrecomp"
TOOLKIT_URL = "https://github.com/sp00nznet/xboxrecomp.git"
TOOLKIT_BASE = "f0f8a461496a4f23d9b0aeaa284b8d70ba7ab82b"
TOOLKIT_TREE = "0d2dbaf3daa7daaccdd9771068ae6c89f1639c8f"
LOCAL_PATCHES = (
    ROOT / "patches" / "xboxrecomp" / "0001-bound-extra-thunks.patch",
    ROOT / "patches" / "xboxrecomp" / "0002-model-wbinvd.patch",
    ROOT / "patches" / "xboxrecomp" / "0003-synchronize-semaphore-target.patch",
    ROOT / "patches" / "xboxrecomp" / "0004-gate-read-tracing.patch",
    ROOT / "patches" / "xboxrecomp" / "0005-synchronize-presented-frames.patch",
    ROOT / "patches" / "xboxrecomp" / "0006-harden-usb-lifecycle.patch",
    ROOT / "patches" / "xboxrecomp" / "0007-account-guest-memory.patch",
    ROOT / "patches" / "xboxrecomp" / "0008-experimental-pgraph-and-staged-walker.patch",
    ROOT / "patches" / "xboxrecomp" / "0009-experimental-window-clipping.patch",
    ROOT / "patches" / "xboxrecomp" / "0010-experimental-face-culling.patch",
    ROOT / "patches" / "xboxrecomp" / "0011-experimental-program-near-plane.patch",
    ROOT / "patches" / "xboxrecomp" / "0012-vectorize-color-unpack.patch",
    ROOT / "patches" / "xboxrecomp" / "0013-optimize-texture-sampling.patch",
    ROOT / "patches" / "xboxrecomp" / "0014-frame-statistics.patch",
    ROOT / "patches" / "xboxrecomp" / "0015-gpu-census-and-ceiling-probe.patch",
    ROOT / "patches" / "xboxrecomp" / "0016-extended-render-backend.patch",
    ROOT / "patches" / "xboxrecomp" / "0017-gpu-vertex-programs.patch",
    ROOT / "patches" / "xboxrecomp" / "0018-async-visibility-reports.patch",
    ROOT / "patches" / "xboxrecomp" / "0019-cache-hot-getenv.patch",
    ROOT / "patches" / "xboxrecomp" / "0020-texture-metadata.patch",
    ROOT / "patches" / "xboxrecomp" / "0021-gpu-window-clip.patch",
    ROOT / "patches" / "xboxrecomp" / "0022-gpu-depth-clear-offload.patch",
    ROOT / "patches" / "xboxrecomp" / "0023-flip-only-frame-captures.patch",
    ROOT / "patches" / "xboxrecomp" / "0024-gpu-shared-presentation.patch",
    ROOT / "patches" / "xboxrecomp" / "0025-gpu-audit.patch",
    ROOT / "patches" / "xboxrecomp" / "0026-flip-id-frame-captures.patch",
    ROOT / "patches" / "xboxrecomp" / "0027-presenter-source-captures.patch",
    ROOT / "patches" / "xboxrecomp" / "0028-scaled-gpu-presentation.patch",
    ROOT / "patches" / "xboxrecomp" / "0029-hud-safe-near-plane.patch",
    ROOT / "patches" / "xboxrecomp" / "0030-accurate-apu-dsp-hooks.patch",
    ROOT / "patches" / "xboxrecomp" / "0031-external-apu-dsp-lifecycle.patch",
    ROOT / "patches" / "xboxrecomp" / "0032-run-hooks-before-firmware-exit.patch",
    ROOT / "patches" / "xboxrecomp" / "0033-prioritize-apu-frame-worker.patch",
    ROOT / "patches" / "xboxrecomp" / "0034-raise-apu-queue-low-water.patch",
)

# PRs #262, #280, #277, #278, and #264, in their tested application order.
PATCH_COMMITS = (
    "19871ddc1324e0b448cb4e94ed89608b86a69078",
    "8aadd8e768de8d3292789fc02aee7a0ae8bc7016",
    "9e680cc137a9af26d93a99b3e758bd9ac996ff09",
    "47ea2b6323c145be5419246debc190aa2e91b7c0",
    "4998bcbb9cc97c044df4eca4ea372dfcac466d2a",
    "60d1456910f3d65bc599cf240bc0e1282770dec5",
    "0dff83abd7bab6a0ebb899ef5c2be63317dae228",
    "eb7b1f5833205a24fcc0eda6eb4ed2df4f2bc2e3",
)


class ToolkitError(Exception):
    pass


def git(repo, *args, capture=False):
    command = ["git"]
    if repo is not None:
        command += ["-C", str(repo)]
    command.extend(args)
    result = subprocess.run(
        command, check=True, text=True, capture_output=capture, stdin=subprocess.DEVNULL
    )
    return result.stdout.strip() if capture else None


CODEGEN_SUBTREES = ("tools", "templates")


def codegen_tree(path=TOOLKIT_DIR, tree=TOOLKIT_TREE):
    """Identify the toolkit parts that generated code depends on.

    The recompiler lives in tools/ and copies templates/runtime/recomp_types.h
    into each generation. Runtime-only patches change neither, so they need a
    rebuild but not a regeneration.
    """
    return ",".join(
        f"{sub}={git(path, 'rev-parse', f'{tree}:{sub}', capture=True)}"
        for sub in CODEGEN_SUBTREES
    )


def verify_toolkit(path=TOOLKIT_DIR):
    path = Path(path)
    if not (path / ".git").is_dir():
        raise ToolkitError(f"Not an xboxrecomp Git checkout: {path}")

    head = git(path, "rev-parse", "HEAD", capture=True)
    tree = git(path, "write-tree", capture=True)
    if head != TOOLKIT_BASE or tree != TOOLKIT_TREE:
        raise ToolkitError(
            f"Unexpected xboxrecomp revision or patch set in {path} "
            f"(HEAD {head}, index tree {tree}). Preserve or move the checkout "
            "before running setup again."
        )

    result = subprocess.run(
        ["git", "-C", str(path), "diff", "--quiet"],
        stdin=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        raise ToolkitError(
            f"xboxrecomp working files differ from the verified index in {path} "
            f"(git diff exit {result.returncode})."
        )

    status = git(path, "status", "--porcelain", "--untracked-files=all", capture=True)
    if any(line.startswith("?? ") for line in status.splitlines()):
        raise ToolkitError(f"Untracked files in the xboxrecomp checkout: {path}")
    return path


def setup_toolkit(path=TOOLKIT_DIR):
    path = Path(path)
    if path.exists():
        verify_toolkit(path)
        print(f"Using verified xboxrecomp checkout: {path}", flush=True)
        return path

    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xboxrecomp-setup-", dir=path.parent) as temp:
        checkout = Path(temp) / "checkout"
        git(None, "clone", "-c", "core.autocrlf=false", "--no-checkout", TOOLKIT_URL, str(checkout))
        git(checkout, "checkout", "--detach", TOOLKIT_BASE)
        git(checkout, "fetch", "--no-tags", "origin", *PATCH_COMMITS)
        for commit in PATCH_COMMITS:
            print(f"Applying upstream commit {commit[:12]}", flush=True)
            git(checkout, "cherry-pick", "--no-commit", commit)
        for patch in LOCAL_PATCHES:
            print(f"Applying local patch {patch.name}", flush=True)
            git(checkout, "apply", "--index", str(patch))
        verify_toolkit(checkout)
        # Windows file watchers can hold a checkout open and prevent a directory
        # rename. copytree refuses an existing destination rather than replacing it.
        shutil.copytree(checkout, path)
        verify_toolkit(path)

    print(f"Installed verified xboxrecomp checkout: {path}", flush=True)
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="verify an existing checkout only")
    args = parser.parse_args(argv)
    try:
        if args.verify:
            verify_toolkit()
            print(f"Verified xboxrecomp checkout: {TOOLKIT_DIR}")
        else:
            setup_toolkit()
    except (ToolkitError, OSError, subprocess.CalledProcessError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
