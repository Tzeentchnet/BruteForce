"""Binary XML descriptors (``.xmb``, magic ``BXML``).

Follows the game's reader: header and tables at 0x254860, the element loop
at 0x255330, values at 0x254C60, and the compressed token stream at
0x281CC0/0x281BF0/0x281D30. See docs/ASSETFORMATS.md, "Layer 3".
"""

from dataclasses import dataclass, field
import struct

from . import FormatError
from .bxsd import (BOOLEAN, FLOAT, DOUBLE, STRING, TYPE_SIZE, WSTRING, MAX_DEPTH)
from .names import name_hash
from .stream import Reader

TOKEN_START, TOKEN_MARK, TOKEN_ATTR, TOKEN_EMPTY = 0x0, 0x9, 0xA, 0xB
TOKEN_END, TOKEN_VALUE = 0xC, 0xD
RING_WORDS = 512
MAX_BLOCK_WORDS = 0x1200  # the reader's block buffer, 0x404..0x2804
MAX_VALUE_COUNT = 1 << 26

_NUMERIC = {1: "?", 2: "b", 3: "b", 4: "B", 5: "h", 6: "H", 7: "i", 8: "I",
            9: "q", 10: "Q", 11: "f", 12: "d"}


class TokenStream:
    """16-bit LZSS token words, decompressed on demand.

    Blocks are ``u16 total_words, u32 output_words`` (u16 for versions other
    than 1.1) followed by payload words. The first payload word of a block is
    a flag word; bit ``i`` set means the next word is a back-reference
    ``offset = w & 0x1FF, length = (w >> 9) + 2`` into a 512-word ring, clear
    means a literal. Blocks are read from the same cursor as the values, only
    when the decoder needs them.
    """

    def __init__(self, reader, version):
        self.r = reader
        self.version = version
        self.block = []
        self.index = 0
        self.blocks = 0
        self.declared = []
        self.emitted = []
        self.ring = [0] * RING_WORDS
        self.pos = 1
        self.match_offset = self.match_left = 0
        self.flags = self._raw()[0]
        self.bit = 0

    def _load(self):
        total = self.r.u16()
        header = 3 if self.version == 0x0101 else 2
        if not header <= total <= MAX_BLOCK_WORDS:
            raise FormatError(f"token block of {total} words at offset {self.r.pos - 2}")
        words = struct.unpack(f"<{total - 1}H", self.r.read(2 * (total - 1)))
        self.block = (total,) + words
        out = words[0] | (words[1] << 16) if header == 3 else words[0]
        self.declared.append(out)
        self.emitted.append(0)
        self.index = header
        self.pos = 1
        self.match_left = 0
        self.blocks += 1

    def _raw(self):
        new = False
        if self.index >= len(self.block):
            self._load()
            new = True
            if self.index >= len(self.block):
                raise FormatError("empty token block")
        word = self.block[self.index]
        self.index += 1
        return word, new

    def _emit(self, word):
        self.ring[self.pos] = word
        self.pos = (self.pos + 1) % RING_WORDS
        self.emitted[-1] += 1
        return word

    def word(self):
        if self.match_left:
            self.match_left -= 1
            word = self.ring[self.match_offset % RING_WORDS]
            self.match_offset += 1
            return self._emit(word)
        if self.bit == 16:
            self.flags = self._raw()[0]
            self.bit = 0
        is_match = (self.flags >> self.bit) & 1
        self.bit += 1
        word, new = self._raw()
        if new:
            self.flags, self.bit = word, 1
            is_match = word & 1
            word = self._raw()[0]
        if not is_match:
            return self._emit(word)
        self.match_offset = word & 0x1FF
        self.match_left = (word >> 9) + 2
        return self.word()

    @property
    def unread_words(self):
        return len(self.block) - self.index


@dataclass
class Element:
    name: int
    attributes: list = field(default_factory=list)  # [(name, value)]
    children: list = field(default_factory=list)
    value: object = None
    marker: int | None = None  # the preceding kind-9 token's value, if any
    resolved: bool = True


@dataclass
class StringId:
    """A ``daxns:stringid`` value: the hash of a name."""
    hash: int


@dataclass
class EnumValue:
    index: int


@dataclass
class Document:
    version: int
    flags: int
    namespaces: list
    names: tuple
    root: Element
    blocks: int
    declared_words: list
    emitted_words: list


