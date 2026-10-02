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
| `duelist [<slot> <class>]` | lists the versus select screen's fighters, or puts a character class in a slot (see below) |
| `player [<class>\|- [<costume>] [skin <set>] [mesh <mesh>\|off]\|off]` | plays as another character, costume or mesh (see below); `-` is each level's own class |
| `restart` | restarts the running mission (as the pause menu's Restart Mission does) |
| `autorestart [on\|off]` | whether a `player` change restarts the running mission at once (on by default) |
| `variants <class>` | lists a character class's costumes and texture sets |
| `meshes [text]` | lists the character meshes on the disc (those containing the text) |
| `unlockprofile` | the game's developer cheat: unlocks everything in the signed-in profile (story, fighters, arenas, bonus missions, concept art); the game saves it with the profile, so back up `saves\` first to keep your progress |
| `clear` | empties the console (also the Clear button) |
| anything else | runs through the game's console: the game's own commands |

Variables take effect immediately. To set them at every start, put them in `mods\vars_xbox.cfg`
(`name=value`, one per line). The full list with descriptions is in
[engine variables](research/engine-variables.md).

### Versus fighters

Yoda has his own cell on the versus select screen, after Random, with his name, title and bust; he
fights like any fighter (he is always unlocked, and Random can pick him), with Anakin's intro and
win cameras (the disc has none of his own). He was made for fighting clones and droids, so he has
no reactions of his own for most of what a duelist does to him and uses Anakin's; the port gives him
his own block reaction for a duelist's blows (Anakin's blocks left him floating), wherever he plays.
Some reactions can still look off. In Yoda against Yoda, player 2 is a dark "Sith" Yoda
with a red saber, as the game's own fighters have a different look against themselves; the port
makes his darker textures from the normal ones. Yoda is otherwise untouched. He has no full-body
picture on the disc, so the arena screen shows his bust (both players' are the normal bust). The
port loads him from the Jedi Temple level's PAK only for duels he is in.

Besides Yoda, the select screen has the game's nine fighters, slots 0-8: `IAnakin`, `IObiwan`, `IDooku`, `IGrievous`,
`IMace`, `ISerra`, `ICinDrallig`, `IVader`, `IOldObiwan`. `duelist <slot> <class>` puts another
character class in a slot until the game is closed. For example, `duelist 0 IYoda`, then choose
Anakin in Versus: Yoda fights instead. The arenas do not contain Yoda; the port loads him from the
Jedi Temple level's PAK (see [how loading works](modding/how-loading-works.md#what-the-port-changes)).
The select screen still shows the slot's original portrait, and a character without duel cameras
of its own (the intro and win shots) uses those of the character whose slot it took. `duelist` refuses class names the game
does not know. A character in another's slot uses the first of its variants whose model is on the
disc (e.g. the battle droid's `hordeBattleDroid`), for both players; characters without a combat
moveset may not work as fighters.

### Playing as another character

The **Characters tab** does all of this with lists: pick a class, a costume and a mesh, then Apply
(the tab runs the matching `player` command, shown under the buttons and in the console).

`player <class>` makes you that character class in every level until the game is closed; `player off`
goes back to each level's own character, and `player` alone shows the current choice. During a
mission a change restarts it with the new character at once, like the pause menu's Restart Mission
(from the mission's start, not the last checkpoint). `autorestart off` (or `[Debug] AutoRestart=0`
in `settings.ini`) keeps the mission running instead; the change then applies from the next level
start. For example, `player IVader` then restart
Order 66, or `player IYoda`, `player ICloneTrooper`, `player IBattleDroid` in Mustafar. Characters
the level does not contain are loaded from other levels' PAKs, like versus fighters. A character
whose usual costume is not on the disc gets the first of its variants that is (the battle droid's
`hordeBattleDroid`). The HUD shows the character's portrait when the game has one (its twelve:
Anakin, Obi-Wan, Dooku, Grievous, Mace, Serra, Cin Drallig, Vader, old Obi-Wan, Yoda, Cody and the
bodyguard), loaded from another level if need be; other classes, such as the clone trooper, have none.
Missions may expect their own character (cutscenes, scripted moments).

A costume follows the class: `player IAnakin duel` is Anakin without robe or hood (his Mustafar
outfit), also in Order 66, where he normally wears his cloak; `player ICloneTrooper horde` is the horde
trooper. `variants <class>` lists a class's costumes by number and name; a costume can be given by
either, or by a part of its name that no other has (the shortest name wins: `duel` is `Anakin_Duel`,
not `Anakin_NPC_Duel`). Some costumes the game lists were cut and their models are not on the disc
(marked in the list); those fall back to the usual costume.

Some classes also have **texture sets**, which the levels pick per character ("Starting texture
set"): the clone trooper's `_var01` is the 501st's blue markings of the Jedi Temple levels, where
the plain textures are the 212th's orange of Utapau. `player ICloneTrooper horde skin 1` (or
`skin var01`) is a 501st horde trooper in any level, `skin 0` the plain textures; `variants <class>`
lists a class's sets. A set's textures come from the levels that have them; a texture the set does
not change keeps its plain version.

`mesh <mesh>` dresses the player in any character mesh instead, keeping the class's moves:
`player IObiwan mesh anakinduel` is Obi-Wan in Anakin's duel outfit, and `player mesh obi` keeps each
level's own character but in Obi-Wan's mesh. `meshes` lists them (`meshes obi` those containing
"obi"); a mesh is named by its folder, its file or `folder\file`. A mesh made for one character has
its skeleton's animations bound for that character; for another, the game rebuilds that binding
(written to `cache\disc\`), and a few animations no character has (a cut part of Anakin's force jump)
borrow a neighbouring one. `player <class> mesh off` goes back to the costume's own mesh, and
`player - mesh <mesh>` keeps each level's own character in that mesh. A mesh of your own works too:
put it (with its textures) under `mods\meshes\chars\<folder>\` and name it `<folder>\<file>`, or just
`<folder>`; it appears in `meshes` and the Characters tab. Without its textures it is drawn plain
green, and a skeleton unlike the class's (a droid's mesh on a Jedi) may look broken.

Some of the game's commands:

- `sabercolor <blue|green|red|purple> <r> <g> <b>` redefines a blade colour (0-255 each), e.g.
  `sabercolor blue 255 128 0` makes blue blades orange.
- `setbind`, `addbind`: controller bindings, as in `binds_xbox.cfg`.

Not everything works: the Xbox release removed a lot of the developer-only code, so some variables
(debug drawing such as `saberdebug`) and commands exist but do nothing.

## Characters tab

Three lists: **Class** (the classes that have costumes; *All classes* shows every class the game
registers, most of them not characters), **Costume** (the picked class's costumes; greyed out ones
were cut and are not on the disc; below them its texture sets, if any) and **Mesh** (every character mesh on the disc), each with a
filter box. *(each level's own)*, *(the usual one)* and *(the costume's own)* keep the game's choice.
**Apply** runs the matching `player` command, **Back to normal** runs `player off`, **Restart
mission** restarts the running mission, and *Restart on apply* is `autorestart`.

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
