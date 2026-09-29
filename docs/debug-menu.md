# Debug menu and console

A developer overlay for testing and modding. Enable it in `settings.ini`:

```ini
[Debug]
DebugMenu=1
MenuKey=~         ; the key that opens it: ~ (default), F1-F24, Insert, Home, a letter...
DebugDisplays=1   ; lets the engine draw its fps counter, profiler and memory display
```

Press **~** (the key left of 1) in game to open or close it. While it is open the game gets no keyboard or mouse input
and the cursor is free; click into the game after closing it to take the mouse back.

## Console tab

The game's own console, whose window the Xbox release left out. Its colours are those of the
engine's debug windows in Indiana Jones and the Emperor's Tomb and Marc Ecko's Getting Up (same engine). Type a command and press Enter;
Up/Down recall earlier commands.

| Command | What it does |
|---|---|
| `help` | lists these commands and every command the game registered |
| `listvars [text]` | lists every variable (name, type, current value), or those whose name contains `text` |
| `get <variable>` | shows one variable |
| `set <variable> <value>` | changes a variable, e.g. `set timeScale 0.5`, `set god true` |
| `<variable>=<value>` | the same, in the `vars_xbox.cfg` form |
| `toggle <variable>` | flips an on/off variable |
| `clear` | empties the console (also the Clear button) |
| anything else | runs through the game's console: the game's own commands |

Variables take effect immediately. To set them at every start, put them in `mods\vars_xbox.cfg`
(`name=value`, one per line). The full list with descriptions is in
[engine variables](research/engine-variables.md).

Some of the game's commands:

- `sabercolor <blue|green|red|purple> <r> <g> <b>` redefines a blade colour (0-255 each), e.g.
  `sabercolor blue 255 128 0` makes blue blades orange.
- `setbind`, `addbind`: controller bindings, as in `binds_xbox.cfg`.

Not everything works: the Xbox release removed a lot of the developer-only code, so some variables
(debug drawing such as `saberdebug`) and commands exist but do nothing.

## Switches tab

Checkboxes for the debug displays, the fps counter, god mode and AI.

## How it works (for contributors)

`src/debug/console.cpp`, `src/debug/menu.cpp`; addresses in `src/game/game.h`.

- The engine console object (console.cpp in the game, vtable 0x55FF58) is created at startup and kept at
  `[[0x66F7A4]+0x48]`. Its `Execute(line, echo)` (slot +0x04) splits the line, looks the first word up
  in the command table and runs the command's handler.
- The Xbox build keeps the command handling but stubs out the rest:
  - The print methods (+0x50 engine string, +0x54 C string) do nothing; the port points them at its log.
  - The global console pointer `0x65E360` that the commands print through is never set; the port sets it.
  - `help` and `listvars` enumerate through stubbed table methods, and `set` calls a stubbed registry
    method (+0x08). The port implements these itself from the registry's working parts: find (+0x04),
    set (+0x1C, as `vars_xbox.cfg` uses) and the variable objects' Get / TypeName methods.
- Console tables (variables at console +0x1C, commands at +0x2B4) are hash tables of 37 buckets of
  `{value, name, next}` nodes.
- Commands are queued by the menu and run on the game thread after a frame is presented.
- New port commands go in `RunPortCommand` (`src/debug/console.cpp`).
