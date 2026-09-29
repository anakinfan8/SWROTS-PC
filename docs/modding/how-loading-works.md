# How loading works

## Level PAKs

Each level has a PAK, `D:\pak\res_<level>.pak`. Some levels also have `_segNN.pak` files that
hold streamed texture data. A PAK is built for one level: it contains every resource that level
loads, **stored in the order the level loads them**.

The engine reads the PAK as a stream. For each resource the level asks for, it:

1. reads the next entry's header;
2. checks that the entry has the name it expects (otherwise it warns
   `Pack reader ... was expecting resource 'X', but found 'Y'` and misreads);
3. decodes the data;
4. moves to the end of the entry.

Two consequences:

- A resource cannot simply be *added* to a level. The level never asks for it, or asks at a
  point where the PAK has something else.
- Changing what a level asks for breaks the order. For example, a different mesh references
  different textures, and a mesh's textures are expected right after it. The old modding
  approach was therefore to rebuild the PAK.

Some resources are not read inline. Most textures in segmented levels only have a header in the
stream; their pixels are loaded later, during "Precaching textures". Animations (`.bnm`) mostly
come from the PAK's memory-image block.

## What the port changes

The port hooks the engine's resource reader (`SceneResPacker::Read`, see `src/game/resources.cpp`).

- **Loose resources skip the PAK.** If `mods\<path>` exists for a requested resource, the port
  decodes that file directly, using the engine's own file reader and decoder in its "not from a
  PAK" mode. This makes textures decode fully instead of becoming precache placeholders. The PAK
  stream is not touched.
- **Unused entries are skipped.** When the next PAK entry is not the requested one, but the
  requested one appears later in the same PAK, the port reads forward to it. The skipped entries
  are the packed originals of replaced resources, which nothing asks for any more. Each skip is
  logged as `PAK: skipping from A to B`. The port finds entries with its own PAK index
  (`src/game/pak.cpp`).
- **Engine warnings are logged.** Messages the retail build discards appear in `logs\swrots.log` as
  `[engine] ...`.
- **Disc files are overridden too**, e.g. configs, movies and audio banks opened by path.

Resources a level never requests still cannot be added this way. That needs either making the
level request them (editing level or character data) or loading levels entirely from loose
files. The engine has a development mode for that (`pack:0`), which needs a complete loose copy
of the level, including animations.
