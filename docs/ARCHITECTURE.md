# Game and asset architecture

This is an evidence-led map of the locally supplied disc, not a complete game
engine or file-format specification. The executable revision is identified in
[retail.json](../fingerprints/retail.json); its hash does not authenticate the
other disc files. Counts and sample sizes below are observations from that
disc, and may differ on other releases. The disc, archives, payloads, and
runtime captures remain local.

## Disc layout and ownership

The extracted disc has 685 files. The relevant top-level distribution is
`data` (496), `fx` (153), `Mech` (11), `videos` (7), six root files,
`cdxmedia` (6), `media` (4), `Images` (1), and `music` (1).

```text
game\disc\
  default.xbe                    supported Brute Force executable
  data\
    common.tgz, e*.tgz, m*.tgz  shared and named resource bundles
    ml-sounds\<language>\*.tgz  language-named bundles
    sounds\*.xwb
    movies\*.bik, movies-pal\*.bik
  fx\fx.xbe                     Tao Feng Demo v0018
  Mech\Mech.xbe                 MechAssault Demo
  demos.xbe, downloader.xbe, dashupdate.xbe, update.xbe
  cdxmedia\, media\, videos\, Images\, music\
```

The layout and the auxiliary XBE titles are observed. File names suggest
shared, episode/mission-like, language, and movie groupings, but names alone
do not prove level boundaries or runtime ownership. In particular, `.jwad`,
`.dds`, `.xmv`, and most direct `.xpr` samples are under `fx`, while `.mgf`
and `.d1Bank` are under `Mech`; do not treat them as Brute Force's model
formats merely because they are on the same disc. Ownership of every other
top-level resource has not been established.

## Executable and runtime boundaries

The main XBE contains a large `.text` section and separately named `D3D`,
`XPP`, `DSOUND`, `XONLINE`, `XNET`, and `BINK` sections. These show linked
graphics, input, audio, networking, and video-related code, not a complete
map of the game's own classes or proof that those subsystems work in the port.
The recorded static classification contains 26,887 candidate functions,
including 13,771 marked unknown; its labels are heuristic. The game's class
hierarchy, physics, AI, scene graph, and relationships between gameplay
systems have not yet been established.

At a high level, the current host validates and maps `default.xbe`, initializes
the framework's kernel and guest paths, and enters the statically recompiled
guest. The guest opens disc bundles and cached resources; with graphics
enabled, its commands are handled by the experimental CPU pushbuffer executor
and presented with D3D11. USB support is optional. This describes the present
recompilation environment, not the original Xbox's complete engine design. See
[FINDINGS.md](./FINDINGS.md) for observed guest entry points and limitations.

## Packaging and runtime paths

The `data` tree contains 361 `.tgz` files: 56 directly under `data` and 305
under `data\ml-sounds`. The latter are divided evenly among `de`, `en`, `es`,
`fr`, and `it` (61 each). It also has 55 direct `.xwb` files under
`data\sounds` and 80 `.bik` files split evenly between `movies` and
`movies-pal`.

**Verified samples:** `common.tgz`, `e01.tgz`, `m01_a.tgz`,
`splash_screen.tgz`, and representative English and German language bundles
open as gzip-compressed tar archives. This does not prove every `.tgz` on
every disc has the same structure. `common.tgz` has 50 regular files;
`e01.tgz` has 21. The sampled language bundles each contain one `.xwb` member
with a `WBND` prefix. Tar member names include their resource directory, for
example `common/objects-common.xmb` and `e01/objects-e01.ipn`.

