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
  point where the PAK has something else. (The port works around both; see below.)
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

- **Resources the level's PAK lacks come from other PAKs.** When a requested resource is not
  further on in the level's PAK, the port takes the copy from wherever it is: earlier in the same
  PAK, or any other level PAK on the disc (the disc's PAKs are indexed once, when first needed).
  Ordinary resources are decoded from memory; animations and other memory-image resources are
  handed to the engine's own memory-image loader; manual entries (a mesh's `.ban`) get a reader
  over the copy. The level's own stream is not moved. Logged as `PAK: X loaded from <pak>`.
- **Characters a level never had can be added.** A level only loads what its PAK header
  *declares*. When the engine asks for a character model, texture, animation or effect the level
  does not declare, and a PAK on the disc has it, the port declares it the way the engine's own
  folder scan does and lets the load go ahead (`Declare: ...` in the log). When that is a
  character definition (`meshes\chars\common\<name>.xml`), the port also brings what a level
  would have loaded for that character from its PAK: its main definition (`s_<name>.xml`) and its
  tables (`r_<name>.csv`, `a_<name>.csv`). Tables the engine opens by name later, such as a
  parent class's (a padawan uses `b_JediKnight.csv`), are declared when it opens them. This is how
  Yoda fights in a versus arena, and how you can play a level as another character (see the
  [debug menu](../debug-menu.md)'s `duelist` and `player` commands).
- **Missing animations are found by name.** Before a character is set up, the engine checks that
  every animation it requires exists, and fails the character otherwise. That includes its own
  animations, ones it shares with other characters, and the grapple animations of the characters
  it can fight. The port takes the engine's list of missing names, finds each as a file
  (`<name>.bnm`) in the disc's PAKs, loads it, and lets the engine check again
  (`Declare: N of M missing animation(s) found on the disc`).

The engine's animation index (which character scripts use to find animations by name) is built
once per level; declaring an animation marks it for a rebuild. Room in the level's string table is
reserved when the level starts, because the engine keeps pointers into it.

- **Some resources are edited as they load.** The port can change a PAK resource's data before the
  engine reads it (in the code, `RegisterResourcePatch`); it adds Yoda's pictures to the versus
  select and arena menus (`interfc\front_end\xml\select_jedi.xml`, `select_arena.xml`) this way.
  A loose copy of such a file under `mods\` is used as it is, without the port's edits.
- **Animation bindings the engine rebuilds go to `cache\disc\`.** When a character's `.ban` (which
  links its model to its animations) lacks animations the character needs in this level, the
  engine rebuilds it and writes it next to the model on the disc drive. The disc is read-only, so
  the port writes it to `cache\disc\<same path>` and reads it from there (also when the engine
  reads a binding straight back after rebuilding it: the rebuilt copy wins over any PAK's); it is
  safe to delete. A body worn by another class (`player <class> mesh <mesh>`) always gets such a
  rebuild.
  Only `.ban` files are redirected. Declared resources a level loads directly from the disc (not
  from its own PAK) are also taken from other PAKs when the disc has no such file.
- **Some resources are made by the port.** A resource no PAK has can be generated from another when
  a level asks for it (in the code, `RegisterResourceGenerator`), e.g. the dark textures of Yoda's
  player 2 in Yoda against Yoda (`meshes\chars\yoda\yoda_head_duel.stx`, darkened copies of his textures,
  the way the game's fighters have `_duel` textures for player 2). They are written to
  `cache\disc\<same path>` and read from there like loose files, remade every session, and safe to
  delete. A disc folder's listing also reports the files only `mods\` or `cache\disc\` has for it.

What is declared is limited to character content (`meshes\`, `animation\`, `effects\`,
`textures\`); a level's menus, text and level files stay its own. If a character still does not
initialize, the log names what is missing, e.g.
`You are missing required animations for character Yoda: ...`.

- **A mod's new resources are declared too.** A file under `mods\` with a name no PAK has (a
  body of your own, `mods\meshes\chars\<folder>\<file>.msh`) is declared to the level when it is
  asked for, and read as a loose file (`Declare: ... , loose` in the log).
- **Texture sets are loaded from other levels.** A character's texture set (`player ... skin 1`,
  e.g. the 501st clone markings) names textures only some levels have; the port declares them from
  the levels that do.

- **Animations never made get a stand-in.** A character can require an animation no PAK has: a Jedi
  brute needs Anakin's finish of his fifth combo on a brute (`anakin_atk_sse5_jdbrutegr_part2`) when
  Anakin is around and that combo is unlocked, which the story never pairs. A grapple move against one
  kind of opponent takes the same move's version against a Jedi instead, under the missing name
  (`Declare: ... never made, stands in as ...` in the log); without it the character is not created.
- **What is pulled in stays for the level.** A level keeps its own resources loaded; one the port
  brings in from another level is held the same way (the port keeps a reference of its own), so a
  spawned character's model is not freed when the last one wearing it dies (the next one would crash).
  The port also holds every character model the level itself loads (`meshes\chars\`), which only
  the characters wearing it would otherwise hold: a live change of character removes the level's
  player, and its model must stay for the next one in it.

Only content that is on the disc can be pulled in this way. Entirely new content (a character
from another game) still has to be provided as loose files under `mods\`, and memory-image
resources (animations) cannot be replaced by loose files yet.
