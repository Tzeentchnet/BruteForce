"""Synthetic coverage for tools/bfassets. No game data is used or needed."""

import io
from pathlib import Path
import struct
import sys
import tarfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import assets  # noqa: E402
from bfassets import FormatError  # noqa: E402
from bfassets.bundle import iter_members  # noqa: E402
from bfassets.bxml import EnumValue, StringId, parse_document  # noqa: E402
from bfassets.bxsd import DAXNS, GLOBAL_GROUP, XS_NS, SchemaSet, parse_schema  # noqa: E402
from bfassets.names import name_hash  # noqa: E402

H = name_hash
TNS = H("synthetic")


@pytest.mark.parametrize("name, expected", [
    ("acg", 0xE9B8A09A),
    ("daxns", 0x1F017D45),
    ("http://www.w3.org/2001/XMLSchema", 0x05DC1F4B),
    ("bfns", 0xFA87713F),
    ("xsi", 0x0B556933),
    ("http://www.w3.org/2001/XMLSchema-instance", 0xFB9E7F5F),
    ("ACG", 0xE9B8A09A),
])
def test_name_hash_vectors(name, expected):
    assert name_hash(name) == expected


# --- synthetic encoders --------------------------------------------------------

def leb(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def qname(local, ns=None):
    if local is None:
        return b"\x00"
    if ns is None:
        return b"\x21" + struct.pack("<I", local)
    return b"\x22" + struct.pack("<II", ns, local)


def decl(flags, name, base=None, children=()):
    base_bytes = qname(None) if base is None else qname(base[1], base[0])
    return struct.pack("<H", flags) + qname(name) + base_bytes + leb(len(children)) + b"".join(children)


def schema_bytes(decls, tns=TNS):
    xs, d = H("xs"), H("d")
    body = leb(1) + qname(tns) + leb(3) + struct.pack("<6I", 0, tns, xs, XS_NS, d, DAXNS)
    head = b"BXSD" + bytes([1, 0, 1, 0]) + struct.pack("<II", 16 + len(body), 0)
    return head + body + leb(len(decls)) + b"".join(decls)


def synthetic_schema():
    xs = lambda n: (H("xs"), H(n))  # noqa: E731
    d = lambda n: (H("d"), H(n))  # noqa: E731
    local = lambda n: (None, H(n))  # noqa: E731
    return schema_bytes([
        decl(0x6, H("vec"), xs("float")),
        decl(0x2, H("id-type"), d("stringid")),
        decl(0x2, H("kind-type"), d("enum-value")),
        decl(0x9, H("root"), None, [
            decl(0xA, H("title"), xs("string")),
            decl(0xA, H("count"), xs("unsignedInt")),
            decl(0xA, H("ref"), local("id-type")),
            decl(0xA, H("kind"), local("kind-type")),
            decl(0xA, H("position"), local("vec")),
            decl(0x9, H("item"), None, [decl(0xA, H("flag"), xs("boolean"))]),
            decl(0x80, GLOBAL_GROUP, None, [decl(0x2, H("width"), xs("short")),
                                            decl(0x2, H("depth"), xs("int"))]),
        ]),
    ])


NAMES = ["root", "title", "count", "ref", "kind", "position", "item", "flag", None,
         "width", "depth"]
NAME_HASHES = [GLOBAL_GROUP if n is None else H(n) for n in NAMES]
IDX = {n: i for i, n in enumerate(NAMES)}
POSITION_COUNT = 0x800 + 5  # needs the extended count encoding


def synthetic_items():
    """(token words, value bytes after each word) in decode order."""
    items = []

    def tok(word, value=b""):
        items.append((word, value))

    tok(0x0000 | IDX["root"])
    title = b"crate"
    tok(0xA000 | IDX["title"], leb(len(title)) + title)
    tok(0xA000 | IDX["count"], struct.pack("<I", 42))
    tok(0xA000 | IDX["ref"], struct.pack("<I", H("crate-mesh")))
    tok(0xA000 | IDX["kind"], leb(3 + 1))
    tok(0x9000 | 4)
    for i in range(4):
        tok(0x0000 | IDX["item"])
        tok(0xA000 | IDX["flag"], bytes([i & 1]))
        tok(0xC000 | IDX["item"])
    tok(0x0000 | IDX["position"])
    floats = struct.pack(f"<{POSITION_COUNT}f", *[i * 0.5 for i in range(POSITION_COUNT)])
    low = POSITION_COUNT & 0x7FF
    tok(0xD000 | 0x800 | low)
    tok(POSITION_COUNT >> 11, floats)  # final continuation word: high bit clear
    tok(0xC000 | IDX["position"])
    tok(0x0000 | IDX[None])
    tok(0xA000 | IDX["width"], struct.pack("<h", -7))
    tok(0x0000 | IDX["depth"])
    tok(0xD000 | 1, struct.pack("<i", 99))
    tok(0xC000 | IDX["depth"])
    tok(0xC000 | IDX[None])
    tok(0xC000 | IDX["root"])
    return items


def compress_block(words):
    """Greedy LZSS for one block, using back-references in the 512-word ring."""
    out, ring, pos, i = [], {}, 1, 0
    groups = []
    while i < len(words):
        best_len, best_off = 0, 0
        for off in range(1, pos):
            n = 0
            while (i + n < len(words) and n < 129 and off + n < pos
                   and ring[off + n] == words[i + n]):
                n += 1
            if n > best_len:
                best_len, best_off = n, off
        if best_len >= 2 and pos + best_len < 512:
            groups.append((1, best_off | ((best_len - 2) << 9)))
            for k in range(best_len):
                ring[pos] = words[i + k]
                pos += 1
            i += best_len
        else:
            groups.append((0, words[i]))
            if pos < 512:
                ring[pos] = words[i]
            pos += 1
            i += 1
    for g in range(0, len(groups), 16):
        chunk = groups[g:g + 16]
        out.append(sum(bit << k for k, (bit, _) in enumerate(chunk)))
        out.extend(word for _, word in chunk)
    return out


def bxml_bytes(items, split=None, trailing=b""):
    """Encode items as BXML; ``split`` starts a new block before that item."""
    ns = struct.pack("<6I", H("acg"), H("acg"), 0, TNS, H("xsi"), H("xsi-uri"))
    names = leb(len(NAME_HASHES)) + struct.pack(f"<{len(NAME_HASHES)}I", *NAME_HASHES)
    table_offset = 16 + 1 + len(ns)
    head = b"BXML" + bytes([1, 1, 7, 0]) + struct.pack("<II", table_offset, 0)
    out = bytearray(head + leb(3) + ns + names)
    cuts = [0] + ([split] if split else []) + [len(items)]
    for a, b in zip(cuts, cuts[1:]):
        chunk = items[a:b]
        words = [w for w, _ in chunk]
        payload = compress_block(words)
        total = 3 + len(payload)
        out += struct.pack("<HI", total, len(words)) + struct.pack(f"<{len(payload)}H", *payload)
        for _, value in chunk:
            out += value
    return bytes(out + trailing)


@pytest.fixture
def schema_set():
    return SchemaSet([parse_schema(synthetic_schema())])


# --- schema -------------------------------------------------------------------

def test_schema_parses_to_end():
    schema = parse_schema(synthetic_schema())
    assert schema.target_ns == TNS
    root = next(d for d in schema.decls if d.name == H("root"))
    assert [c.name for c in root.children][:2] == [H("title"), H("count")]


@pytest.mark.parametrize("mutate, message", [
    (lambda b: b[:-1], "passes the end"),
    (lambda b: b + b"\x00", "unexplained"),
    (lambda b: b"BXSX" + b[4:], "magic"),
    (lambda b: b[:8] + struct.pack("<I", 17) + b[12:], "header says"),
])
def test_schema_rejects_malformed(mutate, message):
    with pytest.raises(FormatError, match=message):
        parse_schema(mutate(synthetic_schema()))


def test_schema_rejects_unknown_qname_tag():
    bad = schema_bytes([b"\x02\x00\x23" + b"\x00" * 8])
    with pytest.raises(FormatError, match="QName tag"):
        parse_schema(bad)


def test_value_types_follow_base_chain(schema_set):
    root = schema_set.root(TNS, H("root"))
    assert schema_set.value_type(schema_set.member(root, H("ref"))).special == "stringid"
    assert schema_set.value_type(schema_set.member(root, H("kind"))).special == "enum"
    position = schema_set.value_type(schema_set.member(root, H("position")))
    assert position.code == 11 and position.is_list


# --- documents ----------------------------------------------------------------

def check_document(doc):
    root = doc.root
    assert root.name == H("root")
    attrs = dict(root.attributes)
    assert attrs[H("title")] == "crate"
    assert attrs[H("count")] == 42
    assert attrs[H("ref")] == StringId(H("crate-mesh"))
    assert attrs[H("kind")] == EnumValue(3)
    items = [c for c in root.children if c.name == H("item")]
    assert [dict(i.attributes)[H("flag")] for i in items] == [False, True, False, True]
    assert items[0].marker == 4 and items[1].marker is None
    position = next(c for c in root.children if c.name == H("position"))
    assert len(position.value) == POSITION_COUNT and position.value[3] == 1.5
    group = next(c for c in root.children if c.name == GLOBAL_GROUP)
    assert dict(group.attributes)[H("width")] == -7
    assert group.children[0].value == 99


def test_single_block_with_back_references(schema_set):
    words = [w for w, _ in synthetic_items()]
    payload = compress_block(words)
    assert len(payload) < len(words) + 2, "fixture must exercise a back-reference"
    doc = parse_document(bxml_bytes(synthetic_items()), schema_set)
    assert doc.blocks == 1
    check_document(doc)


def test_values_interleave_between_blocks(schema_set):
    items = synthetic_items()
    doc = parse_document(bxml_bytes(items, split=9), schema_set)
    assert doc.blocks == 2
    assert doc.declared_words == doc.emitted_words == [9, len(items) - 9]
    check_document(doc)


def test_document_rejects_trailing_and_truncated(schema_set):
    data = bxml_bytes(synthetic_items())
    with pytest.raises(FormatError, match="bytes left"):
        parse_document(data + b"\x00", schema_set)
    parse_document(data + b"\x00", schema_set, strict=False)
    with pytest.raises(FormatError, match="passes the end"):
        parse_document(data[:-3], schema_set)


def test_document_rejects_bad_name_index_and_undeclared(schema_set):
    items = synthetic_items()
    items[1] = (0xA000 | 0x7FF, items[1][1])
    with pytest.raises(FormatError, match="name index"):
        parse_document(bxml_bytes(items), schema_set)
    items = synthetic_items()
    items[1] = (0xA000 | IDX["width"], items[1][1])
    with pytest.raises(FormatError, match="no declaration"):
        parse_document(bxml_bytes(items), schema_set)


def test_document_rejects_bad_block_length(schema_set):
    data = bytearray(bxml_bytes(synthetic_items()))
    start = 16 + 1 + 24 + 1 + 4 * len(NAME_HASHES)
    data[start:start + 2] = struct.pack("<H", 1)
    with pytest.raises(FormatError, match="token block"):
        parse_document(bytes(data), schema_set)


# --- bundles and the command line ---------------------------------------------

def make_tgz(path, members):
    with tarfile.open(path, "w:gz") as archive:
        for name, data in members:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


def test_bundle_skips_empty_and_rejects_traversal(tmp_path):
    good = tmp_path / "good.tgz"
    make_tgz(good, [("good/a.xmb", b"1"), ("good/empty.xmb", b"")])
    assert [n for n, _ in iter_members(good)] == ["good/a.xmb"]
    assert len(list(iter_members(good, include_empty=True))) == 2
    bad = tmp_path / "bad.tgz"
    make_tgz(bad, [("../evil.xmb", b"1")])
    with pytest.raises(FormatError, match="unsafe"):
        list(iter_members(bad))


def test_cli_verify_and_dump(tmp_path, capsys):
    data = tmp_path / "data"
    data.mkdir()
    make_tgz(data / "common.tgz", [("common/schemas/synthetic.xsb", synthetic_schema())])
    make_tgz(data / "level.tgz", [("level/objects-level.xmb", bxml_bytes(synthetic_items(), 9)),
                                  ("level/empty.xmb", b"")])
    assert assets.main(["--data", str(data), "verify"]) == 0
    assert "1 parsed exactly, 0 failed" in capsys.readouterr().out
    out = tmp_path / "dump.json"
    assert assets.main(["--data", str(data), "dump", "level.tgz", "level/objects-level.xmb",
                        "-o", str(out)]) == 0
    assert '"stringid"' in out.read_text()
    make_tgz(data / "broken.tgz", [("broken/x.xmb", b"BXML" + b"\x00" * 12)])
    assert assets.main(["--data", str(data), "verify"]) == 1
