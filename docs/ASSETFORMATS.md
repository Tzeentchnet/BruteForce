# Brute Force asset formats

This is the working format reference for a future Blender importer for
Brute Force characters, props, and levels. It records what has been decoded
from the game's own resource files, how it was established, and what an
importer still has to prove. It is not a complete specification: most
payload layouts are still open, and every section says how much of it is
known.

Disc organization, archive counts, and ownership of the other titles on the
disc are in [ARCHITECTURE.md](./ARCHITECTURE.md). Runtime and renderer
behavior are in [FINDINGS.md](./FINDINGS.md) and
[RENDERING.md](./RENDERING.md).

## Evidence levels and provenance

Each statement below uses one of these labels:

| Label | Meaning |
| --- | --- |
| **Verified** | Checked by a reproducible test against every relevant member of the local disc, or against the executable's own code or tables. |
| **Observed** | Seen in the samples named, but not checked against every member or every case. |
| **Hypothesis** | A plausible reading consistent with the evidence that has not been proved. Do not build importer behavior on it without a test. |
| **Unknown** | Not yet investigated, or investigated without a result. |

All observations come from the single North American retail disc identified
by [retail.json](../fingerprints/retail.json), inspected on 2026-11-22 and 2026-11-23. The
executable hash does not authenticate the other disc files; other releases
may differ. Code references are to the recompiled generation
`20261121-005939-bc768cc6`, whose function names are the original guest
addresses. The disc, extracted members, generated code, and any decoded
payloads stay local, in line with [AGENTS.md](../AGENTS.md). This document
records signatures, structure, and schema vocabulary, not game content.

## Importer targets

The importer should eventually bring three kinds of content into Blender:

| Target | Descriptor files (BXML) | Payload files | Status |
| --- | --- | --- | --- |
| Props and weapons | `objects-*`, `materials-*`, `textures-*`, `objecttypes-*` | `objects-*.ipn`, `objects-*.ivd`, `textures-*.tex` | Descriptors decode fully; payloads are unknown. |
| Characters | The props set plus `animations-*` and `characters/ms_*` | Also `animations-*.chnl` | Skeleton, skinning, and animation encoding are unknown. |
| Levels | `levels-*`, plus each referenced library | All of the above | The level vocabulary is recovered; terrain and placement encodings are unknown. |

