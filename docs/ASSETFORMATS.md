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
by [retail.json](../fingerprints/retail.json), inspected on 2026-11-22. The
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
| Props and weapons | `objects-*`, `materials-*`, `textures-*`, `objecttypes-*` | `objects-*.ipn`, `objects-*.ivd`, `textures-*.tex` | Descriptors are partly decoded; payloads are unknown. |
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

### Header and namespace table

**Verified for all 759 nonempty BXML members:**

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | Magic `BXML`. |
| `0x04` | 4 | Bytes `01 01 07 00` in every file. Probably version fields; **Unknown** meaning. |
| `0x08` | u32 | Offset of the name table, always `0x11 + 8 * N`. |
| `0x0C` | u32 | Always zero. |
| `0x10` | u8 | `N`, the namespace count. |
| `0x11` | `N` × 8 | Namespace pairs: u32 prefix hash, then u32 namespace-URI hash. A prefix hash of zero is the default namespace. |

All integers are little-endian. Every file declares
`xsi` = `http://www.w3.org/2001/XMLSchema-instance`: as the last pair in
753 files, and as the first pair in the six `common/stringtable` files.

**Verified** namespace declarations per descriptor family:

| Family | Namespaces (prefix = URI) |
| --- | --- |
| `objects`, `materials`, `textures`, `animations` | `acg=acg`, default=`daxns`, `xsi` |
| `levels`, `objecttypes`, `characters/ms_*` | default=`bfns`, `acg=acg`, `daxns=daxns`, `xsi` |

This matches a format string in the executable that writes
`<texture-library ... xmlns="daxns" ...>`. So `daxns` is the asset
namespace (textures, materials, meshes, archetypes, animation channels), and
`bfns` is the game namespace (levels, object types, motion sets). Matching
binary schemas, `daxns.xsb` and `bfns.xsb`, are in `common/schemas`.

### Name table

**Verified.** At the offset given by the header field at `0x08`:

- An unsigned LEB128 count (seven bits per byte, low group first, high bit
  set when another byte follows). Counts of up to 630 parse as two-byte
  values and leave the following structure aligned.
- `count` × u32 name hashes.

**Observed.** The table can list names that the document never uses:
`default-playerstats.xmb` lists 17 names, of which two do not appear in its
token stream. **Verified:** all 60 nonempty `objects-*.xmb` and 61
`materials-*.xmb` members carry one identical 110-name table. **Hypothesis:** the table lists the schema vocabulary in scope rather
than only the names a document uses. Element tokens refer to names by their
index in this table.

### Structure block and tokens

**Partly verified.** Immediately after the name table:

| Offset | Size | Field |
| --- | --- | --- |
| `+0` | u16 | `A`. In 677 of 759 files it is the length, in u16 words, of the block that starts here and ends with the first `0xC000` token. |
| `+2` | u16 | `B`. **Unknown.** In the smallest files it equals the token count; in larger files it does not. |
| `+4` | 4 | Flags or counts. **Unknown.** The most common values are `00 00 00 00`, `00 00 00 01`, and `00 00 00 80`. |
| `+8` | u16 words | Token stream, starting with `0x0000`. |

The 82 files that break the `A` rule are all 36 files with
`A >= 0x1000` (26 sounds, 8 levels, and 2 animations descriptors) and 46
others in which a `0xC000` word occurs earlier. **Unknown:** whether the
high bits of `A` are flags, and whether the earlier `0xC000` words are
tokens or data.

**Observed** in the small `default-global-bfdata.xmb`,
`default-playerstats.xmb`, and `profiles/profile-default.xmb`. Each token
is a u16 whose top four bits give a kind and whose low 12 bits are a
name-table index:

| Top nibble | Reading in the small samples |
| --- | --- |
| `0x0` | Start element *n*. The root is index 0. |
| `0xA` | Attribute *n* of the current element; its value is in the value area. |
| `0xC` | End element *n*. The stream ends with `0xC000`. |
| `0x9`, `0x1`, `0xE`, `0xF`, others | **Unknown.** All 16 kinds occur in larger files. Runs of `0xFExx`/`0xFFxx` words with steadily increasing low bytes in `textures-common.xmb` suggest packed tables or offsets rather than tokens. |

For example, `profile-default.xmb` is one root element with 12 attributes:
the tokens are `0000`, `A001` through `A00C`, and `C000`.

### Value area

**Observed.** Typed values follow the token stream. 32-bit little-endian
IEEE floats appear (for example `1.0` as `00 00 80 3F`). A string is
prefixed with its length: `textures-common.xmb` stores the 19-character
data-file name preceded by the byte `0x13`. **Unknown:** how values are
typed. **Hypothesis:** the value layout is schema-driven, so decoding values
requires the matching BXSD type of each element and attribute, including
the `stringid` type, which may store a hash rather than text.

### Next steps for BXML

The fastest authoritative route is to trace the game's own reader in the
generated code, starting from callers of the name-hash function
`sub_00238370` and from the code that checks the `BXML` magic. That reader
defines the token kinds, the meaning of `A` and `B`, and the value
encoding. Check each conclusion against all 759 files, not only the small
samples.

## Layer 4: BXSD schemas (`.xsb`)

**Verified.** There are seven schemas, all in `common/schemas`: `bfens`,
`bfns`, `daxns`, `gameflags`, `gameflags-downloadable`, `global-gameflags`,
and `limp`. The header is `BXSD` followed by `01 00 01 00` and a u32 that
varies by file. **Unknown:** that u32's meaning, and the full grammar.

**Observed** in `limp.xsb` and `daxns.xsb`:

