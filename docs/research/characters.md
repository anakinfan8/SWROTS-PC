# Characters: classes, costumes, texture sets and bodies

How the game decides what the player character is and looks like, and what the port changes so
that any story level can be played as any character ([`player` and the Characters
tab](../debug-menu.md#playing-as-another-character)). Addresses are the retail Xbox executable's
(NTSC-U), as loaded. The code is `src/game/characters.cpp` (the player's class, costume, texture set
and body; HUD portraits), `src/game/resources.cpp` (loading from other levels, animation lookups),
`src/game/fixes.cpp` (the animation binding rebuild), `src/kernel/misc.cpp` (restarting a mission)
and `src/debug/console.cpp`, `src/debug/menu.cpp` (the commands and the tab).

## Four layers

| Layer | What it is | Where the game keeps it | Port command |
|---|---|---|---|
| Class | behaviour: moves, weapons, AI, health (`IAnakin`, `ICloneTrooper`) | the class registry, by name | `player <class>` |
| Costume | a named body for the class ("Mesh Choice") | the class's costume list; character +0x1E8 | `player <class> <costume>` |
| Texture set | alternative textures for the body ("Starting texture set") | the costume list's header; character +0x1EC | `skin <set>` |
| Body | the mesh (`meshes\chars\<folder>\<file>.msh`) and its animation binding (`.ban`) | the costume's record | `mesh <mesh>` |

## Creating the player

At a level start, 0x2AB660 creates the player through **0xB1F90** (`kSpawnPlayer`, cdecl
`(TString* class, int costume)`) with the launch settings' player class (+0xAC: the mission
list's or the versus select's class). The character factory creates it by class name (class
registry 0xE9B60, create vfunc +0x30). The new character is given its costume through its vfunc
+0x2C8 (0xB1D60); at **0xB1DE1** (`mov eax, [esp+0xC] / mov edx, [ecx] / push eax`) `ecx` is the
character and `[esp+0xC]` the costume index. -1 means the profile's costume (`[[0x68D944]+0x98]`),
an index into the *level's own* class's list.

The port hooks both: 0xB1F90 replaces the class name, and 0xB1DE1 calls `PlayerVariant(player,
costume)`, which chooses the costume, gives the player its own copy of the list when the body
changes, and sets the texture set. The chosen names live in the port's memory: every level change
and restart reboots the game image, and the game's strings go with it.

Versus fighters are created another way (0x27B7D0 from the duel's class table, then spawn 0xA2DE0):
there 0xB1F90 swaps the class, but 0xB1DE1 is not reached, so costumes, texture sets and bodies do
not apply in Versus yet.

## Costume lists

Static data in `.data`, one per class, found from a character at **+0x1E0** (set by the class's
constructor, e.g. 0x29DD26 for Anakin). The game's own lists are shared by every character of the
class; the port gives the player a copy when it changes the body, so NPCs of the class keep theirs.

```c
struct CostumeList {                 // 0x2C bytes, then the records
    const char* name;                // the class without its "I": "Anakin", "CloneTrooper"
    uint32_t    numbers[4];          // small numbers, meaning unknown (0x45, 0x0A, 0x45, 0x0A for most)
    const char* textureSets[6];      // suffixes, null-terminated: "_var01", "_var02"
    Costume     costumes[];          // up to a record with a null name
};
struct Costume {                     // 20 bytes
    const char* name;                // "Anakin_Duel"
    const char* mesh;                // "AnakinDuel\\AnakinDuel", under meshes\chars, no extension
    uint32_t    zero;
    const char* code;                // three letters: "AKN", "CLT", "CLC"
    uint32_t    zero2;
};
```

The port finds the lists by scanning `.data` once for this shape: a name in `.rdata`, four numbers
below 0x10000 (the weapon, font and skeleton-joint tables built alike have pointers there), and a
first record whose name has no backslash and whose mesh has one. 40 lists match. A class's list is
the one whose name is the class name without its "I".

Some lists name costumes whose models are not on the disc (cut during development): `Sidious`,
`Emperor`, `Luke`, `CommanderCody` (`clonecomm`), `Obiwan_Dirty` and `Obiwan_NPC_Dirty`, `PadwMale`,
`Grievous_shell`, and the battle droid's plain `BattleDroid` (its levels use `hordeBattleDroid`). The
port never picks those: a class whose chosen or usual costume is missing gets the first one that is
on the disc (`VariantOnDisc`). A class with no body at all (a costume whose mesh is on the disc
with its binding) is refused: `ICommanderCody`, whose only costume is the cut `clonecomm` and of
whom the disc keeps only the HUD portrait and versus bust (no definition, tables or animations; with
another body his state code faults at 0x616D1), and `IPoggle`, whose model is static.

Costumes cover the class's looks across the story, and also NPC copies of the same bodies
(`Anakin_NPC_Duel`); `player` matches a costume by number, name, or the shortest name containing
the text, so `duel` is `Anakin_Duel`.

## Texture sets

The list header's suffixes are the class's texture sets, which level designers pick for each
character they place: the instance property **"Starting texture set"** (registered by
`ICharacter::Prop_Serialize`, 0x15B4F3: "(Default)" and the suffixes), stored at character
**+0x1EC** (0 the plain textures, 1 the first suffix). When the body loads (0x155763, called from
`ICharacter::Init`), the suffix `[list + set*4 + 0x10]` is put in the global **0x6941A0**, and the
mesh's texture loader (**0x14F7E0**) first asks for `<folder>\<texture><suffix>` and uses it if the
level declares it, else the plain texture. Character **+0x1F4** is a suffix that overrides it (the
versus `_duel` look of a fighter against itself), and 0x15C670 / 0x15C730 are editor helpers that
step or reset the set.

| List | Sets | `_var01` textures on the disc (levels) |
|---|---|---|
| CloneTrooper | `_var01`, `_var02` | `hordetrooper_var01` (the 501st: m09a, m13, u109, u113), `clonesniper_var01` (m13) |
| BlazeTrooper | `_var01`, `_var02` | `clonesky_var01` (m13, u113) |
| Stormtrooper | `_var01`, `_var02` | none |
| BattleDroid | `_var01` | `hordebattledroid_var01` (m01, m05, m06a, m12b, u105, u107, u112) |
| GrappleDroid | `_var01` | `grappledroid_var01` (m06a, m06b, u106) |
| BattleDroidFly, BuzzDroid, CloneWalker, CommanderCody | `_var01` | none |

No `_var02` texture is on the disc. The clone trooper's plain textures are the 212th's orange
(Utapau); the 501st's blue is `_var01`, placed only in the Coruscant temple levels.

For a set, the port sets +0x1EC on the player and declares the set's textures in the running level
from wherever the disc has them (every `*<suffix>.stx` in the body's folder), so the 501st look works
on Utapau too.

## Bodies and animation bindings

A body is a mesh plus its **animation binding** (`<mesh>.ban`), which maps the skeleton to the
animations the character uses. The disc has 75 meshes under `meshes\chars\`; the 48 shipped with a
binding are character bodies. The other 27 are dismemberment limbs (`*_limbs`), debris
(`*_chunks`), vehicles (gunship, Neimoidian shuttle), droid-throw pieces, Poggle's static model and
`skeleton\skeleton_lightningfx` (the lightning effect). Loaded as a body, these fault in the mesh
setup (0x611B4, from `TGMCharMesh` vfunc +0x28) and the game then loops in its own error handler,
writing `Message.log` without end. The port offers and accepts only bodies: disc meshes with a
binding, and loose meshes under `mods\` (the engine builds their binding).

When the body changes, the port gives the player its own copy of the costume list with the body's
mesh in the costume's record. A body on another class needs a binding for that class's animations,
which the engine rebuilds; three things in that path had to be fixed:

1. **The lookup's found list is compact.** The binding rebuild (0x68670 validates a binding,
   0x68804 rebuilds) asks the animation lookup (0x65C50, vtable slot 0 of `[[0x66F7A4]+0x28]`; the
   call returns to **0x6890D**) for every animation the class uses. The lookup leaves animations it
   cannot find out of its found list, but the rebuild reads one entry per name. A few animations exist
   in no PAK (`Anakin_Frc_Jump_C1`, `_C2`, cut from Anakin's force jump), so the rebuild read past the
   list. For that caller only, the port lays the found list out again in the names' order (engine
   array resize 0x21B70), each missing name taking its nearest neighbour's animation (the names are
   sorted, so a close relative).
2. **A failed rebuild threw the binding away.** After reporting missing animations ("BAN file creation
   yielded missing anim errors"), the rebuild freed the binding and returned none (from **0x689F8**),
   and the game crashed using it (0x68F37). The port continues on the success path (**0x68A33**).
3. **A binding of the wrong size could be accepted.** The validator compares the binding's count with
   the request, but when the packer says so (its vfunc +0xF4, at `[esp+0x13]`) a different count goes
   on to a name-by-name comparison that reads past a shorter binding, which can then pass and be filled
   past its end (0x68F71). At **0x68703** a different count now always rebuilds.

The engine writes a rebuilt binding next to the mesh on the disc drive; the port redirects it to
`cache\disc\` (`src/kernel/file.cpp`) and, when the engine reads it straight back, serves that copy
before any other PAK's (the manual-file hook in `resources.cpp`); it used to get the original
character's binding again.

A loose mesh under a name no PAK has (a modder's own body) is declared to the level from `mods\`
(`DeclareFromDisc`), and the mesh lists include `mods\meshes\chars\`.

## Saber colours

A saber's colour is a key (saber +0x814, float r, g, b) set with its vfunc **+0x604** (`SetColor`,
thiscall `(const float rgb[3])`, `ret 4`: 0x286180 for the single-bladed sabers, vtables 0x5B1FF0,
0x5B2620, 0x5B2C90, 0x5B32F8; 0x2864F0 for the double-bladed, 0x5B3958). The effect (0x2E1600)
draws the pure keys (1,0,0), (0,1,0), (0,0,1), (1,0,1) with the colours of the `sabercolor` table
(0x6512F4 blue, 0x651300 green, 0x65130C red, 0x651318 purple; the command is 0x2F4C50), any other
key as it is; that is why `sabercolor` changes every saber of a colour.

A character equipping a saber (0x280480, at 0x28054E) gives it `[character + 0xF20]` if set, else
its own default; +0xF20 is a `const float*` override that IJedi's constructor clears and Versus sets
per fighter (IGameManager::Init 0x27B872, from 0x650C98). The power-up switch (saber vfunc +0x614,
0x286B90; called by the character update 0x281230 when a power-up starts and ends) brightens the key
(blue (0,0,1) to (0.3, 0.5, 1)), which is not a pure key and so is drawn raw, the original colour
whatever `sabercolor` says; but it does nothing when the owner's +0xF20 is set.

The port's `saber` points the player's +0xF20 at its own colour (at creation, before the equip, and
live), and recolours the player's sabers in its four weapon slots (character +0x1080, 0x20 apart) with
`SetColor`. Only characters that are IJedi (IsA, vfunc +4, with the type key 0x249950, as the power-up
switch asks) have the field. Cutscene events and scripts can still set a saber's colour.

## Spawning characters

The game spawns characters at run time in three places, all alike: the AI respawner (AI.cpp, 0x19C1E0),
the versus fighters (0x27B7D0) and a developer item spawner with no callers (TGVaderInterface,
0x2DF600, which builds a transform in front of the player). The recipe:

1. The class's object: the class registry's lookup **0xE9B60** (cdecl `(const char* class)` -> class
   info), then **0xE2350** (cdecl `(class info)` -> object; class info vfunc +0x30). Every class is
   registered at startup, so any class can be created in any level.
2. `object+0xC &= ~0x08000000` (created objects are marked inactive), the costume at +0x1E8 (and the
   texture set at +0x1EC).
3. **0xA2DE0** (cdecl `bool (object, owner or null, const float transform[16], 1)`): places it (object
   vfunc +0x1F4), pushes it on the spawn stack (0x68D9A8, count 0x68DA38) and adds it to the level's
   instance manager (`[[0x68D944]+0x58]`: 0xB6540 unowned, 0xB67B0 owned), which initializes it
   (`ICharacter::Init`, loading what the level lacks through the port's resource hooks). Logged by
   the game as "SpawnEnd %s (unowned)".
4. The object's vfunc **+0xB4** (thiscall, no arguments; as the item spawner at 0x2DF849 and the player's
   creation do): without it the character exists but is not seen.

A character's transform is at **+0x150**: 4 rows of 4 floats (right, up, forward, position; the copy
0x1A7970 takes 3 of each). The port places a spawn 120 units in front of the player, turned round.

Loading a character once the level has loaded needed two fixes in the port's manual-file handling: the
packer is then no longer streaming (+0x1D0 is 0) and its stream reader is freed while +4 still points
at it, so (a) a manual file (a body's `.ban`) the disc does not have as a file is served from a PAK copy
instead of being opened on the disc, and (b) the packer's "put the stream back" after a manual read
(0x51E40, called by the scene at 0x4B0D5) returns at once when not streaming.

**Shared bindings.** A body's animation binding (`<mesh>.ban`) is one resource per mesh, shared by every
character wearing it; it is rebuilt for a class whose animations it lacks. Two classes in one body in
the same level therefore fight over it, and the one rebuilt over crashes in its animation (0x663C5).
The port refuses spawns that would do this; a spawn takes no other body.

## Changing the player live (not done)

- *The body in place*: re-running the body load's mesh part on the live player (costume vfunc +0x2C8,
  the mesh holder +0x388's load vfunc +0x70, post-mesh 0x152780) loads the mesh but the animation
  binding and state (+0x438, from `TLinkScript::Lock`) stay the old body's: the engine reports "No
  running animation has valid movement data" every frame and the character spins.
- *Replacing the player*: a new character spawned at the player's transform can take over. The
  player that some 400 call sites ask for (0xA30F0) is the primary character record's +0xC (record
  `[0x68DCEC]`, 0xB1C60), controller 0 is bound with 0x150580 (thiscall `(int id)`, rebinding the
  input manager's slot through 0x8ADD0), +0x390 = 2 marks the player-controlled character, and
  0x68EF70 points at the player's transform. With all of those moved over, the master camera still
  follows the old player: it keeps its own target in its camera-mode objects (TCCMode / TCCControl,
  around 0x12B7A0), not yet found. The old player can be marked inactive (+0xC 0x08000000) but keeps
  being simulated and drawn; no instance removal function was found (0xB63F0 initializes and
  registers, 0xB4CC0 finds by id).

## Health and Force

A character's health is a float at **+0x130** and its maximum at **+0x134** (1000 for Anakin, 50 for a
clone trooper); Force power at **+0xA40** and its maximum at **+0xA44**, on the Jedi-like characters.
The game's `health`, `maxhealth` and `power` variables only set them, on the player (0x150420,
0x150450, 0x150480, through 0xA3130); read back they give what was last set, not the player's values,
and so do `forcelevel`, `combatskilllevel` and `forcepowerlevel`. `god` (options +0xC9) covers health
only; the port's infinite Force keeps +0xA40 at +0xA44 every frame.

## HUD portraits

The game manager (`[0x7EB964]`) keeps the level's character portraits in an array at +0x260
`{capacity, count, data}` of `{face, head, name}`, filled at the level start for the characters the
level expects (0x27BE30, add 0x1F8A00). The HUD finds the player's face by name (0x27B740) from the
table of twelve portraits per class (0x650C30; heads 0x34 bytes on). The port loads a portrait from
the table that the level did not expect, through the texture manager (`[[0x645F7C]+0x38]+0x4C`,
vfunc +4). Classes without a portrait (the clone trooper) show none.

## Restarting the mission

A change of character applies when the player is created, so the port restarts the running mission:
an in-process reboot with the launch data the running game image started with, as the game's own
Restart Mission does (`kernel::RestartMission`). The launch data page itself is no use: the game
rewrites it while it runs, and from the menus it no longer names the mission. The port keeps a copy
from every boot (`BootInit`). A mission booted straight from `mods\Default_Xbox.cfg` has no launch
data; its restart boots the same config again.

If a game thread does not stop within 4 seconds (a game stuck in its error handler), the reboot falls
back to starting the game process again; the character choice is handed to the new process as
`SWROTS_PLAYER` (`kernel::SetBeforeRelaunch`), which it reads at its start.

## Open questions

- **Changing the character live**, without a restart: the body is loaded once in `ICharacter::Init`
  (0x155710 onwards), and nothing in the retail build reloads it. Replacing the player in place (a
  new character at the old one's position, with its health and state) is the likely route.
- **Versus**: costumes, texture sets and bodies for fighters need the fighter creation path
  (0x27B7D0 / 0xA2DE0) hooked like 0xB1DE1.
- The four numbers in a costume list's header.
- Characters a class's moves were not made for (droids with a Jedi's moves) stretch or twist; there
  is no retargeting.
