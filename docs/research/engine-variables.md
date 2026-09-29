# Engine variables and commands

Generated from the game's own executable by `tools/engine_vars.py` (not from notes). The engine registers
each console variable with the variable registry (reached through the engine-services pointer at
`0x645F7C`, `+0x38`, `+0x48`, virtual `+0xA0`): a name, a description, the field holding its value, and
limits. Registry slot `+0x24` registers integers, `+0x28` decimals, `+0x30` on/off switches, `+0x04`
console commands. Registration happens in several places:

- `0x2409A0` -- game options (the counterpart of Indiana Jones' `TGameOptions::InitConsole`)
- `0x9C1B0` -- `GameExec.cpp`: player/cheat variables and the console commands `setbind`, `addbind`,
  `setconfig`, `quit`, `disk`
- `0x241680` -- renderer debug switches (`rnd_*`, `scn_*`)
- `0x2E6340` -- Revenge of the Sith game code: progression, Force/skill, saber debug, commands `st`,
  `tweak`, `sabercolor`

Console variables can be set at startup in `vars_xbox.cfg`, one `name=value` per line (on/off values:
`true`/`false` or a number; put overrides in `mods\vars_xbox.cfg`). The reader (0x11510) runs before the
variables are registered; the console keeps such values pending and applies each when its variable
registers (0x12740), so every registered variable can be set this way (tested: `aiDisabled`, `fps`).
The log lists each pair read (`vars_xbox.cfg: name = value`).

The options object ([[0x645F7C]+0x38]+8) has an unregistered byte at +0xD7 that hides the engine's debug
displays: the constructor (0x157C0) sets it to 1, and the display code (e.g. 0xA0D80) draws only while it
is 0. The port clears it with `[Debug] DebugDisplays=1` in `settings.ini`; then `fps=true` shows the fps
counter, and the frame profiler (fps, time per timer category, recent spikes) and the memory display
(allocated / peak) appear.

### vars_xbox.cfg or Default_Xbox.cfg

Two separate systems; a name only works in its own file (only `checkpoint` and `vsync` exist in both).

| | `vars_xbox.cfg` | `Default_Xbox.cfg` |
|---|---|---|
| What | registered console variables (this table) | launch arguments ([below](#launch-arguments-default_xboxcfg)) |
| Format | `name=value` | `name:value` |
| Changeable while running | yes, through the console (`set name value`) | no console; a field changed in memory takes effect wherever the engine reads it next |

### Console commands

Usable in the port's debug menu (F1): see [debug menu](../debug-menu.md).


The console (`F:\Slayer\ENGINE\EConsole\console.cpp`) registers its own commands `help`, `listvars`
(prints `name [boolean]` / `[number]`...), `set` and `move_window`; the game adds those in the list below
(`setbind`, `addbind`, `setconfig`, `quit`, `disk`, `st`, `tweak`, `sabercolor`). The console's window and
text input are not in the Xbox build (no "Console" window strings); the command handling is. In Indiana
Jones and Marc Ecko's Getting Up (same engine) the window is opened with patches, and commands such as
`set god true`, `set gravity false` work there.

### Variables the game overwrites

A value from `vars_xbox.cfg` is only a starting value for these (found by scanning for game code that writes
the options object's fields; the scan covers the usual code pattern only):

- When a level starts, 0x2AAB00 copies the profile's settings into the options object: `subtitles`,
  `rumble`, `rnd_collision`, volumes, and `god` (from the profile's byte +0x2D, possibly its invincibility
  cheat). The port keeps `god=true` from `vars_xbox.cfg` there (`src/game/devoptions.cpp`).
- 0x13ADD4 turns `god` on (a cheat or a scripted moment, not identified).
- Gameplay code: `fov` (camera), `timeScaleFast` / `timeScaleSlow`, `volumemusic`, `rnd_npcMeshes`.

| Variable | Type | Description | Registered in |
|---|---|---|---|
| `inputstream` | integer | Player input stream | `0009C1B0` |
| `targetinputstream` | integer | Set nearest character input stream | `0009C1B0` |
| `swap` | integer |  | `0009C1B0` |
| `difficultyLevel` | integer | AI difficulty level | `0009C1B0` |
| `health` | integer | Player health | `0009C1B0` |
| `maxhealth` | integer | Player max health | `0009C1B0` |
| `power` | integer | Player power level | `0009C1B0` |
| `maxpower` | integer | Player max power | `0009C1B0` |
| `targethealth` | integer | Set nearest character health | `0009C1B0` |
| `targetmaxhealth` | integer | Set nearest character health | `0009C1B0` |
| `targetpower` | integer | Set nearest character power | `0009C1B0` |
| `targetmaxpower` | integer | Set nearest character power | `0009C1B0` |
| `chunkVelFact` | decimal | Chunk velocity multiplier | `0009C1B0` |
| `highrestextures` | on/off | Render high res textures | `0009C1B0` |
| `highresmeshes` | on/off | Render high res meshes | `0009C1B0` |
| `displaybumpbounds` | integer | Rendering character bump bounds | `0009C1B0` |
| `displayswaying` | integer | Render swaying mesh quad tree | `0009C1B0` |
| `debugcarryrules` | integer | Debug info for carry rules | `0009C1B0` |
| `debugsprites` | on/off | Debug sprite rendering | `0009C1B0` |
| `forcecapsules` | on/off | Always update capsules | `0009C1B0` |
| `framecapture` | on/off |  | `0009C1B0` |
| `light` | decimal |  | `0009C1B0` |
| `gameInfo` | on/off | Show IPointGameplayInfo text | `0009C1B0` |
| `timeScale` | decimal | Current time scale | `0009C1B0` |
| `InputDeadZone` | decimal | Set input dead zone | `0009C1B0` |
| `InputSaturationZone` | decimal | Set input saturation zone | `0009C1B0` |
| `setbind` | command |  | `0009C1B0` |
| `addbind` | command |  | `0009C1B0` |
| `setconfig` | command |  | `0009C1B0` |
| `quit` | command |  | `0009C1B0` |
| `disk` | command |  | `0009C1B0` |
| `DisplayTrivialPhysics` | integer |  | `000D8540` |
| `DisplayStaticManager` | integer |  | `00104B10` |
| `fixedtick` | integer |  | `002409A0` |
| `singletick` | on/off | Single tick mode | `002409A0` |
| `gravity` | on/off | Gravity constant | `002409A0` |
| `vsync` | on/off | VSync amount | `002409A0` |
| `grid` | decimal | Draw grid | `002409A0` |
| `watchmode` | integer | Object watch mode | `002409A0` |
| `aiDisabled` | on/off | Disables AI | `002409A0` |
| `physics` | on/off | Display physics/collision debug info | `002409A0` |
| `oldmovementdrop` | on/off | Turn on old movement drop | `002409A0` |
| `hud` | decimal | HUD alpha | `002409A0` |
| `maxlights` | integer | Maximum number of hardware lights | `002409A0` |
| `shadows` | on/off | Render stencil shadows | `002409A0` |
| `allshadows` | on/off | All shadow rendering | `002409A0` |
| `attachments` | on/off | Render attachment point debug info | `002409A0` |
| `fpsLimit` | integer | Engine FPS limit | `002409A0` |
| `maxFPSLimit` | integer | Max Engine FPS limit | `002409A0` |
| `charfade` | on/off | Character fade distance | `002409A0` |
| `minsoundreport` | on/off | Toggles channel sound info | `002409A0` |
| `volume` | decimal | Sound volume | `002409A0` |
| `noMusic` | on/off | Disable in-game music | `002409A0` |
| `timermaxcount` | integer | Maximum number of timers to display | `002409A0` |
| `deathtime` | decimal | Wait time after character death | `002409A0` |
| `watchrange` | decimal | Watch info render range | `002409A0` |
| `microsecondmin` | integer | Minimum number of micro seconds to display for hierarchy timer | `002409A0` |
| `psysDynLightsEnabled` | on/off | Toggles dynamic lights for particle spawning | `002409A0` |
| `defaultfov` | decimal | Default camera FOV | `002409A0` |
| `fov` | decimal | Current camera FOV | `002409A0` |
| `timeScaleSlow` | decimal | Timescale slow setting | `002409A0` |
| `timeScaleFast` | decimal | Timescale fast setting | `002409A0` |
| `transInfo` | on/off | AI Trans Info | `002409A0` |
| `god` | on/off | Toggles god mode for player | `002409A0` |
| `useranges` | on/off | Use attach ranges | `002409A0` |
| `fps` | on/off | Draw FPS on screen | `002409A0` |
| `characterInputDebug` | on/off | Debug character input | `002409A0` |
| `noCheat` | on/off | Disables tab-fly cheat | `002409A0` |
| `nogamepadCheat` | on/off | Disables right thumbstick or tab cheats | `002409A0` |
| `debugKillAllies` | on/off | Kill Allies When Using Shift K | `002409A0` |
| `buglog` | on/off | Display bug log | `002409A0` |
| `logSplats` | on/off | Log wall splats | `002409A0` |
| `logTriggers` | on/off | Log all triggers | `002409A0` |
| `logCinematics` | on/off | Log cinematics | `002409A0` |
| `logMusic` | on/off | Log music | `002409A0` |
| `logPickupPhysics` | on/off | Log pickup physics | `002409A0` |
| `cameraDistance` | decimal |  | `002409A0` |
| `cameraResetDelay` | decimal |  | `002409A0` |
| `cameraCombatRange` | decimal |  | `002409A0` |
| `cameraInvertFirstVert` | on/off |  | `002409A0` |
| `cameraInvertThirdVert` | on/off |  | `002409A0` |
| `cameraInvertThirdHoriz` | on/off |  | `002409A0` |
| `debugCamera` | on/off |  | `002409A0` |
| `gamma` | decimal | Gamma setting | `002409A0` |
| `triggerLogUnnamed` | on/off |  | `002409A0` |
| `earAtCam` | on/off |  | `002409A0` |
| `particleAxisSize` | decimal |  | `002409A0` |
| `volumemusic` | decimal | Music volume | `002409A0` |
| `volumefx` | decimal | Sound FX volume | `002409A0` |
| `volumevoice` | decimal | Dialog volume | `002409A0` |
| `rumble` | on/off | Allow controller rumble | `002409A0` |
| `logRumble` | on/off | Log out controller rumble | `002409A0` |
| `rumbleScale` | decimal | Allow global tuning of rumble | `002409A0` |
| `subtitles` | on/off | Display subtitles | `002409A0` |
| `tips` | on/off | Show in-game tips | `002409A0` |
| `debugtest` | on/off |  | `002409A0` |
| `mousespeed` | decimal | Mouse speed | `002409A0` |
| `dbanim` | on/off |  | `002409A0` |
| `scn_lightmaps` | on/off | Enable lightmap rendering in scenery? (disabled at present) | `002409A0` |
| `scn_textures` | on/off | Hide textures in scenery, rendering lightmaps only? (disabled at present) | `002409A0` |
| `scn_onlyfirstlayer` | on/off | Disable second through n-pass rendering (disabled at present) | `002409A0` |
| `rnd_wire` | on/off | Render wireframe | `002409A0` |
| `rnd_entities` | on/off | Render entities | `002409A0` |
| `rnd_meshes` | on/off | Enable mesh rendering?  Disabled means render _no_ meshes. | `002409A0` |
| `rnd_characterMeshes` | on/off | Enable character mesh rendering? | `002409A0` |
| `rnd_playerMesh` | on/off | Render the player character? | `002409A0` |
| `rnd_npcMeshes` | on/off | Render non-player characters? | `002409A0` |
| `rnd_staticMeshes` | on/off | Render static meshes? | `002409A0` |
| `rnd_trivialmeshes` | on/off | Render trivially managed meshes? | `002409A0` |
| `rnd_clipall` | on/off |  | `002409A0` |
| `rnd_scene` | on/off | Render scene geometry? | `002409A0` |
| `rnd_collision` | integer | Display collision info instead of scene rendering? | `002409A0` |
| `rnd_portals` | on/off | Display portals? | `002409A0` |
| `rnd_fog` | on/off | Enable depth fog? | `002409A0` |
| `rnd_brushes` | on/off | Render brushes? | `002409A0` |
| `rnd_bones` | integer | Render character bone/skeletons? | `002409A0` |
| `rnd_reflections` | on/off | Render reflections? | `002409A0` |
| `rnd_singlesector` | on/off | Render only the sector containing the camera, disabling others? | `002409A0` |
| `rnd_skybox` | on/off | Should the skybox be rendered? | `002409A0` |
| `rnd_skyboxonly` | on/off | Render only the skybox, disabling other sectors? | `002409A0` |
| `rnd_playeronly` | on/off | Render only the player, turning off all other polygons? | `002409A0` |
| `rnd_idonly` | integer | Renders only the object matching the id (0 means disable this feature) | `002409A0` |
| `rnd_particles` | on/off | Enable particle rendering? | `002409A0` |
| `rnd_particlebounds` | on/off | Display particle system render sphere bound | `002409A0` |
| `rnd_particleaxis` | on/off | Display particle system world matrix | `002409A0` |
| `rnd_shadows` | on/off | Render stencil shadows? | `002409A0` |
| `rnd_water` | on/off | Render water? | `002409A0` |
| `rnd_stripfx` | on/off | Render StripFX? | `002409A0` |
| `rnd_alphascene` | on/off | Render the alpha polygons that are part of the scene? | `002409A0` |
| `rnd_opaquescene` | on/off | Render the opaque polygons that are part of the scene? | `002409A0` |
| `scn_lightmaps` | on/off | Enable lightmap rendering in scenery? (disabled at present) | `00241680` |
| `scn_textures` | on/off | Hide textures in scenery, rendering lightmaps only? (disabled at present) | `00241680` |
| `scn_onlyfirstlayer` | on/off | Disable second through n-pass rendering (disabled at present) | `00241680` |
| `rnd_wire` | on/off | Render wireframe | `00241680` |
| `rnd_entities` | on/off | Render entities | `00241680` |
| `rnd_meshes` | on/off | Enable mesh rendering?  Disabled means render _no_ meshes. | `00241680` |
| `rnd_characterMeshes` | on/off | Enable character mesh rendering? | `00241680` |
| `rnd_playerMesh` | on/off | Render the player character? | `00241680` |
| `rnd_npcMeshes` | on/off | Render non-player characters? | `00241680` |
| `rnd_staticMeshes` | on/off | Render static meshes? | `00241680` |
| `rnd_trivialmeshes` | on/off | Render trivially managed meshes? | `00241680` |
| `rnd_clipall` | on/off |  | `00241680` |
| `rnd_scene` | on/off | Render scene geometry? | `00241680` |
| `rnd_collision` | integer | Display collision info instead of scene rendering? | `00241680` |
| `rnd_portals` | on/off | Display portals? | `00241680` |
| `rnd_fog` | on/off | Enable depth fog? | `00241680` |
| `rnd_brushes` | on/off | Render brushes? | `00241680` |
| `rnd_bones` | integer | Render character bone/skeletons? | `00241680` |
| `rnd_reflections` | on/off | Render reflections? | `00241680` |
| `rnd_singlesector` | on/off | Render only the sector containing the camera, disabling others? | `00241680` |
| `rnd_skybox` | on/off | Should the skybox be rendered? | `00241680` |
| `rnd_skyboxonly` | on/off | Render only the skybox, disabling other sectors? | `00241680` |
| `rnd_playeronly` | on/off | Render only the player, turning off all other polygons? | `00241680` |
| `rnd_idonly` | integer | Renders only the object matching the id (0 means disable this feature) | `00241680` |
| `rnd_particles` | on/off | Enable particle rendering? | `00241680` |
| `rnd_particlebounds` | on/off | Display particle system render sphere bound | `00241680` |
| `rnd_particleaxis` | on/off | Display particle system world matrix | `00241680` |
| `rnd_shadows` | on/off | Render stencil shadows? | `00241680` |
| `rnd_water` | on/off | Render water? | `00241680` |
| `rnd_stripfx` | on/off | Render StripFX? | `00241680` |
| `rnd_alphascene` | on/off | Render the alpha polygons that are part of the scene? | `00241680` |
| `rnd_opaquescene` | on/off | Render the opaque polygons that are part of the scene? | `00241680` |
| `p1Stream` | integer | Player 1's input stream | `002E6340` |
| `p2Stream` | integer | Player 2's input stream | `002E6340` |
| `p3Stream` | integer | Player 3's input stream | `002E6340` |
| `p4Stream` | integer | Player 4's input stream | `002E6340` |
| `skill` | integer | Player skill | `002E6340` |
| `exp` | integer | Player XP | `002E6340` |
| `forcelevel` | integer | Force level | `002E6340` |
| `useLightning` | on/off | Anakin uses lightning | `002E6340` |
| `unlockmission` | integer | Unlock Level | `002E6340` |
| `unlockprofile` | integer | Unlock entire Shell | `002E6340` |
| `combatskilllevel` | integer | Sets force power level | `002E6340` |
| `forcepowerlevel` | integer | Sets combat skill level | `002E6340` |
| `checkpoint` | integer | Sets last checkpoint | `002E6340` |
| `saberdebug` | on/off | Display saber debug info | `002E6340` |
| `saberThrowDebug` | on/off | Display saber throw debug path | `002E6340` |
| `st` | command |  | `002E6340` |
| `tweak` | command |  | `002E6340` |
| `sabercolor` | command |  | `002E6340` |

## Launch arguments (Default_Xbox.cfg)

A separate system from the registered variables above (`python tools/engine_vars.py --args`). At startup the
game asks the argument list (the parsed `Default_Xbox.cfg`, `name:value`) for each argument by name and
passes the address of a field in the settings object at `[[0x66F7A4]+4]`; the value is copied there, and the
engine then reads the field, not the file. Flipping a field in memory therefore works like changing the
file -- from the next time the engine reads it (continuously, at level load, or only at boot).

Right after each lookup the reader writes the field's default (e.g. `playerMesh` -1, `useSelectScreen` 0);
the value from the file still wins (checked at runtime: `useSelectScreen:1` leaves the field at 1), so the
lookup records the field and the value is applied afterwards.

Known to work (used in this project's configs): `map`, `pack`, `packlist` (writes each level's `.load` file
list -- showed the loading order), `playlicensemovie`, `allowskipmovie`, `detectDirectLaunch`,
`streamingmemoryscale`. The others are untested.

Compiled out: `useSelectScreen` is read into +0x106, but no code reads that field (the developer level
select is not in the Xbox build). Check any other argument the same way before relying on it.

Leads: `player` (text, +0xAC) and `playerMesh` (integer, +0x70, default -1) -- in Indiana Jones and Marc
Ecko's Getting Up `player` picks the player character's class; `playerMesh` may pick a variant of the
character's model (e.g. `anakin` / `anakinhooddown`). What the code shows:
- GameExec's init (0x9C1B0) copies both into the game-exec object (`player` text at +0x94, `playerMesh` at
  +0x98). With `playerMesh` -1 it takes the value from a table entry (0x76270; entries of 0x1C bytes, +0x14).
- `player` is passed, with the mesh, to 0xB1F90 (`Manager_Base.cpp`; mesh -1 means game-exec's +0x98), from
  0x2AB660, a virtual method not called on the direct `map:` launch path. 0x2D21A0 (also virtual) writes a
  name from its own table into the `player` field, like a mission or character selection would.
- Tested: `player:Obiwan` with `map:u170_ep4_deathstar_01` still plays Vader: the mission list overrides it.

### The mission list (gameinfo\missionlist.txt)

Inside every level PAK. One line per level, the header documents the format:
`EXEC <SpawnIndex> <PlayerMeshIndex> "MissionName" "Objectives" Mapname "Parameters"`. The parameters are
launch arguments in the `Default_Xbox.cfg` format and are applied at startup for the level being loaded,
over `Default_Xbox.cfg`: e.g. `Player:Anakin playermesh:2 v_difficultyLevel:1 Location:Mustafar ...`.

- `Player:` picks the player character: Anakin, Obiwan, Vader, Yoda, Grievous, GrievBGuard, Serra (with
  `Player2:` for the second player, e.g. Cin). Duel levels have none (the character select sets it).
- `playermesh:` picks the character's variant (Anakin 0-3, Obiwan 0-2 across the campaign).
- Tested (Death Star PAK copy in `mods\pak\` with `Player:OldObiwan`, same entry length): the setting takes,
  `player` reads `OldObiwan`, and the level loads the player first -- Old Obi-Wan's resources -- which the
  PAK holds in its NPC position. A PAK is read as one stream in the order it was packed for its player, so
  loading a different player needs that PAK's order to match: the engine reports "Pack reader was expecting
  X, but found Y" and then fails. Reading entries out of order (a second reader on the PAK) is not enough on
  its own: textures in segmented levels have only a header in the PAK (`raw 0`, one copy per segment) and
  their pixels are streamed from `_segNN.pak` in the packed order while precaching. On hold.
- A loose `mods\gameinfo\missionlist.txt` does not work yet (see the port's known issues).

| Launch argument | Type | Field in [[0x66F7A4]+4] | Read in |
|---|---|---|---|
| `rpecopy` | text | `+0x4` | `000141B0` |
| `map` | text | `+0xC` | `00013FD0` |
| `loadparams` | text | `+0x10` | `00013FD0` |
| `executable` | text | `+0x18` | `000141B0` |
| `profile` | text | `+0x1C` | `000141B0` |
| `execParams` | text | `+0x20` | `000141B0` |
| `allowskipmovie` | on/off | `+0x27` | `000141B0` |
| `allowgameskipmovie` | on/off | `+0x28` | `000141B0` |
| `alreadyPlayedMovieList` | text | `+0x2C` | `000141B0` |
| `playlicensemovie` | on/off | `+0x31` | `000141B0` |
| `detectDirectLaunch` | on/off | `+0x32` | `000141B0` |
| `sound` | integer | `+0x38` | `000141B0` |
| `Dialogue` | on/off | `+0x3D` | `000141B0` |
| `antiAlias` | integer | `+0x40` | `000141B0` |
| `vsync` | on/off | `+0x45` | `000141B0` |
| `forcebumpmapping` | on/off | `+0x47` | `000141B0` |
| `forcevolumetextures` | on/off | `+0x48` | `000141B0` |
| `packlist` | on/off | `+0x4E` | `000141B0` |
| `debuganims` | on/off | `+0x4F` | `000141B0` |
| `enablestreaming` | on/off | `+0x50` | `000141B0` |
| `emulatestreaming` | on/off | `+0x51` | `000141B0` |
| `measurestreaming` | on/off | `+0x52` | `000141B0` |
| `streamingglobalbudget` | decimal | `+0x58` | `000141B0` |
| `screenfadeexitcolor` | text | `+0x60` | `000141B0` |
| `screenfadeexittime` | decimal | `+0x64` | `000141B0` |
| `game` | text | `+0x68` | `000141B0` |
| `spawnindex` | integer | `+0x6C` | `000141B0` |
| `playerMesh` | integer | `+0x70` | `00013FD0` |
| `preferredWindowRes` | integer | `+0x74` | `000141B0` |
| `preferredFullscreenRes` | integer | `+0x78` | `000141B0` |
| `preferredBitDepth` | integer | `+0x7C` | `000141B0` |
| `memoryunit` | integer | `+0x80` | `000141B0` |
| `controller` | integer | `+0x84` | `000141B0` |
| `variations` | integer | `+0x88` | `000141B0` |
| `resurrected` | integer | `+0x90` | `000141B0` |
| `maxactivechar` | integer | `+0x94` | `000141B0` |
| `checkpoint` | integer | `+0x98` | `000141B0` |
| `timesinceinput` | decimal | `+0x9C` | `000141B0` |
| `player` | text | `+0xAC` | `00013FD0` |
| `session` | text | `+0xB0` | `000141B0` |
| `resmgrload` | text | `+0xBC` | `000141B0` |
| `packfilename` | text | `+0xC0` | `000141B0` |
| `sessionload` | on/off | `+0xC4` | `000141B0` |
| `loadInventory` | on/off | `+0xC5` | `000141B0` |
| `resetHealth` | on/off | `+0xC6` | `000141B0` |
| `inToolMode` | on/off | `+0xC7` | `000141B0` |
| `precachecharacters` | integer | `+0xD8` | `000141B0` |
| `delayInit` | on/off | `+0xDC` | `000141B0` |
| `dumpStrings` | on/off | `+0xDE` | `000141B0` |
| `makePrecacheItemsList` | on/off | `+0xDF` | `000141B0` |
| `usePrecacheItemsList` | on/off | `+0xE0` | `000141B0` |
| `dumpData` | on/off | `+0xE1` | `000141B0` |
| `bindAllAnims` | on/off | `+0xE3` | `000141B0` |
| `allanims` | on/off | `+0xE3` | `000141B0` |
| `optionalanims` | on/off | `+0xE4` | `000141B0` |
| `logframerate` | on/off | `+0xE6` | `00013FD0` |
| `precachepickups` | on/off | `+0xEB` | `000141B0` |
| `logPrecachePickups` | on/off | `+0xEC` | `000141B0` |
| `cinematicCapture` | on/off | `+0xEE` | `000141B0` |
| `cinematicCaptureTime` | decimal | `+0xF0` | `000141B0` |
| `cinematicCaptureScreenHeight` | integer | `+0xF4` | `000141B0` |
| `nextmap` | integer | `+0xF8` | `00013FD0` |
| `preservebackbuffer` | on/off | `+0x104` | `000141B0` |
| `allowSkippableLevelIntroMovies` | on/off | `+0x105` | `000141B0` |
| `useSelectScreen` | on/off | `+0x106` | `000141B0` |
| `spawn` | text | `+0x108` | `000141B0` |
| `language` | text | `+0x10C` | `000141B0` |
| `stream1` | integer | `+0x110` | `000141B0` |
| `stream2` | integer | `+0x114` | `000141B0` |
| `pack` | integer | `+0x118` | `000141B0` |
| `attractTriggerTime` | decimal | `+0x12C` | `000141B0` |
| `demoTimeout` | decimal | `+0x130` | `000141B0` |
| `demoTimeTotal` | decimal | `+0x134` | `000141B0` |
| `autosave` | on/off | `+0x138` | `000141B0` |
| `transtoshell` | on/off | `+0x139` | `000141B0` |
| `toShellWhenDone` | on/off | `+0x139` | `000141B0` |
| `loadScreenPrefix` | text | `+0x140` | `000141B0` |
| `objRampLow` | decimal | `+0x144` | `000141B0` |
| `objRampHigh` | decimal | `+0x148` | `000141B0` |
| `tileRampLow` | decimal | `+0x14C` | `000141B0` |
| `tileRampHigh` | decimal | `+0x150` | `000141B0` |
| `objLightFactor` | decimal | `+0x154` | `000141B0` |
| `tileLightFactor` | decimal | `+0x158` | `000141B0` |
| `soundCompensation` | on/off | `+0x15C` | `000141B0` |
| `vsyncInterval` | integer | `+0x160` | `000141B0` |
| `precachemusic` | text | `+0x168` | `000141B0` |
| `funcTimer` | on/off | `+0x171` | `000141B0` |
| `funcTimerFrames` | integer | `+0x174` | `000141B0` |