class _Decoder:
    def __init__(self, data, schemas):
        self.schemas = schemas
        self.r = Reader(data)
        head = self.r.read(16)
        if head[:4] != b"BXML":
            raise FormatError("missing BXML magic")
        if head[5] * 0.1 + head[4] > 1.1 + 1e-6:
            raise FormatError(f"unsupported BXML version {head[4]}.{head[5]}")
        self.version = (head[4] << 8) | head[5]
        self.flags = head[6]
        table = int.from_bytes(head[8:12], "little")
        self.namespaces = [(self._name(), self._name()) for _ in range(self.r.leb128())]
        self.r.seek(table)
        count = self.r.leb128()
        if count * 4 > self.r.remaining:
            raise FormatError(f"name table of {count} entries passes the end")
        self.names = struct.unpack(f"<{count}I", self.r.read(4 * count))
        self.default_ns = dict(self.namespaces).get(0)
        self.tokens = TokenStream(self.r, self.version)

    def _name(self):
        # 0x232A40: raw hash when header flag bit 0 is set, else a string.
        if self.flags & 1:
            return self.r.u32()
        length = self.r.leb128()
        return name_hash(self.r.read(length)) if length else 0

    def _name_at(self, token):
        index = token & 0xFFF
        if index >= len(self.names):
            raise FormatError(f"name index {index} outside a {len(self.names)}-entry table")
        return self.names[index]

    def _count(self, token):
        value = token & 0xFFF
        if token & 0x800:
            shift = 11
            while True:
                word = self.tokens.word()
                value |= (word & 0x7FFF) << shift
                shift += 15
                if not word & 0x8000:
                    break
                if shift > 41:
                    raise FormatError("extended count too long")
        return value

    def _value(self, decl, count, where):
        vt = self.schemas.value_type(decl)
        if vt is None:
            raise FormatError(f"{where}: no value type in the schema")
        if vt.code in (STRING, WSTRING):
            if self.flags & 3 and vt.special == "stringid":
                return StringId(self.r.u32())
            if self.flags & 3 and vt.special == "enum":
                return EnumValue(self.r.leb128() - 1)
            raw = self.r.read(self.r.leb128())
            return raw.decode("utf-16-le" if vt.code == WSTRING else "latin-1")
        if count > MAX_VALUE_COUNT:
            raise FormatError(f"{where}: value count {count} too large")
        size = TYPE_SIZE[vt.code]
        raw = self.r.read(count * size)
        fmt = _NUMERIC.get(vt.code)
        if fmt is None:
            return raw
        items = list(struct.unpack(f"<{count}{fmt}", raw))
        if vt.code in (FLOAT, DOUBLE):
            items = [float(x) for x in items]
        if vt.code == BOOLEAN:
            items = [bool(x) for x in items]
        return items[0] if count == 1 and not vt.is_list else items

    def element(self, decl, name, marker, depth):
        if depth > MAX_DEPTH * 4:
            raise FormatError("elements nest too deeply")
        node = Element(name, marker=marker, resolved=decl is not None)
        pending = None
        while True:
            token = self.tokens.word()
            kind = token >> 12
            if kind == TOKEN_END:
                return node
            if kind in (TOKEN_START, TOKEN_EMPTY):
                child_name = self._name_at(token)
                child = self.schemas.member(decl, child_name)
                if child is None:
                    raise FormatError(f"no declaration for element #{child_name:08x}")
                if kind == TOKEN_START:
                    node.children.append(self.element(child, child_name, pending, depth + 1))
                else:
                    node.children.append(Element(child_name, marker=pending))
                pending = None
            elif kind == TOKEN_ATTR:
                attr_name = self._name_at(token)
                attr = self.schemas.member(decl, attr_name)
                if attr is None:
                    raise FormatError(f"no declaration for attribute #{attr_name:08x}")
                node.attributes.append(
                    (attr_name, self._value(attr, 1, f"attribute #{attr_name:08x}")))
            elif kind == TOKEN_VALUE:
                node.value = self._value(decl, self._count(token), f"element #{name:08x}")
            elif kind == TOKEN_MARK:
                pending = self._count(token)
            # The game's loop ignores every other kind.

    def document(self):
        token = self.tokens.word()
        if token >> 12 != TOKEN_START:
            raise FormatError(f"document starts with token {token:#06x}")
        name = self._name_at(token)
        decl = self.schemas.root(self.default_ns, name)
        if decl is None:
            raise FormatError(f"no root declaration for #{name:08x}")
        root = self.element(decl, name, None, 0)
        return Document(self.version, self.flags, self.namespaces, self.names, root,
                        self.tokens.blocks, self.tokens.declared, self.tokens.emitted)


def parse_document(data, schemas, strict=True):
    """Decode a BXML file against a SchemaSet.

    With ``strict``, every byte and every token word must be consumed, and
    each block must emit exactly the word count its header declares.
    """
    decoder = _Decoder(data, schemas)
    doc = decoder.document()
    if strict:
        if decoder.r.remaining:
            raise FormatError(f"{decoder.r.remaining} bytes left after the document")
        if decoder.tokens.unread_words:
            raise FormatError(f"{decoder.tokens.unread_words} token words left unread")
        if doc.declared_words != doc.emitted_words:
            raise FormatError(
                f"block word counts {doc.declared_words} != emitted {doc.emitted_words}")
    return doc
