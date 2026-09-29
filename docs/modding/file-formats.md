# File formats

What is known and verified in the port. All integers are little-endian.

## PAK entry

A level PAK starts with tables (string table, directories, resource types), followed by the
stream of entries. Each entry starts with this header:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | flag (0/1) |
| 0x04 | 4 | offset: the file offset of this header itself |
| 0x08 | 4 | size of the whole entry (header + inline data) |
| 0x0C | 4 | raw size of the data (the record size the decoder uses) |
| 0x10 | 4 | manual file flag (0/1) |
| 0x14 | 4 | name length *n* |
| 0x18 | *n* | name: relative path, e.g. `meshes\chars\human.xbl_cap` |
| +0 | 4 | flag (0/1) |
| +4 | 4 | segmented (1: data is in a `_segNN.pak`, loaded when precaching) |
| +8 | 4 | type id (0 stx, 4 msh, 10 bnm, ...) |
| +12 | 4 | segment index (-1 if none) |
| +16 | 4 | memory-image offset (-1 if none) |
| +20 | 4 | memory-image size (-1 if none) |

Inline data follows the header. The self-referencing offset field makes entries easy to find by
scanning. Xbox-converted files carry an `xbl_` or `xb_` extension prefix in the PAK
(`human.xbl_cap`, `bonegroups.xbl_xml`, `ep3.xb_wml`), but the engine requests them by their
source name (`human.cap`, `bonegroups.xml`).

## STX texture

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | magic `STX\0` |
| 0x0C / 0x10 | 4 + 4 | width, height |
| 0x34 | 4 | BIR block size (file size = 0x104 + this) |
| 0x3C | 1 | format: 0x40 DXT1, 0x52 DXT3 (Morton-swizzled blocks), 0x56 BGRA |
| 0x40 | 32 | texture name, zero-padded; **the engine looks textures up by this name** |
| 0x104 | | `BIR\0` block; pixel data starts 0x80 into it |

The port's own texture code (`src/d3d/textures.cpp`) handles the Xbox formats at render time.

## MSH mesh

Only the string data is documented so far. Texture references are zero-terminated strings
relative to the mesh's folder, stored consecutively, for example:

```
palpatine_grey\0..\..\SharedMaps\SpecularMap\0palpatine_grey\0..\..\SharedMaps\SpecularMap\0
```

Shader-group names follow (`palpatine_greyGeo_palpatineBodySG`). The engine requests
`<mesh folder>\<name>.stx` for each reference, but only if the level's PAK lists that texture.

## Character skin table

In the game executable (`.data`). Each record is 16 bytes:

```c
struct SkinRecord { const char* name; const char* meshPath; uint32_t zero; const char* code; };
```

Examples: `{ "Anakin", "Anakin\\Anakin", 0, "AKN" }`,
`{ "Palpatine", "Palpatine_grey\\palpatine_grey", 0, "PLP" }`,
`{ "Sidious", "sidious\\sidious", 0, "SID" }`, `{ "Luke", "luke\\luke", 0, "LKE" }`.

About 75 skins are registered, including characters used in only a few levels (Emperor, Luke,
Serra, Cin Drallig). Level files (`levels\<level>.slp`) place characters as class instances
(`IVader`, `IOldObiwan`, `IStormtrooper`) with properties such as `Name`, `Transform`, `Player?`
and `Enemies`.
