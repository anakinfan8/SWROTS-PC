# Getting started

## The mods folder

`mods\` sits next to `swrots.exe`.
A file in `mods\` replaces the game file with the same relative path. Paths are the ones the
game uses on the disc (the `D:` drive), for example:

| Game file | Mod file |
|---|---|
| `D:\vars_xbox.cfg` | `mods\vars_xbox.cfg` |
| `D:\meshes\chars\anakinhooddown\anakinhooddown.msh` (inside a level PAK) | `mods\meshes\chars\anakinhooddown\anakinhooddown.msh` |
| `D:\interfc\hud_b\hud_face_vader01.stx` (inside a level PAK) | `mods\interfc\hud_b\hud_face_vader01.stx` |

This works both for files that exist on the disc and for resources packed inside the level
PAKs (`D:\pak\res_<level>.pak`). Folders that exist only inside a PAK can simply be created
under `mods\`.

## Settings for modders

These optional sections go in `settings.ini` next to `swrots.exe`, alongside the
[player settings](../settings.md):

```ini
[Mods]
DumpResources=0        ; 1: save every resource a level loads to dump\ (see dumping-assets.md)

[Debug]
LogResources=0         ; 1: log every resource read, with its path and type id
Console=0              ; 1: open a console window showing the log
```

`logs\swrots.log` (next to the exe) lists every file a mod replaced (`Mod override:` for disc files,
`Loose resource:` for PAK resources). It also shows the engine's own warnings as `[engine] ...`
lines. The retail game normally discards these, but they are the first place to look when a
mod does not work.

## Useful config overrides

Copy the game's config files into `mods\` and edit the copies:

- `vars_xbox.cfg`: engine variables, one `name=value` per line, e.g. `aiDisabled=true`,
  `god=true`, `fps=true`. The full list is in [engine variables](../research/engine-variables.md).
  The fps counter and the other debug displays also need `DebugDisplays=1` in the `[Debug]` section of
  `settings.ini`. (The frame rate is set in `settings.ini`, see [settings](../settings.md), which
  overrides `fpsLimit` here.)
- `Default_Xbox.cfg`:
  - `map:<level>` boots straight into a level, e.g. `map:m08_cor_palpatinesoffice` or
    `map:u170_ep4_deathstar_01`. Level names are the PAK names without `res_` and `.pak`.
  - `allowskipmovie:1` and `playlicensemovie:0` shorten startup.
  - `detectDirectLaunch:0`.

Never set `pack:1` in `Default_Xbox.cfg`. That mode rebuilds, and deletes, the level PAKs.
