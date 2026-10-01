# Changelog

What changed in each release of SWROTS-PC, newest first. Downloads are on the
[releases page](https://github.com/jedijosh920/SWROTS-PC/releases).

## [v0.2.1](https://github.com/jedijosh920/SWROTS-PC/releases/tag/v0.2.1) - not released yet

A fix for crashes after a level change on some graphics drivers.

### Fixed

- **Crash after a level change, a new versus match or a return to the menu** on some graphics
  drivers, e.g. Windows in Parallels Desktop on a Mac ("A critical graphics error has occurred")
  ([#2](https://github.com/jedijosh920/SWROTS-PC/issues/2),
  [#3](https://github.com/jedijosh920/SWROTS-PC/issues/3)). The game restarts itself for every level,
  creating its graphics device again; two caches of the port's (vertex layouts, used for movies and
  screen-space drawing) were never released, so they kept the old device alive and were handed to
  the new one. Most drivers tolerate that; stricter ones end the game. They are now released with
  their device.

### Added

- The log warns when a released graphics device is still in use (`Graphics device still referenced`).
- For contributors: `SWROTS_REBOOT_EVERY=<seconds>` restarts the game on a timer, to test restarts
  ([contributing](https://github.com/jedijosh920/SWROTS-PC/blob/main/CONTRIBUTING.md)).

## [v0.2.0](https://github.com/jedijosh920/SWROTS-PC/releases/tag/v0.2.0) - 2026-09-30

Yoda joins Versus.

### Added

- **Playable Yoda in Versus.** His own cell on the select screen, after Random, with the name,
  title and bust the disc kept for him (a fighter the game planned but never finished). He is
  always unlocked, Random can pick him, and he fights in every arena with Anakin's intro and win
  cameras (the disc has none of his own). No settings or debug options needed.
- **Sith Yoda.** In Yoda against Yoda, player 2 is a dark Yoda with a red saber, as the game's own
  fighters get a different look against themselves. His darker textures are made from the normal
  ones when the duel loads; Yoda looks as always everywhere else.
- **Yoda blocks a duelist's blows with his own animation.** He was made for fighting clones, and
  fell back to Anakin's blocks, which left him floating. Some of his other reactions can still
  look off.
- **Characters a level never had are loaded from the rest of the disc**: their model, textures,
  animations and tables come from whichever level has them. This is how Yoda fights in the versus
  arenas.
- **`unlockprofile`** in the debug console: the developers' own cheat, left out of the retail
  game, unlocks everything in the signed-in profile (story, fighters, arenas, bonus missions,
  concept art). The game saves it with the profile, so back up `saves\` first to keep your
  progress.
- Docs for developers: [the versus roster](https://github.com/jedijosh920/SWROTS-PC/blob/main/docs/research/versus-roster.md)
  (how Yoda was added, with addresses) and
  [adding versus fighters](https://github.com/jedijosh920/SWROTS-PC/blob/main/docs/modding/adding-versus-fighters.md).

### Known issues

- The menus show Yoda's normal bust for player 2 in Yoda against Yoda, and he has no full-body
  picture on the select screen (the disc has none).

## [v0.1.1](https://github.com/jedijosh920/SWROTS-PC/releases/tag/v0.1.1) - 2026-09-30

A fix for missing audio and frozen cutscenes.

### Fixed

- **No sound and frozen cutscenes after the first launch** on some systems
  ([#1](https://github.com/jedijosh920/SWROTS-PC/issues/1)). The game's audio needs a Windows
  component (COM) that the port relied on something else to set up. On the first launch the
  installer did, so sound worked; on later launches, on systems where nothing else did, audio
  failed to start (`XAudio2 initialisation failed (800401F0)` in the log). Cutscenes are timed by
  their sound, so they froze too, e.g. the opening "A long time ago..." of the first mission. The
  port now sets COM up itself before starting audio.
- **If audio cannot start at all** (no audio device, a broken driver), the game now plays on
  silently: sounds and movies keep time instead of freezing, and the log says
  `the game runs without sound`. Before, a half-started audio engine made every sound fail.
- A paused sound given a new play position no longer starts playing, as on the Xbox; a sound
  without a voice no longer risks a crash there.

### Added

- For contributors: `SWROTS_NO_AUDIO=1` runs the game as without an audio device, to test the
  silent path ([contributing](https://github.com/jedijosh920/SWROTS-PC/blob/main/CONTRIBUTING.md)).

## [v0.1.0](https://github.com/jedijosh920/SWROTS-PC/releases/tag/v0.1.0) - 2026-09-28

The first public release: the game boots, plays its movies and menus, and is playable through
level changes, restarts and saving.

### The port

- The game's own code runs natively as a 32-bit Windows program, with no emulation layer. The
  Xbox kernel, Direct3D (to Direct3D 9), audio (to XAudio2) and controller libraries are replaced
  by native Windows code.
- Level changes and mission restarts restart the game in the same window, as the console
  reboots into itself.
- First-run setup from your disc image (`.iso` / `.xiso`, or a `.7z` / `.zip` / `.rar` containing
  one): it checks that it is the supported North American release, copies the game files into
  `GameData\` and adds a Start menu shortcut (and optionally a desktop one).
- Saves in `saves\`, temporary files in `cache\`, a log in `logs\swrots.log` (the previous run's
  in `logs\swrots.previous.log`).

### Graphics

- Any window size, borderless fullscreen, optional VSync, and a picture scaled to fit the window
  at the right aspect ratio (or stretched, `Stretch=1`).
- 16:9 widescreen (the game's anamorphic mode) or 4:3.
- Higher internal resolution (`ResolutionScale`, automatic by default: 2x for 720p, 3x for 1080p
  and 1440p, 5x for 4K), anisotropic filtering up to 16x, the game's bloom on or off, sharp on
  high-DPI displays.
- 30 fps like the original; 60 fps experimental (`FpsLimit=60`).

### Input

- Keyboard and mouse, rebindable in `controls.ini`, with mouse sensitivity and walk speed.
- Xbox controllers (XInput) and PlayStation controllers (DualShock 4, DualSense) with rumble.

### Modding

- Mods as loose files in `mods\`: textures, models and other game files replace the packed
  copies without rebuilding the level archives; disc files (configs, movies, audio banks) too.
- Asset dumping (`DumpResources=1`) and resource logging (`LogResources=1`) to find the files a
  level loads and the paths a mod must use.
- Guides: [modding](https://github.com/jedijosh920/SWROTS-PC/blob/main/docs/modding/README.md), including a worked character swap.

### Developer tools

- A debug menu (`DebugMenu=1`, opened with ~): the game's own console, which the Xbox release had
  no window for, with its variables and commands, and switches for god mode, AI and the engine's
  hidden debug displays (fps counter, profiler, memory display).
- A flight recorder (the last 3 seconds of frames), Xbox library call tracing and file-open
  tracing, for bug reports and research.

### Known issues

- On-screen button prompts and some messages still refer to the Xbox and its controller.
- 60 fps is experimental: some of the game's logic is timed for 30 fps.
- The keyboard and the first controller are both player 1; two-player modes need two controllers.
- Only the North American release is supported.