- `0x21` is followed by a u32 name hash: a declared name.
- `0x22` is followed by a u32 namespace hash and a u32 local-name hash: a
  qualified type reference, such as XML Schema `string` or a `daxns`
  complex type.
- Declarations are introduced by small codes such as `0x09`, `0x0A`, and
  `0x19`. A `0x09` declaration ends with a small count (for example
  `00 03`), which may be its number of children.

Name coverage per schema, counting `0x21` name tokens, is 153 of 297 for
`daxns.xsb` and 1,320 of 2,580 for `bfns.xsb`. **Hypothesis:** the schemas
give each BXML element and attribute its value type. That would make
decoding `daxns.xsb` a prerequisite for reading asset values, and a curated
source for confirming recovered names.

## Layer 5: asset families

The element names in this section are curated recovered names from the
`tutorial` and `common` bundles. Use them as vocabulary to look for, not as
a schema: nesting, cardinality, and value types are **Unknown** until BXML
and BXSD decoding is complete.

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
- **Hypothesis:** each `image` or `texture` has an `offset` and a `size`
  inside the `.tex` file, plus a `format`, `width`, and `height` that use
  Xbox (NV2A/D3D8) texture format codes. Linear formats would then be
  swizzled, and DXT formats stored as blocks. `u0`/`v0`/`u1`/`v1` would be
  sub-rectangles for animated or atlas textures.
- **Unknown:** the format-code mapping, mip layout, cube maps, palettes,
  and the meaning of `optimized-texture`.

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
- **Hypothesis:** a `mesh` is split into `geoset`s, one per material, each
  pointing at a range of a vertex buffer with a given `vertex-type`, and a
  range of an index buffer, drawn as triangle strips. An `archetype` is a
  hierarchy of `part`s, each referring to a mesh, joined at named
  hardpoints. `lod-switch` selects between detail levels.
- **Unknown:** which file holds vertices and which holds indices (the
  strip-like prefix argues that `.ivd` holds at least some indices), the
  vertex layouts and packed normal formats, index width beyond the u16
  prefix, primitive types other than strips, bounding volumes, and the
  coordinate system.

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

Keep the format code independent of Blender:

1. **`bundle`:** safe tar access by member name; see
   [Layer 1](#layer-1-bundles).
2. **`names`:** the name hash, and a curated name map rebuilt locally from
   the user's own executable and disc.
3. **`bxml` and `bxsd`:** decode into a generic tree of elements,
   attributes, and typed values. Keep unresolved names as `#xxxxxxxx` and
   raw values next to typed ones.
4. **`model`:** typed asset objects (texture, material, mesh, archetype,
   animation, level) built from the trees, with every cross-reference and
   payload range checked.
5. **Blender add-on:** a thin layer that turns `model` objects into Blender
   data, and holds the coordinate conversion.

Layers 1-4 should be pure Python with no third-party dependencies, so they
run in Blender's bundled interpreter and in the project's test runner.
A command-line dump, for example a JSON tree, a glTF file, or PNG textures,
written to a local, untracked directory, lets each layer be validated
without Blender.

Milestones, each gated on its own evidence:

| Milestone | Delivers | Gate |
| --- | --- | --- |
| M0 | Name hash, curated names, BXML/BXSD tree dump | Every nonempty `.xmb` and `.xsb` parses to its end with no unexplained bytes; the token grammar matches the game's reader. |
| M1 | Textures | At least two formats decode, and match runtime texture data for the same images in two bundles. |
| M2 | Static meshes | One known prop imports with correct shape, normals, UVs, and materials, numerically checked against a CPU-renderer capture, then repeated with a second bundle. |
| M3 | Props with parts, hardpoints, and LODs | One multi-part archetype assembles correctly. |
| M4 | Levels | Tutorial terrain and object placement match a reviewed in-game view; then a second level. |
| M5 | Characters and animation | One character's hierarchy, bind pose, and one animation match the running game. |

Synthetic fixtures must cover empty, truncated, oversized, and malformed
members for every parser. Tests that use the user's disc run locally only,
and nothing decoded from it is committed.

## Reproduce the observations

From the repository root, after the
[build workflow](./USER_GUIDE.md#requirements-and-build-workflow) has
extracted an owned disc. This script prints each nonempty BXML member's
namespace pairs and name count, reading archives without extracting them.
Combine it with the `name_hash` function above to label hashes.

```powershell
@'
import struct, tarfile
from pathlib import Path

def leb128(data, i):
    value = shift = 0
    while True:
        b = data[i]; i += 1
        value |= (b & 0x7F) << shift; shift += 7
        if b < 0x80:
            return value, i

for path in sorted(Path(r"game\disc\data").glob("*.tgz")):
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            if not (member.isfile() and member.size and member.name.endswith(".xmb")):
                continue
            data = archive.extractfile(member).read()
            assert data[:4] == b"BXML"
            n = data[0x10]
            assert struct.unpack_from("<I", data, 8)[0] == 0x11 + 8 * n
            pairs = list(struct.iter_unpack("<II", data[0x11:0x11 + 8 * n]))
            count, _ = leb128(data, 0x11 + 8 * n)
            print(member.name, [f"{p:08x}={u:08x}" for p, u in pairs], count)
'@ | python -
```

## Open questions

- BXML: token kinds other than start, attribute, and end; the meaning of
  `A`, `B`, and the flag word; files with `A >= 0x1000`; value typing.
- BXSD: the full declaration grammar and the header field at `0x08`.
- Which of `.ipn` and `.ivd` holds vertices, indices, or both, and the
  vertex layouts.
- `.tex` format codes, swizzling, and mip layout.
- `.chnl` channel encoding and compression; whether characters are skinned
  or rigid.
- Terrain block layout, transform encoding, coordinate system, and units.
