# SWROTS-PC

An unofficial, native Windows port of **Star Wars: Episode III - Revenge of the Sith** for the
original Xbox. Not an emulator: the game's own code runs directly on your PC, and the parts of the
Xbox it relied on (its kernel, Direct3D, audio and controllers) are replaced by native Windows code.

You need your own copy of the game (the North American Xbox release). This project contains no
game code, art, audio or other game data.

> **Status: early development.** The game boots, plays its movies and menus, and is playable
> through level changes, restarts and saving. It has not been played through from start to finish
> yet. Expect bugs; [reports](#reporting-problems) are very welcome.

What's new in each release: [changelog](CHANGELOG.md).

## Features

- Runs natively as a 32-bit Windows program: no emulation layer.
- Any window size or fullscreen, 16:9 widescreen, higher internal resolution, anisotropic
  filtering, a sharp picture on high-DPI displays.
- Keyboard and mouse, Xbox controllers, and PlayStation controllers (DualShock 4, DualSense) with rumble.
- Level changes and mission restarts happen in the same window, like on the console.
- Mods as loose files: replace textures, models and other game files without rebuilding archives.
- A developer menu with the game's own debug console and its hidden debug displays.

## Quick start

1. Download the latest release and unpack `swrots.exe` and `core.dll` into a folder of your
   choice, e.g. `C:\Games\SWROTS`.
2. Run `swrots.exe`. The first time, it asks for your disc image: an Xbox `.iso` / `.xiso`, or a
   `.7z` / `.zip` / `.rar` containing one. It checks that it is the supported release, copies the
   game files into `GameData\` (about 2.5 GB) and adds a Start menu shortcut.
3. Play. Settings are in `settings.ini`, key bindings in `controls.ini`, both next to `swrots.exe`.

More: [installing](docs/install.md).

## Requirements

- 64-bit Windows 10 (1803) or newer. (The port itself is a 32-bit program, but it needs the address
  space a 64-bit Windows gives 32-bit programs.)
- A Direct3D 9 capable graphics card (anything from the last 15 years).
- Your own disc image of *Star Wars: Episode III - Revenge of the Sith* for the Xbox, North American
  release (the game's executable must have MD5 `6460ef37862de3af364a0563a0a18c4e`).

## Controls

| | Keyboard and mouse | Controller |
|---|---|---|
| Move (hold Left Ctrl to walk) | W A S D | Left stick |
| Fast / strong / critical attack | Left / right / middle mouse, E | X / Y / B (Square / Triangle / Circle) |
| Jump | Space | A (Cross) |
| Block | Shift | Left trigger |
| Force push / grip, saber throw, stun / lightning | F, Q, R | Right trigger, White, Black (L1, R1) |
| Deflect bolts, Force targeting | Hold Shift, move the mouse in circles | Hold block, circle the right stick |
| Pause / back | Escape / Tab | Start / Back |

Keys can be rebound, and the mouse sensitivity and walk speed changed, in `controls.ini`. Details
and controller layouts: [controls](docs/controls.md).

## Documentation

| | |
|---|---|
| [Installing](docs/install.md) | Setup, the folder layout, reinstalling |
| [Settings](docs/settings.md) | Window, graphics, frame rate, developer options |
| [Controls](docs/controls.md) | Keyboard, mouse and controllers |
| [Debug menu](docs/debug-menu.md) | The game's console, variables and debug displays |
| [Modding](docs/modding/README.md) | Loose-file mods, dumping assets, textures, character swaps |
| [How it works](docs/architecture.md) | The port's design, for the curious and for contributors |
| [Changelog](CHANGELOG.md) | What changed in each release |
| [All documentation](docs/README.md) | Including research notes on the game's engine |

## Known issues

- On-screen button prompts and some messages still refer to the Xbox and its controller.
- 60 fps (`FpsLimit=60`) is experimental: some of the game's logic is timed for 30 fps.
- The keyboard and the first controller are both player 1; two-player modes need two controllers.
- Controllers other than Xbox (XInput) and PlayStation ones need Steam Input or a similar translator.
- Some modding limits: animations stored in a level's memory image (most `.bnm`, `.ban`) cannot be
  replaced yet, and loose text resources in folders the disc also has (e.g. `gameinfo\`) fail to load.
- Only the North American release is supported.

See the [issue tracker](../../issues) for the current list.

## Reporting problems

Open an [issue](../../issues/new/choose) and attach `logs\swrots.log` (next to `swrots.exe`). If the
game crashed and you have started it again since, attach `logs\swrots.previous.log` instead: it keeps
the run before. Say what you were doing, which level, and whether it happens every time.

## Roadmap

- PC button prompts and wording (keyboard, mouse and PlayStation icons).
- Proper 60 fps and higher (the engine has fixed-step simulation to build on).
- An in-game settings menu, keyboard as its own player for versus modes, more controllers.
- Modding tools: a character swap tool, dumping and replacing animations, adding content to levels.
- A full playthrough on many machines, and a stable 1.0.

## Building from source

Requires Visual Studio 2022 or newer with the C++ desktop workload, and CMake 3.20 or newer.
The port must be built as 32-bit (the game's code is 32-bit):

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The result is `build\bin\swrots.exe` and `core.dll`. Run it from there; it will ask for your disc
image as described above. See [CONTRIBUTING.md](CONTRIBUTING.md) for the development setup.

## Contributing

Contributions are welcome: code, testing on different hardware, documentation, research into the
game's engine, and mods and modding tools. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; in short,
never add the game's files or code to the repository, and base findings on the game's own executable.

## Legal

SWROTS-PC is free software under the [GNU General Public License v3.0 or later](LICENSE). It includes
code adapted from Cxbx-Reloaded (GPL-2.0-or-later) and uses Dear ImGui (MIT); see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This is an unofficial fan project. It is not affiliated with, endorsed or sponsored by Lucasfilm,
Disney, LucasArts or The Collective. *Star Wars* and all related names are trademarks of Lucasfilm Ltd.
The game itself is not included and is not distributed by this project; you must own a copy.

## Credits

- **LucasArts** and **The Collective**, who made the game and its engine, *Slayer*.
- **Cxbx-Reloaded** and its contributors, whose research into the Xbox and its libraries this port
  builds on, and whose shader translation it adapts.
- **Dear ImGui** by Omar Cornut and contributors, for the debug menu.
