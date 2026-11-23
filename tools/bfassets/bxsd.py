"""Binary schemas (``.xsb``, magic ``BXSD``) and the type resolution BXML needs.

Grammar from the game's loader at 0x281FE0, its declaration reader
0x281F10/0x254620, and the type registration at 0x2371C1. See
docs/ASSETFORMATS.md, "Layer 4: BXSD schemas".
"""

from dataclasses import dataclass, field

from . import FormatError
from .names import name_hash
from .stream import Reader

XS_NS = name_hash("http://www.w3.org/2001/XMLSchema")
DAXNS = name_hash("daxns")

# Value type codes and element sizes (the table at 0x4063D0).
TYPE_SIZE = (0, 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 1, 2)
BOOLEAN, BYTE, UBYTE, SHORT, USHORT, INT, UINT = 1, 3, 4, 5, 6, 7, 8
INT64, UINT64, FLOAT, DOUBLE, STRING, WSTRING = 9, 10, 11, 12, 13, 14

# Built-in types registered before any schema (the table at 0x406CB8).
BUILTIN_TYPES = {
    (XS_NS, name_hash("boolean")): BOOLEAN,
    (XS_NS, name_hash("byte")): BYTE,
    (XS_NS, name_hash("unsignedByte")): UBYTE,
    (XS_NS, name_hash("short")): SHORT,
    (XS_NS, name_hash("unsignedShort")): USHORT,
    (XS_NS, name_hash("int")): INT,
    (XS_NS, name_hash("unsignedInt")): UINT,
    (XS_NS, name_hash("float")): FLOAT,
    (XS_NS, name_hash("double")): DOUBLE,
    (XS_NS, name_hash("string")): STRING,
    (DAXNS, name_hash("stringid")): STRING,
    (DAXNS, name_hash("wstring")): WSTRING,
    (DAXNS, name_hash("user-typedef")): INT,
    (DAXNS, name_hash("enum-value")): STRING,
}
STRINGID = (DAXNS, name_hash("stringid"))
ENUM_VALUE = (DAXNS, name_hash("enum-value"))

# The game's reader compares element names with this hash (0x255352,
# 0x255087). In the schemas it names flag-0x80 members whose own members are
# simple values. Its text name is unknown.
GLOBAL_GROUP = 0x1B8535F0

FLAG_LIST = 0x04
MAX_DEPTH = 64


@dataclass
class Decl:
    flags: int
    ns: int
    name: int | None
    base: tuple | None
    children: list = field(default_factory=list)


@dataclass
class Schema:
    version: tuple
    unknown: int
    target_ns: int
    namespaces: list
    decls: list


def _qname(reader):
    tag = reader.u8()
    if tag == 0x00:
        return None
    if tag == 0x21:
        return (None, reader.u32())
    if tag == 0x22:
        return (reader.u32(), reader.u32())
    raise FormatError(f"unknown QName tag {tag:#04x} at offset {reader.pos - 1}")


def parse_schema(data):
    """Parse a BXSD file; the whole file must be consumed."""
    r = Reader(data)
    head = r.read(16)
    if head[:4] != b"BXSD":
        raise FormatError("missing BXSD magic")
    version = (head[4], head[5])
    if head[5] * 0.1 + head[4] > 1.0 + 1e-6:
        raise FormatError(f"unsupported BXSD version {version}")
    if head[6] == 0:
        raise FormatError("BXSD with text names is not supported (not observed)")
    tree_offset = int.from_bytes(head[8:12], "little")
    unknown = r.leb128()
    target = _qname(r)
    if target is None or target[0] is not None:
        raise FormatError("BXSD target namespace must be a local name")
    count = r.leb128()
    namespaces = [(r.u32(), r.u32()) for _ in range(count)]
    if r.pos != tree_offset:
        raise FormatError(f"declarations start at {r.pos}, header says {tree_offset}")
    prefixes = dict(namespaces)
    tns = target[1]

    def resolve(q):
        if q is None:
            return None
        ns, local = q
        return (tns if ns is None else prefixes.get(ns, ns), local)

    def decls(depth):
        if depth > MAX_DEPTH:
            raise FormatError("BXSD declarations nest too deeply")
        out = []
        for _ in range(r.leb128()):
            flags = r.u16()
            name = _qname(r)
            base = _qname(r)
            out.append(Decl(flags, tns, None if name is None else name[1], resolve(base),
                            decls(depth + 1)))
        return out

    tree = decls(0)
    if r.remaining:
        raise FormatError(f"{r.remaining} unexplained bytes after the declarations")
    return Schema(version, unknown, tns, namespaces, tree)


@dataclass
class ValueType:
    code: int
    is_list: bool
    special: str | None  # "stringid", "enum" or None


class SchemaSet:
    """Top-level declarations of several schemas, keyed by (namespace, name)."""

    def __init__(self, schemas=()):
        self.types = {}
        for schema in schemas:
            self.add(schema)

    def add(self, schema):
        for decl in schema.decls:
            self.types.setdefault((decl.ns, decl.name), decl)

    def root(self, default_ns, name):
        decl = self.types.get((default_ns, name))
        if decl is None:
            decl = next((d for (_, n), d in self.types.items() if n == name), None)
        return decl

    def value_type(self, decl):
        """The simple value type of a declaration, following its base chain."""
        if decl is None or decl.base is None:
            return None
        is_list = bool(decl.flags & FLAG_LIST)
        special = None
        q = decl.base
        for _ in range(MAX_DEPTH):
            if q == ENUM_VALUE:
                special = "enum"
            if q == STRINGID:
                special = "stringid"
            if q in BUILTIN_TYPES:
                return ValueType(BUILTIN_TYPES[q], is_list, special)
            d = self.types.get(q)
            if d is None:
                raise FormatError(f"unknown type #{q[0]:08x}:#{q[1]:08x}")
            is_list = is_list or bool(d.flags & FLAG_LIST)
            if d.base is None:
                return None
            q = d.base
        raise FormatError("type derivation chain too long")

    def members(self, decl):
        seen = set()
        while decl is not None and id(decl) not in seen:
            seen.add(id(decl))
            yield from decl.children
            if decl.base is None or decl.base in BUILTIN_TYPES:
                return
            decl = self.types.get(decl.base)

    def member(self, decl, name):
        # The game also has a global lookup for members of a GLOBAL_GROUP
        # element; every member on the retail disc resolves locally, so that
        # path is not modelled until a file needs it.
        for child in self.members(decl):
            if child.name == name:
                return child
        return None
