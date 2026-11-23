"""Blender-independent readers for Brute Force resource formats.

The package uses only the Python standard library so that it can run both in
the project's test runner and in Blender's bundled interpreter. See
docs/ASSETFORMATS.md for the formats and the evidence behind them.
"""


class FormatError(ValueError):
    """Raised when a resource does not match the decoded format."""
