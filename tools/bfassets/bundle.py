"""Safe, read-only access to the game's ``data\\*.tgz`` resource bundles."""

from pathlib import PurePosixPath
import tarfile

from . import FormatError

# Larger than any member observed on the retail disc, small enough to refuse
# a corrupt or hostile size before allocating it.
MAX_MEMBER_SIZE = 256 * 1024 * 1024


def _check_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise FormatError(f"unsafe member name {name!r}")


def iter_members(bundle_path, suffixes=None, include_empty=False):
    """Yield ``(name, data)`` for regular members, without extracting files.

    Links, devices and directories are skipped; unsafe names and oversized
    members raise FormatError. Empty members are "no data" and are skipped
    unless ``include_empty`` is set.
    """
    with tarfile.open(bundle_path, "r:gz") as archive:
        for member in archive:
            if not member.isfile():
                continue
            _check_name(member.name)
            if suffixes and not member.name.lower().endswith(tuple(suffixes)):
                continue
            if member.size > MAX_MEMBER_SIZE:
                raise FormatError(f"{member.name}: member too large ({member.size} bytes)")
            if member.size == 0 and not include_empty:
                continue
            handle = archive.extractfile(member)
            data = handle.read() if handle else b""
            if len(data) != member.size:
                raise FormatError(f"{member.name}: truncated member")
            yield member.name, data


def read_member(bundle_path, name):
    """Return the bytes of one named member."""
    for member_name, data in iter_members(bundle_path, include_empty=True):
        if member_name == name:
            return data
    raise KeyError(name)
