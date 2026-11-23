"""The case-insensitive name hash used by BXML and BXSD.

The executable lowercases each byte and runs a reflected CRC-32 whose table
was generated with an arithmetic (sign-extending) right shift. See
docs/ASSETFORMATS.md, "Layer 2: name hashing".
"""


def _make_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            shifted = (c >> 1) | (c & 0x80000000)
            c = shifted ^ 0xEDB88320 if c & 1 else shifted
        table.append(c)
    return tuple(table)


TABLE = _make_table()


def crc_update(state, data):
    """Advance a raw CRC state over already-lowercased bytes."""
    for b in data:
        state = TABLE[(b ^ state) & 0xFF] ^ (state >> 8)
    return state


def name_hash(name):
    """Return the 32-bit hash of a str or bytes name."""
    if isinstance(name, str):
        name = name.encode("latin-1")
    return crc_update(0xFFFFFFFF, bytes(name).lower()) ^ 0xFFFFFFFF


def label(value, names=None):
    """Return a readable label: a known name, or ``#xxxxxxxx``."""
    if names:
        known = names.get(value)
        if known:
            return known
    return f"#{value:08x}"