The steps that every target depends on are, in order: reading bundles,
decoding BXML into a named element tree, and resolving the libraries the
tree references. The [importer plan](#importer-architecture-and-milestones)
lays out the stages.

## Layer 1: bundles

**Verified.** The 56 `.tgz` files directly under `data` are gzip-compressed
POSIX tar archives. Every regular member name starts with the bundle's own
directory, for example `<bundle>/objects-<bundle>.xmb`. The 305 language
bundles under `data\ml-sounds` hold only sound banks and are not importer
input.

Across the 56 direct bundles, the members are:

| Extension | Members | Empty | First four bytes | Role (from names and references) |
| --- | ---: | ---: | --- | --- |
| `.xmb` | 767 | 8 | `BXML` in all 759 nonempty | Binary XML descriptors; see [Layer 3](#layer-3-bxml-descriptors-xmb). |
| `.gscr` | 179 | 0 | ASCII `dura` in all | Text scripts. **Unknown** grammar. |
| `.ipn` | 61 | 1 | Varies, often float-like | Referenced by `objects-*.xmb`. **Unknown** layout. |
| `.ivd` | 61 | 1 | `00 00 01 00` in all 60 nonempty | Referenced by `objects-*.xmb`. **Unknown** layout. |
| `.tex` | 61 | 0 | Varies; no header signature | Referenced by `textures-*.xmb`. **Unknown** layout. |
| `.mem` | 61 | 0 | No common signature | Paired with `sounds-*.xmb`. Not importer input. |
| `.chnl` | 60 | 7 | Mostly `00 00 00 00` | Referenced by `animations-*.xmb`. **Unknown** layout. |
| `.xsb` | 7 | 0 | `BXSD` in all | Binary schemas, all in `common/schemas`; see [Layer 4](#layer-4-bxsd-schemas-xsb). |
| `.xwb` | 6 | 0 | `WBND` | Wave banks. Not importer input. |
| `.bin` | 2 | 0 | Zeros | Not investigated. |
| `.xbx` | 1 | 0 | `XPR0` | `common/camp-save-image.xbx`, a save image. |
| `.ale` | 1 | 0 | ASCII `UTF ` | `common/effects-common.ale`; see [effects](#effects-and-scripts). |

**Verified.** A typical campaign, tutorial, or multiplayer-map bundle
contains one set of
`animations`, `levels`, `materials`, `objects`, `objecttypes`, `sounds`, and
`textures` descriptors, with the matching `.chnl`, `.ipn`, `.ivd`, `.mem`,
and `.tex` payloads, plus `world-materials.xmb`, `characters/*.xmb`, and
`scripts/*.gscr`. `common.tgz` instead carries the shared libraries,
schemas, string tables, and options. `mp_common.tgz` is the only bundle
with several payload sets: six `objects-*`, `textures-*`, and
`animations-*` sets named `dm` and `sdm_*`.
**Hypothesis:** those sets hold multiplayer character models, which would make
`mp_common.tgz` a convenient first character target.

**Observed.** Empty members exist (for example `mp1/animations-mp1.xmb`,
`mp1/animations-mp1.chnl`, and the `splash_screen` objects set). An importer
must treat a zero-length member as "no data", not as a corrupt file.

**Importer rules.** Read members in place with `tarfile`, or validate them
before extracting: reject absolute paths, `..` components, links, device
nodes, and sizes that exceed the archive. Never trust a size, count, or
offset read from a member without a bounds check.

## Layer 2: name hashing

**Verified.** BXML and BXSD files never store element, attribute, type, or
namespace names as text. They store a 32-bit hash of each name. The hash is
a case-insensitive CRC-32 with a nonstandard table:

1. Lowercase each byte. The executable's 256-byte translation table at
   guest VA `0x3BC550` maps `A`-`Z` to `a`-`z` and every other byte to
   itself.
2. Run a reflected CRC-32 with initial value `0xFFFFFFFF` and a final
   complement, using the table at guest VA `0x3BC150`.
3. That table is **not** the standard CRC-32 table. It is produced by the
   usual reflected generator with polynomial `0xEDB88320`, except that the
   right shift is arithmetic (sign-extending), as if the generator used a
   signed 32-bit integer. Entry 128 matches the standard table; entry 1 is
   `0x09073096` instead of `0x77073096`.

This reference implementation reproduces all 256 entries of the
executable's table:

```python
def make_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            shifted = (c >> 1) | (c & 0x80000000)  # arithmetic shift
            c = shifted ^ 0xEDB88320 if c & 1 else shifted
        table.append(c)
    return table

TABLE = make_table()

def name_hash(name: str) -> int:
    c = 0xFFFFFFFF
    for b in name.encode("ascii").lower():
        c = TABLE[(b ^ c) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF
```

Test vectors. The executable stores the first three as precomputed
(string pointer, hash) pairs in a built-in table at guest VA `0x406DF4`.
The last three are names whose hashes appear in every BXML namespace
table:

| Name | Hash |
| --- | --- |
| `acg` | `0xE9B8A09A` |
| `daxns` | `0x1F017D45` |
| `http://www.w3.org/2001/XMLSchema` | `0x05DC1F4B` |
| `bfns` | `0xFA87713F` |
| `xsi` | `0x0B556933` |
| `http://www.w3.org/2001/XMLSchema-instance` | `0xFB9E7F5F` |

**Evidence.** The loop is `sub_00238370` (`loc_00238380`) in the generated
code: it reads each byte through the translation table at `0x3BC550`, then
through the CRC table at `0x3BC150`, and complements the result. A
standard CRC-32 table also exists in the executable at VA `0x3B7EE0`; the
users examined hash fixed-size binary keys or force the top bit of the
result, and none produces the name hashes. The
built-in table of XSD simple types next to the test-vector pairs lists
`boolean`, `byte`, `unsignedByte`, `short`, `unsignedShort`, `int`,
`unsignedInt`, `float`, `double`, `string`, `stringid`, `wstring`,
`user-typedef`, and `enum-value`.

### Recovering names

A hash cannot be inverted, so names have to be recovered by hashing
candidates. On 2026-11-22, the BXML name tables across all bundles held
2,019 distinct hashes:

- Hashing every identifier-like string in `default.xbe`, every bundle
  member name, and every word in the `.gscr` scripts resolved 117.
- Hashing every hyphen- and underscore-joined pair from a vocabulary of
  9,219 words taken from those sources raised the total to 1,012 hashes with
  at least one candidate.

The pair search tries about 170 million strings against 2,019 targets, so
about 80 chance collisions are expected. Many results are plainly real
schema words (`mesh-library`, `vertex-buffer`, `index-buffer`,
`lod-switch`), while some are junk (`2kxluv3_as`). **A name map must be
curated:** keep a candidate only if it reads as a schema word, fits its
siblings, or is confirmed by a BXSD declaration. An importer should keep the
raw hash and print an unresolved name as `#xxxxxxxx`, and must not depend on
a name that is still unresolved. Collisions also mean a single hash can
yield several plausible candidates; record every candidate, not only the
first.

Names below are written without the `#` only where a curated, plausible
candidate exists. The hash dictionary itself is derived from the user's
executable and disc, so it stays local and should be rebuilt by a script
rather than committed.

## Layer 3: BXML descriptors (`.xmb`)

**Verified.** On 2026-11-23 the format was traced through the game's own
reader and checked by decoding all 759 nonempty BXML members against the
seven BXSD schemas. Every member decodes with no byte left over, no token
word left unread, and every token block emitting exactly the word count its
header declares (`python tools\assets.py verify`). The reader routines are:

| Guest VA | Role |
| --- | --- |
| `0x254860` | Header, namespace table, name table, root element. |
| `0x255330` | Element loop (recursive); dispatches on the token kind. |
| `0x254C60` | Reads one value. |
| `0x281CC0`, `0x281BF0`, `0x281D30` | Token-stream setup, block loader, and LZSS decoder. |
| `0x252CA0` | Unsigned LEB128. |
| `0x232A40` | A namespace name: a raw u32 hash, or a hashed string. |

### Header and tables

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | Magic `BXML`, checked byte by byte. |
| `0x04` | 1, 1 | Version: the reader accepts `b5 * 0.1 + b4 <= 1.1`. Every file has `01 01` (1.1). |
| `0x06` | u8 | Flags, `07` in every file. Bit 0: namespace names are raw u32 hashes rather than strings. Bits 0-1: enable the `stringid` and `enum-value` encodings below. |
| `0x07` | u8 | Zero. **Unknown.** |
| `0x08` | u32 | Offset of the name table; the reader seeks there after the namespaces. |
| `0x0C` | u32 | Zero; not read. |
| `0x10` | LEB128 | Namespace count `N`. |
| `0x11` | `N` x 8 | Namespace pairs: u32 prefix hash, then u32 URI hash. Prefix zero is the default namespace. |
| name table | LEB128, then u32s | Name count, then that many name hashes. Tokens index this table. |

The name table can list names a document does not use; all 60 nonempty
`objects-*.xmb` and 61 `materials-*.xmb` members carry one identical
110-name table.

Every file declares `xsi` = `http://www.w3.org/2001/XMLSchema-instance`.
The asset families (`objects`, `materials`, `textures`, `animations`) use
`acg=acg`, default=`daxns`, `xsi`; the game families (`levels`,
`objecttypes`, `characters/ms_*`) use default=`bfns`, `acg=acg`,
`daxns=daxns`, `xsi`. A format string in the executable writes
`<texture-library ... xmlns="daxns" ...>`, so `daxns` is the asset namespace
and `bfns` the game namespace; their schemas are `daxns.xsb` and `bfns.xsb`.

### Compressed token stream

The structure is a stream of 16-bit token words, compressed with a
word-oriented LZSS and stored in blocks. The first block starts directly
after the name table.

- **Block:** `u16 total` (words, including this one), then the decompressed
  word count, a u32 (low half first) for version 1.1 or a u16 otherwise,
  then the payload. The reader's buffer holds at most `0x1200` words.
- **Payload:** a flag word, then up to 16 items, repeated. Bit `i` of the
  flag word, from bit 0, describes item `i`: clear means the item is a
  literal token word; set means it is a back-reference `w`, which copies
  `(w >> 9) + 2` words starting at position `w & 0x1FF` of a 512-word ring.
  Every output word is written to the ring at the current position, which
  starts at 1 in each block. A back-reference never spans blocks.
- **Interleaving:** values are read from the same file cursor as the
  blocks, and the next block is loaded only when the decoder needs another
  word. A file is therefore `block 1, values of the tokens in block 1,
  block 2, ...`. 36 of the 759 files use more than one block (806 blocks in
  all); those are the 36 files whose first word was at least `0x1000`.

The earlier header reading `A`, `B`, and a flag word is this block header:
`A` is the block length, `B` and the next u16 are the decompressed count,
and the "flags" are the first LZSS flag word. The earlier puzzles, such as
all 16 token kinds and runs of `0xFExx` words, were compressed data.

### Tokens

Each token word has a kind in its top four bits and an operand in the low
12 bits:

| Kind | Meaning | Uses on the disc |
| --- | --- | ---: |
| `0x0` | Start a child element; operand = name index. | 4,174,242, plus 759 roots |
| `0x9` | Marker: operand is an extended count (below), held for the next start element. | 510,545 |
| `0xA` | Attribute; operand = name index. One value follows in the value stream. | 9,622,122 |
| `0xB` | Empty child element; operand = name index. | 0 |
| `0xC` | End the current element. | 4,175,001 |
| `0xD` | Element value; operand = extended element count. | 282,218 |
| others | Ignored by the game's loop. | 0 |

An **extended count** is the low 12 bits of the token. If bit 11 (`0x800`)
is set, further token words follow, each contributing `w & 0x7FFF` at bit
11, then bit 26, and so on, while bit 15 of the word is set. The reader ORs
rather than replaces, so bit 11 of an extended count is always set.

The document is one root start token, its contents, and its end token.
**Observed:** in four bundles, 32,327 of 33,594 markers equal the number of
consecutive same-named siblings that start at the marked element, and every
run of more than one same-named sibling is marked; some lone elements carry
a marker of 2. The game passes the marker to an optional handler along with
the next element. **Hypothesis:** an array-size hint. It does not affect
decoding.

### Values and types

A value's encoding comes from the schema type of its element or attribute
(see [Layer 4](#layer-4-bxsd-schemas-xsb)). Each type resolves through its
base chain to a built-in type, with a code and an element size from the
table at `0x4063D0`:

| Code | Built-in types (registration table at `0x406CB8`) | Bytes per element |
| ---: | --- | ---: |
| 1 | `xs:boolean` | 1 |
| 3, 4 | `xs:byte`, `xs:unsignedByte` | 1 |
| 5, 6 | `xs:short`, `xs:unsignedShort` | 2 |
| 7 | `xs:int`, `daxns:user-typedef` | 4 |
| 8 | `xs:unsignedInt` | 4 |
| 9, 10 | Not registered by name | 8 |
| 11 | `xs:float` | 4 |
| 12 | `xs:double` | 8 |
| 13 | `xs:string`, `daxns:stringid`, `daxns:enum-value` | special |
| 14 | `daxns:wstring` | special |

- **Numeric codes:** an attribute reads one element; an element value reads
  the token's count of elements, little-endian.
- **Codes 13 and 14:** with header flag bits 0-1 set, the reader walks the
  type's base chain. A type derived from `daxns:stringid` is a u32 name
  hash. One derived from `daxns:enum-value`, and not from `stringid`, is a
  LEB128 holding the enumeration index plus one. Any other string is a
  LEB128 byte length followed by the bytes, UTF-16LE for `wstring`.

Across the disc, the value reads are 3,615,138 `stringid`, 2,791,030 float,
1,556,232 int, 1,158,674 boolean, 579,816 enum, 74,445 short, 54,745
unsigned short, 33,141 unsigned byte, 32,242 string, 7,569 wstring, and
1,308 unsigned int values.

### Resolving names to declarations

The root element resolves to the top-level declaration with its name in the
document's default namespace. A child element or attribute resolves to the
member with that name: first the parent declaration's own children, then
those of the named type its base refers to, and so on along the base chain.
Every lookup on the retail disc resolves this way. The game also
special-cases the element name `#1b8535f0` with a global lookup; no file on
this disc needs that path, and it is not modelled.

## Layer 4: BXSD schemas (`.xsb`)

**Verified.** All seven schemas in `common/schemas` (`bfens`, `bfns`,
`daxns`, `gameflags`, `gameflags-downloadable`, `global-gameflags`, and
`limp`) parse to their last byte with this grammar, read by the loader at
`0x281FE0` and the declaration reader at `0x281F10`/`0x254620`:

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | Magic `BXSD`. |
| `0x04` | 1, 1 | Version, `01 00` (1.0); the reader accepts at most 1.0. |
| `0x06` | u8 | `1` in every file: names are raw u32 hashes. With `0`, the loader reads a string table first; not observed. |
| `0x08` | u32 | Offset of the declaration tree. |
| `0x0C` | u32 | Zero. |
| `0x10` | LEB128 | `1` in every file. **Unknown.** |
| next | QName | Target namespace (a local name). |
| next | LEB128, pairs | Namespace count, then u32 prefix and u32 URI hashes, as in BXML. |
| tree | | A declaration list. |

A **declaration list** is a LEB128 count followed by that many
declarations. A declaration is `u16 flags`, QName `name`, QName `base`, and
its own declaration list. A **QName** is a tag byte: `00` for none; `21`
followed by a u32 local hash in the schema's target namespace; or `22`
followed by a u32 prefix hash and a u32 local hash.

The flag values found across the seven schemas, and how they read in the
trees:

| Flags | Count | Reading |
| ---: | ---: | --- |
| `0x0001` | 143 | Named complex type (top level). |
| `0x0002` | 331 | Simple type, or a simple member of a `0x80` member. |
| `0x0006` | 9 | List type, for example `float-array : xs:float`. |
| `0x0009` | 409 | Member element with inline content. |
| `0x000A` | 1,723 | Member with a simple type. |
| `0x0018`, `0x0019` | 1, 624 | Member whose content is a named type. |
| `0x0080` | 48 | A member named `#1b8535f0` whose members are simple values. |
| `0x0101` | 97 | **Unknown** (only in `bfns.xsb`). |

**Verified** from the loader: a declaration with flag bit 1 (`0x2`) takes
its value code from the root of its base chain (`0x254530`). The other
readings fit every decoded document, but are named by analogy with XML
Schema rather than from the game's code. The built-in types above are
registered before any schema, so the schemas' own declarations of
`daxns:stringid`, `wstring`, `user-typedef`, and `enum-value` do not change
their codes.

## Layer 5: asset families

The element names in this section are curated recovered names from the
`tutorial` and `common` bundles. Since 2026-11-23, every descriptor decodes
to a typed tree (`python tools\assets.py dump`), so nesting and value types
can be read directly; the meaning of each field still has to be established
against its payload. Unresolved names are written as hashes.

### Textures (`textures-*.xmb`, `textures-*.tex`)

- **Observed** names: `texture-library`, `texture`, `image`, `reference`,
  `lod-number`, `name`, `format`, `width`, `height`, `animated-texture`,
  `frames`, `frame`, `frame-number`, `u0`, `v0`, `u1`, `v1`,
  `frame-textures`, `frame-texture`, `optimized-texture`, `offset`, `size`,
  `data-file`, `data-size`.
- **Verified:** all 51 `.tex` names found in descriptors are in
  `textures-<set>.xmb` and name that set's own `textures-<set>.tex`.
- **Observed:** `.tex` has no header signature. 21 of the 61 files begin
  with zero bytes, and others begin with repeated byte patterns typical of
  compressed or swizzled image blocks.
- **Verified** from decoded descriptors: a `texture-library` holds a
  `#1b8535f0` member whose `data-file` names the `.tex` and whose
  `data-size` equals its size (61 of 61), then `optimized-texture`
  elements. Each texture's `#1b8535f0` member has `name` (a `stringid`),
  `format`, `#120336ef`, `width`, `height`, `reference`, `offset`, and
  `size`. All 12,647 entries with a format and width have an `offset` that
  is a multiple of 128 and lies, with its `size`, inside the `.tex` file.
- **Observed:** for every entry with a non-negative `format`, `size` equals
  a full mip chain of `#120336ef` levels at the bit depth of the Xbox
  Direct3D format with that number, except two format-7 entries:

  | `format` | Entries | Matches the size of |
  | ---: | ---: | --- |
  | 0 | 19 | `L8`, 8 bits per pixel |
  | 6, 7 | 4, 26 | `A8R8G8B8`, `X8R8G8B8`, 32 bits per pixel |
  | 11 | 2,486 | `P8`, 8 bits per pixel |
  | 12 | 152 | `DXT1`, 4 bits per pixel, 4x4 blocks |
  | 14, 15 | 8,229, 137 | `DXT2/3`, `DXT4/5`, 8 bits per pixel, 4x4 blocks |
  | 18, 46 | 3, 1 | Linear 32-bit formats |
  | 25 | 1,490 | `A8`, 8 bits per pixel |

  100 entries have negative formats (-12, -14, -1000) and match no size
  rule. **Hypothesis:** `#120336ef` is the mip-level count, the format is
  the Xbox `D3DFORMAT` value, and swizzled formats are stored swizzled.
  `u0`/`v0`/`u1`/`v1` would be sub-rectangles for animated or atlas
  textures.
- **Unknown:** pixel data has not been decoded yet, so swizzling, palettes
  for `P8`, cube maps, the negative formats, and the meaning of
  `optimized-texture` are open.

The renderer already decodes NV2A texture formats and swizzling at runtime;
see [RENDERING.md](./RENDERING.md). An importer can reuse that knowledge, and
can validate a decoded `.tex` image against the guest texture data the
renderer sees, without capturing or committing any images.

### Materials (`materials-*.xmb`, `world-materials.xmb`)

- **Observed** names: `material-library`, `material`, `type`, `textures`,
  `param-id`, `texture-name`, `constant`, `material-constant`, `value`.
- **Hypothesis:** a material is a type (shader or combiner setup) plus
  texture slots named by texture name and a list of numeric constants. The
  Blender mapping would be a Principled BSDF with image textures, and the
  raw `type`, `param-id`, and constants kept as custom properties until
  their meanings are known.
- `world-materials.xmb` is byte-identical in all 54 bundles that carry
  it, and its names (`physical`, `is-soft`, `debris-effect`, `hit-sound`, `slide`,
  `collision`) describe surface physics and effects, not rendering. Import
  it as optional custom properties at most.

### Meshes and props (`objects-*.xmb`, `.ipn`, `.ivd`)

- **Observed** names in the `mesh-library`: `mesh`, `geoset`,
  `material-ref`, `vertex-type`, `vertex-buffer`, `index-buffer`,
  `is-indoor`.
- **Observed** names in the `archetype-library`: `archetype`, `part`,
  `part-name`, `archetype-name`, `archetype-ref`, `parent-part`,
  `child-part`, `parent-point`, `child-point`, `orientation`, `axis`, `min`,
  `max`, `hardpoints`, `hardpoint`, `point`, `mesh-name`, `mesh-ref`,
  `lod-switch`.
- **Verified:** all 41 `.ipn` and 46 `.ivd` names found in descriptors are
  in `objects-<set>.xmb` and name that set's own payloads.
- **Observed** in `objects-tutorial.ivd` (3,391,160 bytes): read as u16,
  the first sixteenth of the file looks like triangle-strip indices with
  degenerate stitching. It starts `0, 1, 2, 2, 3, 3, 3, 4, 5, 5, 5, ...`,
  and 99% of consecutive values differ by less than 64. The rest of the file
  does not look like indices.
- **Observed** in `objects-tutorial.ipn` (2,907,252 bytes): read as
  32-bit floats, one window in every four is about 45% plausible floats,
  against 12-16% in the other windows. This is consistent with mixed float
  and packed vertex attributes.
- **Observed:** the size ratio varies. In the six `mp_common` sets, `.ivd`
  is 11 to 56 times larger than `.ipn`; in `common`, `e01`, and `m01_a`, it
  is 1.4 to 13.5 times larger.
- **Verified** from decoded descriptors: in all 60 nonempty
  `objects-*.xmb` members, the `mesh-library` element's `data-file` names
  the `.ivd` and its `#ed4c66fd` attribute equals the `.ivd` size; a second
  library, `#104db1cd`, names the `.ipn` with `#ed4c66fd` equal to its size.
  So `.ivd` belongs to the meshes and `.ipn` to `#104db1cd`, whose entries
  are a `name` and one integer `#e9e44859` that increases from 0 (in
  `tutorial`: 0, 1184, 1600, ...).
- **Observed** in `objects-tutorial.xmb`: each `geoset` has a float
  `#1ab09530` and a three-float child `#e99750a9` (**Hypothesis:** a
  bounding sphere's radius and centre), and a descendant carrying
  `vertex-type` (a `stringid`) and several integers, one of which
  (`#0cdffa25`) increases through the `.ivd` like an offset.
- **Hypothesis:** a `mesh` is split into `geoset`s, one per material, each
  pointing at a range of a vertex buffer with a given `vertex-type`, and a
  range of an index buffer, drawn as triangle strips. An `archetype` is a
  hierarchy of `part`s, each referring to a mesh, joined at named
  hardpoints. `lod-switch` selects between detail levels.
- **Unknown:** how the `.ivd` splits into vertices and indices, the
  vertex layouts and packed normal formats, the index width beyond the u16
  prefix, primitive types other than strips, what `#104db1cd` and the
  `.ipn` describe, and the coordinate system.

Blender mapping, once proved: one mesh object per `mesh`, with one material
slot per geoset; parts as a parent-child object hierarchy, or as a single
armature for rigid multi-part props; hardpoints as empties named after the
hardpoint; LODs as separate collections, with LOD 0 visible.

### Object types (`objecttypes-*.xmb`)

- **Observed** names include `base-archetype`, `object-type`,
  `archetype-type`, `character-archetype`, `weapon-archetype`,
  `turret-archetype`, `platform-archetype`, `light-archetype`,
  `decal-archetype`, `effect-archetype`, `camera-archetype`, `mesh-name`,
  `part-name`, `hardpoint-name`, `motion-archetype`, `collision-type`, and
  `projectile-mesh`.
- **Hypothesis:** object types bind a gameplay object, such as a character,
  weapon, light, or decal, to the meshes and parts in the objects library,
  and to motions for characters. A level's `object-instance` then names an
  object type. This is the join between level placement and mesh data.

### Characters (`characters/ms_*.xmb`, `animations-*.xmb`, `.chnl`)

- **Observed** motion-set names in `ms_*.xmb`: `motion-sequence`,
  `motion`, `motion-type`, `default-motion`, `next-motion`, `transition`,
  `in-time`, `out-time`, `loop`, `bone`, `root-bone`, `effector-bone`,
  `ik-id`, `local-axis`, `angle`, `damping`, `overlay`, `block-overlay`,
  `hardpoints`, `physics`, `collision`, `damage`, `recoil`, `aim`.
- **Observed** animation-library names: `animation-library`, `channel`,
  `name`, `data-type`, `frame-type`, `offset`, `size`, `num-frames`,
  `compressed`, `data-file`, `archetype-set`, `archetype`, `target`, `type`,
  `data-channel`, `root-height`, `event-channel`, `duration`.
- **Verified:** all 44 `.chnl` names found in descriptors are in
  `animations-<set>.xmb` and name that set's own payload. Seven `.chnl`
  members are empty.
- **Hypothesis:** each animation channel has an offset and size in `.chnl`,
  a frame count, and a data or frame type, and may be compressed. A channel
  targets a part or bone of an archetype. Combined with
  `parent-part`/`child-part` joints from the objects library, this suggests
  rigid, hierarchical character parts rather than vertex-weighted skinning.
- **Unknown:** whether characters use vertex weights at all, the bind pose,
  bone naming, channel encoding and compression, frame rate, and how
  `motion-sequence` timing maps onto channel frames.

`characters/*_chatter.xmb` files hold dialogue selection data and are not
importer input.

Blender mapping, once proved: an armature built from the part hierarchy,
with bones at the joint points; rigid parts parented to bones, or
vertex groups if weights exist; one action per animation; motion-set data
kept as custom properties or NLA markers.

### Levels (`levels-*.xmb`)

- **Observed** names, grouped by apparent purpose:
  - Library binding: `level-file`, `level`, `build-number`, `size`,
    `data-libraries`, `texture-libraries`, `material-libraries`,
    `animation-libraries`, `sound-libraries`, `engine-libraries`,
    `alchemy-libraries`, `library-name`.
  - Terrain: `terrain`, `width`, `length`, `blocks`, `block-index`, `base`,
    `max-height`, `min-height`, `compiled`, `mesh-name`, `mesh-ref`,
    `material-ref`, `is-indoor`.
  - Placement: `group`, `parent`, `is-open`, `object-instance`, `type`,
    `x-offset`, `z-offset`, `ground-offset`, `transform`, `position`,
    `rotation`, `orientation`, `connection`, `parent-hardpoint`,
    `child-hardpoint`, `child-rotation`, `static-effects`.
  - Environment: `sky`, `top`, `bottom`, `texture-name`, `texture-offset`,
    `plane`, `du`, `dv`, `background-color`, `ambient`, `color`,
    `start-distance`, `end-distance`, `far-plane`, `shadow-color`,
    `shadow-range`.
  - Gameplay: many `*-trigger`, `*-point`, `ai-data`, `personality-*`,
    `inventory`, `spawn-trigger`, `save-point`, and `path-object` elements.
- **Hypothesis:** a level binds its bundle's libraries by name and contains
  a block-based terrain made from meshes in the objects library, a tree of
  groups holding object instances placed by transform, or by `x-offset`,
  `z-offset`, and `ground-offset` relative to the terrain, plus sky, fog,
  and ambient settings. The `x`/`z` offsets and `ground-offset` suggest a
  Y-up world with ground in the XZ plane; this is **not** established.
- **Unknown:** terrain block size and layout, transform encoding (matrix,
  quaternion, or Euler), units, and how `connection` attaches instances to
  one another.

Blender mapping, once proved: one collection per level, terrain blocks as
mesh objects or a joined mesh, object instances as linked duplicates of the
imported prop meshes, gameplay points and triggers as named empties with
their attributes as custom properties, and sky and fog as world settings.

### Effects and scripts

- `common/effects-common.ale` begins with ASCII `UTF `. The level and
  object-type vocabularies use `alchemy-libraries` and `alchemy-effects`.
  **Hypothesis:** the file is a particle-effect library for third-party
  effects middleware. It is out of scope for the first importer.
- `.gscr` scripts are text and all begin with `dura`. They drive
  cutscenes and events. Camera paths in them may be worth importing later;
  the grammar is **Unknown**.

### Out of scope

Sound (`.xwb`, `.mem`, `sounds-*.xmb`), string tables, options, game flags,
profiles, Bink movies, and the save image are not importer input. The
`fx\` and `Mech\` trees belong to other titles on the disc; see
[ARCHITECTURE.md](./ARCHITECTURE.md#disc-layout-and-ownership).

## Coordinate system and units

**Unknown.** Establish handedness, up axis, units, triangle winding, and UV
origin from decoded data, and check them against a runtime capture of the
same object, before importing anything into a scene. Blender is right-handed
and Z-up. Apply the conversion once, at the boundary between the
intermediate representation and Blender, never inside the parsers.

## Importer architecture and milestones

Keep the format code independent of Blender. Layers 1-3 exist in
[tools/bfassets](../tools/bfassets), as pure Python with no third-party
dependencies, so they run both in Blender's bundled interpreter and in the
project's test runner:

1. **`bundle`:** safe tar access by member name; see
   [Layer 1](#layer-1-bundles). Done.
2. **`names`:** the name hash, plus a local candidate map rebuilt from the
   user's own executable and disc by `tools\assets.py names`. The hash is
   done; the candidates still need curating.
3. **`bxml` and `bxsd`:** decode into a generic tree of elements,
   attributes, and typed values, keeping unresolved names as hashes. Done.
4. **`model`:** typed asset objects (texture, material, mesh, archetype,
   animation, level) built from the trees, with every cross-reference and
   payload range checked.
5. **Blender add-on:** a thin layer that turns `model` objects into Blender
   data, and holds the coordinate conversion.

A command-line dump, for example a JSON tree, a glTF file, or PNG textures,
written to a local, untracked directory, lets each layer be validated
without Blender.

Milestones, each gated on its own evidence:

| Milestone | Delivers | Gate | Status |
| --- | --- | --- | --- |
| M0 | Name hash, curated names, BXML/BXSD tree dump | Every nonempty `.xmb` and `.xsb` parses to its end with no unexplained bytes; the token grammar matches the game's reader. | Gate met on 2026-11-23; name curation remains. |
| M1 | Textures | At least two formats decode, and match runtime texture data for the same images in two bundles. | Descriptor fields located; pixels not decoded. |
| M2 | Static meshes | One known prop imports with correct shape, normals, UVs, and materials, numerically checked against a CPU-renderer capture, then repeated with a second bundle. | Not started. |
| M3 | Props with parts, hardpoints, and LODs | One multi-part archetype assembles correctly. | Not started. |
| M4 | Levels | Tutorial terrain and object placement match a reviewed in-game view; then a second level. | Not started. |
| M5 | Characters and animation | One character's hierarchy, bind pose, and one animation match the running game. | Not started. |

Synthetic fixtures must cover empty, truncated, oversized, and malformed
members for every parser; [tests/test_assets.py](../tests/test_assets.py)
does this for the bundle, schema, and descriptor layers. Tests that use the
user's disc run locally only, and nothing decoded from it is committed.

## Reproduce the observations

From the repository root, after the
[build workflow](./USER_GUIDE.md#requirements-and-build-workflow) has
extracted an owned disc to `game\disc`. The tool reads the bundles in place
and writes only under the untracked `build\assets` directory.

```powershell
# M0 gate: decode all 7 schemas and all 759 nonempty descriptors exactly.
python tools\assets.py verify

# Uncurated name-hash candidates from the executable and bundles.
python tools\assets.py names

# One descriptor as a JSON tree, labelled with the candidates.
python tools\assets.py dump tutorial.tgz tutorial/textures-tutorial.xmb `
  --names build\assets\name-candidates.json
```

`verify` takes about 30 seconds and exits nonzero if any member fails.
`--data` selects a different `data` directory. Candidate names are
unreviewed: a hash can have several candidates, and some are chance
collisions.

## Open questions

- BXML: the marker token's purpose; the unused kind `0xB`; the game's
  global lookup for members of `#1b8535f0`, which no retail file needs.
- BXSD: the meaning of flags `0x0101`, the LEB128 field at `0x10`, and the
  text-name form (header byte `0x06` = 0), which is not observed.
- Names: curating the candidate map; names for hashes such as `#1b8535f0`,
  `#120336ef`, and `#ed4c66fd`.
- The `.ivd` layout (vertices, indices, and their split), and what the
  `.ipn` and its `#104db1cd` library describe.
- `.tex` pixel layout: swizzling, `P8` palettes, cube maps, and the
  negative format codes.
- `.chnl` channel encoding and compression; whether characters are skinned
  or rigid.
- Terrain block layout, transform encoding, coordinate system, and units.
