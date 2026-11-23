"""Inspect Brute Force resource bundles from a locally extracted, owned disc.

Commands:
  verify  Decode every BXSD schema and nonempty BXML descriptor (the
          ASSETFORMATS M0 gate) and report any that do not parse exactly.
  dump    Write one BXML member as a JSON tree.
  names   Build local name-hash candidates from the executable and bundles.

Everything written by this tool is derived from game data and must stay in
an untracked directory (the default is build\\assets).
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bfassets import FormatError  # noqa: E402
from bfassets.bundle import iter_members, read_member  # noqa: E402
from bfassets.bxml import EnumValue, StringId, parse_document  # noqa: E402
from bfassets.bxsd import SchemaSet, parse_schema  # noqa: E402
from bfassets.names import crc_update, label, name_hash  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = ROOT / "game" / "disc" / "data"
DEFAULT_XBE = ROOT / "game" / "disc" / "default.xbe"
DEFAULT_OUT = ROOT / "build" / "assets"
SCHEMA_BUNDLE = "common.tgz"


def load_schemas(data_dir):
    schemas = []
    for name, data in iter_members(data_dir / SCHEMA_BUNDLE, suffixes=(".xsb",)):
        schemas.append((name, parse_schema(data)))
    if not schemas:
        raise FormatError(f"no .xsb schemas in {data_dir / SCHEMA_BUNDLE}")
    return schemas


def bundles(data_dir):
    paths = sorted(data_dir.glob("*.tgz"))
    if not paths:
        raise FileNotFoundError(f"no .tgz bundles in {data_dir}")
    return paths


def cmd_verify(args):
    started = time.perf_counter()
    schemas = load_schemas(args.data)
    schema_set = SchemaSet(s for _, s in schemas)
    print(f"schemas: {len(schemas)} parsed to their end")
    ok = blocks = multi = 0
    failures = []
    families = Counter()
    for bundle in bundles(args.data):
        for name, data in iter_members(bundle, suffixes=(".xmb",)):
            try:
                doc = parse_document(data, schema_set)
            except FormatError as error:
                failures.append((name, str(error)))
                continue
            ok += 1
            blocks += doc.blocks
            multi += doc.blocks > 1
            families[re.sub(r"[-_].*$|\.xmb$", "", Path(name).name)] += 1
    print(f"descriptors: {ok} parsed exactly, {len(failures)} failed")
    print(f"token blocks: {blocks} ({multi} descriptors use more than one)")
    for name, error in failures[:20]:
        print(f"  FAIL {name}: {error}")
    print(f"elapsed: {time.perf_counter() - started:.1f} s")
    return 1 if failures else 0


def load_names(path):
    if not path:
        return {}
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    out = {}
    for key, value in raw.items():
        candidates = value if isinstance(value, list) else [value]
        if candidates:
            out[int(key.lstrip("#"), 16)] = "|".join(candidates)
    return out


def to_json(element, names):
    def value(v):
        if isinstance(v, StringId):
            return {"stringid": label(v.hash, names)}
        if isinstance(v, EnumValue):
            return {"enum": v.index}
        if isinstance(v, bytes):
            return {"bytes": v.hex()}
        return v

    out = {"name": label(element.name, names)}
    if element.marker is not None:
        out["marker"] = element.marker
    if element.attributes:
        out["attributes"] = {label(k, names): value(v) for k, v in element.attributes}
    if element.value is not None:
        out["value"] = value(element.value)
    if element.children:
        out["children"] = [to_json(c, names) for c in element.children]
    return out


def cmd_dump(args):
    schema_set = SchemaSet(s for _, s in load_schemas(args.data))
    data = read_member(args.data / args.bundle, args.member)
    doc = parse_document(data, schema_set)
    names = load_names(args.names)
    tree = {
        "member": args.member,
        "version": f"{doc.version >> 8}.{doc.version & 0xFF}",
        "namespaces": [[label(p, names), label(u, names)] for p, u in doc.namespaces],
        "root": to_json(doc.root, names),
    }
    out = args.output or DEFAULT_OUT / "dump" / (args.member.replace("/", "_") + ".json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(tree, indent=1), encoding="utf-8")
    print(out)
    return 0


def _all_hashes(data_dir):
    targets = set()
    schemas = load_schemas(data_dir)

    def walk(decls):
        for d in decls:
            for q in (d.name, d.base[1] if d.base else None):
                if q is not None:
                    targets.add(q)
            walk(d.children)

    for _, schema in schemas:
        walk(schema.decls)
    for bundle in bundles(data_dir):
        for _, data in iter_members(bundle, suffixes=(".xmb",)):
            schema_free = _name_table(data)
            targets.update(schema_free)
    return targets


def _name_table(data):
    from bfassets.stream import Reader
    r = Reader(data)
    head = r.read(16)
    r.seek(int.from_bytes(head[8:12], "little"))
    count = r.leb128()
    return [r.u32() for _ in range(count)]


def cmd_names(args):
    targets = _all_hashes(args.data)
    words = set()
    ident = re.compile(rb"[A-Za-z_][A-Za-z0-9_\-:.]{1,40}")
    words.update(w.decode() for w in ident.findall(args.xbe.read_bytes()))
    for bundle in bundles(args.data):
        for name, data in iter_members(bundle):
            words.update(re.split(r"[/.]", name))
            if name.endswith(".gscr"):
                words.update(w.decode() for w in ident.findall(data))
    parts = set()
    for w in words:
        parts.update(p.lower() for p in re.split(r"[-_:.]", w) if p)
        parts.update(p.lower() for p in re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+", w))
    found = {}
    for w in words | parts:
        h = name_hash(w)
        if h in targets:
            found.setdefault(h, set()).add(w.lower())
    vocab = sorted({p.encode() for p in parts if 2 <= len(p) <= 14 and p.isalpha()})
    for a in vocab:
        state = crc_update(0xFFFFFFFF, a + b"-")
        for b in vocab:
            h = crc_update(state, b) ^ 0xFFFFFFFF
            if h in targets:
                found.setdefault(h, set()).add((a + b"-" + b).decode())
    out = args.output or DEFAULT_OUT / "name-candidates.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({f"{h:08x}": sorted(v) for h, v in sorted(found.items())},
                              indent=1), encoding="utf-8")
    print(f"{len(found)} of {len(targets)} hashes have uncurated candidates: {out}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA,
                        help="directory holding the disc's data\\*.tgz bundles")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("verify", help="decode every schema and descriptor")
    dump = sub.add_parser("dump", help="write one descriptor as JSON")
    dump.add_argument("bundle", help="bundle file name, for example tutorial.tgz")
    dump.add_argument("member", help="member path, for example tutorial/textures-tutorial.xmb")
    dump.add_argument("--names", type=Path, help="local JSON map of hash to name(s)")
    dump.add_argument("-o", "--output", type=Path)
    names = sub.add_parser("names", help="build local name-hash candidates")
    names.add_argument("--xbe", type=Path, default=DEFAULT_XBE)
    names.add_argument("-o", "--output", type=Path)
    args = parser.parse_args(argv)
    try:
        return {"verify": cmd_verify, "dump": cmd_dump, "names": cmd_names}[args.command](args)
    except (FormatError, FileNotFoundError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
