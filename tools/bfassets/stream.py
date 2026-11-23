"""A bounds-checked little-endian byte reader."""

import struct

from . import FormatError


class Reader:
    def __init__(self, data, pos=0):
        self.data = bytes(data)
        self.pos = pos

    @property
    def remaining(self):
        return len(self.data) - self.pos

    def read(self, size):
        if size < 0 or size > self.remaining:
            raise FormatError(
                f"read of {size} bytes at offset {self.pos} passes the end ({len(self.data)})"
            )
        out = self.data[self.pos:self.pos + size]
        self.pos += size
        return out

    def u8(self):
        return self.read(1)[0]

    def u16(self):
        return struct.unpack("<H", self.read(2))[0]

    def u32(self):
        return struct.unpack("<I", self.read(4))[0]

    def leb128(self):
        """Unsigned LEB128, as read by the game's routine at 0x252CA0."""
        value = shift = 0
        while True:
            if shift > 35:
                raise FormatError(f"LEB128 value too long at offset {self.pos}")
            byte = self.u8()
            value |= (byte & 0x7F) << shift
            shift += 7
            if byte < 0x80:
                return value

    def seek(self, offset):
        if not 0 <= offset <= len(self.data):
            raise FormatError(f"offset {offset} outside a {len(self.data)}-byte file")
        self.pos = offset
