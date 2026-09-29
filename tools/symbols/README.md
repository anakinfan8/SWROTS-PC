# Symbol tools

Builds the project's reconstructed symbol tables (our own `.map` for the game) from the player's own
files. Output goes to `build/bin/symbols/` and is never committed: it is derived from LucasArts' binaries.

Requires Python 3 with `capstone` (disassembly). C++ names are demangled with Windows' `dbghelp`.

## Trust levels

1. Official, from the game's own XBE: class names returned by `GetTypeName` (vtable slot 3),
   `Class::Method` log strings, source-file paths, XDK library names, config keys.
2. Official names strongly matched from Indiana Jones and the Emperor's Tomb (same engine and developer):
   vtables aligned slot by slot, several distinctive shared strings, identical code.
3. Official names weakly matched (call-graph neighbours, rough code similarity). Tentative.
4. Hints only (your own earlier Ghidra/ReClass work, if you have any): compared against the results, never used to
   name anything.

## Stage 1: Indiana Jones 1.01 map

    python tools/symbols/build_symbols.py indy [--indy "<install folder>"]

Indiana Jones ships `.map` files from its 1.0 release, but Steam only ever had the 1.01 patch (one
manifest for depot 560431). `realign.py` walks each map in order, tracking the piecewise-constant 1.0 -> 1.01
address shift (reset at fixed points: `X::GetTypeName` where exactly one function returns `"X"`), and
checks every placement against the 1.01 binary. `indy.exe` is encrypted by Steam's DRM and skipped (the
engine is in the DLLs). Output: `build/bin/symbols/indy/<binary>.1.01.map`, `*.contradicted.txt`, `report.md`.

## Stages 2-4: swrots.map

    python tools/symbols/build_symbols.py game [--ghidra-hints <tsv>] [--reclass-hints <file.reclass>]

- Stage 2 (`swrots.py`): the game's own facts -- XDK library names, class vtables found through their
  slot-3 `GetTypeName` (which returns the class name), `Class::Method` texts used by exactly one function,
  and source-file paths (`f:\srcvader\...`, `..\Slayer\...`).
- Stage 3 (`indy_match.py`): Indy names carried over -- vtables pinned at the name getter and compared slot by
  slot (the Xbox code is ~25% smaller, so similarity is by size and call count), shared distinctive strings
  (mutual best pairs), identical code, call-graph neighbours, and overrides (a class-specific function in a
  slot whose method is known elsewhere in the same hierarchy).
- Stage 4 (`hints.py`): earlier hand research compared against the result; never used to name anything.
  Export Ghidra names with `ghidra/ExportUserSymbols.java` (headless, read-only; run on a copy).

Output: `build/bin/symbols/swrots.map` (address, kind, level, name, size, source file, evidence) and
`report.md`. The port reads `symbols\swrots.map` next to the exe when present, so crash logs and thread dumps
show names. `ghidra/ApplySwrotsSymbols.java` imports it into a Ghidra project, renaming only functions that
still have default names.

## Files

- `binary.py` -- XBE/PE loading, function discovery, per-function facts (strings, calls, fingerprints).
- `mapfile.py` -- MSVC `.map` parsing and demangling.
- `realign.py` -- stage 1.
- `swrots.py`, `indy_match.py`, `hints.py` -- stages 2, 3, 4.
- `ghidra/` -- export (hints) and import (results) scripts.
- `build_symbols.py` -- command line.