A local runtime trace opens `D:\data\common.tgz`, creates matching members
under `Z:\common\...` on `\Device\Harddisk0\Partition5`, and later opens
`schemas`, `objects`, and `textures` members there. This is evidence of a
disc-bundle-to-cache workflow for that run, not proof that every resource
follows it or that a file open means successful decoding. `Z:` is a guest
path, not a path an importer should hardcode on the host. Save and cache
isolation is described in [README.md](../README.md#experimental-controller-diagnostic).

## Resource families inside the game bundles

The table distinguishes observed names and signatures from unverified roles.
Examples are tar member paths, not files that this project distributes.

| Resource | Observation | Remaining uncertainty |
| --- | --- | --- |
| `.xmb` | All 35 nonempty `.xmb` members in `common.tgz`, and all 13 in each of `e01.tgz` and `m01_a.tgz`, begin with `BXML`. Names include `objects`, `levels`, `materials`, `textures`, `animations`, and character/game configuration. | The `BXML` signature does not establish a standard XML encoding or a decoded schema. Byte order, records, strings, references, and version semantics are unknown. |
| `.xsb` | The seven `common/schemas/*.xsb` members begin with `BXSD`. In contrast, disc files `cdxmedia\cdx.xsb` and `media\Sound.xsb` begin with `SDBK`. | The extension identifies at least two different content families here. Do not feed the `BXSD` schema-named files to a sound-bank parser solely because of their suffix; neither layout has been decoded. |
| `.ipn`, `.ivd` | `objects-<bundle>.ipn`, `.ivd`, and `.xmb` occur together in the three sampled resource bundles. The beginning of the sampled `.ivd` files can be read as small little-endian 16-bit integers. | They are promising geometry-related candidates by association, but their roles, element types, strides, offsets, topology, and compression are unverified. Small integers alone do not prove an index buffer. |
| `.tex`, `.xmb` | `textures-<bundle>.tex` accompanies `textures-<bundle>.xmb` in the same samples. | Texture metadata/payload linkage, formats, dimensions, mipmaps, and any Xbox swizzle or tiling are unknown. Do not assume the `.tex` payload is DDS. |
| `.chnl`, `.xmb` | `animations-<bundle>.chnl` accompanies `animations-<bundle>.xmb`. | Animation channels, skeleton representation, timing, and transforms are unverified. |
| `.mem`, `.xmb` | `sounds-<bundle>.mem` accompanies `sounds-<bundle>.xmb`. | Their relationship and audio encodings are unverified; names alone are not a decoder specification. |
| `.gscr` | The sampled `e01` and `m01_a` bundles contain script-named files that start with ASCII text. | Full encoding, grammar, and runtime semantics have not been established. |
| `.xbx` | `common/camp-save-image.xbx` begins with `XPR0`; sampled direct `.xpr` files under `fx` and `cdxmedia` share that prefix. | The individual resource layouts and texture encodings have not been decoded. |
| `.xwb`, `.bik` | Sampled direct `.xwb` files and language-bundle members start with `WBND`; sampled `.bik` files start with `BIKi`. The XBE also has `BINK`-named sections. | These signatures are consistent with Xbox wave banks and Bink video, respectively, but their variants and successful host decoding are not verified. |

Empty members are valid evidence too: `splash_screen.tgz` contains empty
`animations-splash_screen.xmb`, `animations-splash_screen.chnl`, and
`objects-splash_screen.xmb`, `.ipn`, and `.ivd` members. Importers must not
assume every named resource has a header or payload.

### Geometry-related sample sizes

These byte counts make it possible to recognize the same sampled members
without copying their contents. They do not establish which member holds
vertices or indices.

| Archive | `objects-*.xmb` | `objects-*.ipn` | `objects-*.ivd` |
| --- | ---: | ---: | ---: |
| `common.tgz` | 67,427 | 144,604 | 1,947,256 |
| `e01.tgz` | 108,767 | 3,758,724 | 5,363,076 |
| `m01_a.tgz` | 152,849 | 1,238,244 | 3,743,668 |

## Reproduce the archive inventory

After extracting an owned disc with the project's
[build workflow](../README.md#requirements), run this from the project root.
It reads archive metadata without extracting or writing members:

```powershell
@'
from collections import Counter
from pathlib import Path
import tarfile

data = Path(r"game\disc\data")
for name in ("common.tgz", "e01.tgz", "m01_a.tgz", "splash_screen.tgz"):
    with tarfile.open(data / name, "r:gz") as archive:
        files = [member for member in archive if member.isfile()]
        extensions = Counter(Path(member.name).suffix.lower() or "<none>" for member in files)
        print(name, len(files), dict(extensions))
'@ | python -
```

Use the local runtime diagnostic described in the README to reproduce the
`D:` and `Z:` observations. Its logs and any read-byte traces can contain
game-derived data and should stay local.

## Future Blender importer: staged investigation

1. Inventory additional owned-disc bundles, including empty and unusual
   entries. Read tar members in place or validate paths, sizes, and member
   types before extraction; reject traversal, links, overflows, and excessive
   allocations. Record revision and sample provenance.
2. Reverse `BXML`/`BXSD` using small, independently comparable resources.
   Establish header fields, bounds, strings, record types, and links between
   resources; do not infer a complete schema from magic bytes.
3. Trace the relationship between `objects-*.xmb`, `.ipn`, and `.ivd`.
   Prove offsets, lengths, strides, index width, topology, and vertex
   attributes against more than one object and bundle. Check every range and
   index before allocating or constructing geometry.
4. Map material and texture references; validate dimensions, format, mip
   ranges, and whether a swizzle or tile transform is actually required.
   Separately establish skeletons, bind poses, animation channels, coordinate
   system, units, winding, normals, and UV conventions.
5. Keep the binary parser independent of Blender and expose a validated
   intermediate scene representation to an add-on. Use synthetic fixtures for
   malformed, empty, truncated, and boundary cases; run owned-asset checks
   locally without committing payloads. Validate at least one known object
   visually and numerically, then repeat with a second distinct bundle before
   claiming general support.

No Blender importer or validated mesh, material, texture, or animation schema
exists in this project yet.
