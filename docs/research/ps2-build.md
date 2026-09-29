# The PS2 build of Revenge of the Sith as a reference

PS2 NTSC-U (SLUS-21143, disc version 1.03). Facts only; no game data is kept in the project.

## Layout

- `SLUS_211.43`: a small MIPS ELF loader. It has `.symtab`/`.strtab` sections, but they are empty.
- `COREC.BIN` / `CORED.BIN`: the game's code and data as Metrowerks CodeWarrior relocatable modules
  (`MWo3`, internal names `reinitc.bin` / `reinitd.bin`). No function names inside.
- `ENGINE\PS2PAK_0.PK2`, `PS2PAK_1.PK2`: all game files, indexed by `PS2PAK.HSH`, a text file with one line
  per file: `path archive size offset` (1023 files).

## What it has that the Xbox disc does not

- Developer configs for every platform:
  - `Resource_Xbox.cfg`, `Resource_PC.cfg`, `Resource_ps2.cfg` -- the resource type table (`SET_DIRECTORY`
    extension -> folder / type number). The Xbox one maps e.g. `stx` textures, `WXB` sound, `WMA` music,
    `XB_SML`/`XB_WML`/`HWX` audio. Needed by the engine's loose-file pack mode (`pack:0`); the port's own mod
    loader (`mods\`) does not need it.
  - `default_ps2*.cfg` -- developer switches. Our Xbox executable recognizes 38 of them, including
    `useSelectScreen` (developer level select), `logframerate`, `logload`, `logmemorystats`, `cheats`,
    `playerMesh`, `variations`, `nextmap`, `loadparams`, `res`, `fullScreen`, `vsync`, `vsyncInterval`,
    `language`, `enablestreaming`/`emulatestreaming`/`measurestreaming`. Untested in the port so far.
    PS2-only: `w_unlockcampaigns`, `w_unlockmedals`, `w_unlockvariations`, `logtextures`, `logmemory`, ...
  - `default_xbox_makepack.cfg` -- how the Xbox PAKs were built (`packlist=1`, a map and `nextmap`).
  - `binds_pc.cfg`, `bindcfgs_pc.cfg` -- the engine's PC keyboard/mouse bindings, inherited from Indiana
    Jones (`COMMAND_PUNCH`, `KICK`, `INVENTORY`...), not designed for this game. Our Xbox executable still
    contains the engine's `KEYBOARD`/`MOUSE`/`KEY_*` names; whether its keyboard input path works is untested.
  - `EP3PS2DiskPak*.cfg`, `PS2Boot.cfg`, `PS2DiskPak_PAL.cfg` -- PS2 disc packing lists.
- The same 126 level PAKs by name as the Xbox (no extra levels); PS2-format audio (`.ilv`), dialogue
  (`.rp2`) and movies (`.sfd`).
- Leftover class names from The Collective's earlier Buffy game in the data (`IZombie`, `IVampire`,
  `IOldBones`).

## For symbols

No names: the code modules carry no symbols. Same game and source, so shared strings could still match
PS2 functions to ours, but that gives no new names.
