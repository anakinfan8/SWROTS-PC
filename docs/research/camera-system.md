# The camera system

How the game's camera reaches the screen, what the developers left of their own camera tools, and
how the port's free camera (`freecam` in the [debug console](../debug-menu.md#free-camera)) plugs in.
It is the groundwork for a photo mode. Addresses are the retail Xbox executable's (NTSC-U), as
loaded; the port's code is `src/game/freecam.cpp`.

## The master camera

One object drives the view: the master camera, `IMasterCamera` (engine, `IMasterCamera.cpp`) and the
game's `IMasterCameraVader` (`IMasterCameraVader.cpp`), vtables 0x582488 and 0x5B4DC0. Gameplay, duel
and scripted cameras feed it.

- Its per-frame update is 0x129D40 (vtable slot 64). It works out the camera's placement and hands it
  over through **SetTransform**, 0x129990 (vtable +0x1F4, thiscall `(const Matrix*)`), which only the
  two master camera classes use. SetTransform passes it on to the generic object setter (0xF2550).
- The matrix is 4x4, row vectors: rows 0-2 are the camera's right, up and forward axes, row 3 its
  position. The world's up is +Y, and right x up = forward, as in Direct3D. A character is roughly
  180 units tall.
- It keeps running when the game is frozen with `timeScale 0`, so a camera hooked here still moves.

## Left-over developer tools

- **`debugCamera`** (an on/off variable, options object +0x173) is still checked by the master
  camera's update (0x12A188): when on, the camera stays at a stored position (camera +0x180) and keeps
  aiming at its target, the player. Nothing moves that position in the retail build, so it is not a
  free camera: turned on, the view freezes in place and follows the player around.
- **`noCheat`**, described as "Disables tab-fly cheat" (options +0xCE), is registered, but no code
  reads it: the fly cheat itself was removed from the retail build.
- The same engine in Indiana Jones and the Emperor's Tomb names a developer free camera
  (`TTelemetry::m_bFreeCamera`) and a "Freeze Camera" switch in its
  [symbol maps](indiana-jones.md).

## From the master camera to the screen

The renderer does not read the master camera. Each frame the scene's render (0x85FD0) hands a camera
to the engine API (`GameAPIMain.cpp`, vtable 0x5613C0 slot 40, 0x18510), which passes it to the
render interface:

- **The camera** (render interface vtable 0x59CA48, 0x2116A0; thiscall `(int kind, const Matrix*)`):
  kind 0, once per render pass, with a **view matrix**, the inverse of the camera's placement.
  - A frame has three or four passes. Most share the main view; some scenes add a far background
    pass with a camera of its own at another scale (thousands of units away).
  - The view is the master camera's placement taken into the level's world: view = X * inverse(master).
    X is the level's transform; a duel arena's world is mirrored and offset from the master
    camera's, so the two matrices do not look alike.
- **The field of view** (render interface vtable 0x59CA60, 0x211C30; thiscall `(float radians)`),
  once a frame. In a duel it moves between about 0.8 and 1.3 as the fight's camera zooms.
- The engine variable `fov` ("Current camera FOV", options +0xEC) is set by Camera Control objects in
  levels (0x12BAF0) and by 0x1D1F55 (in the cinematics code, by its neighbours), not by the fight's
  zoom.

The port's Direct3D layer gets the view as transform state 0 (0x2100A0) and the projection as state
1 (0x210030), for fixed-function draws; shaded draws use matrices the engine combines itself (at
0x78B460), so a camera change has to be made before this point to reach every draw.

## Camera effects

Two effects are added between the master camera and the renderer:

- **Shake**: a small position offset of the view, about one or two units, when a fighter is hit or
  lands a heavy blow. `IScreenShake` (`IScreenShake.cpp`, update 0x12AF40) hands its shake to the
  instance manager as "ScreenShake" (0x12B069), but skipping that call does not stop the jitter seen
  in duels: camera components have shake properties of their own ("Shake displacement X/Y/Z", "Shake
  duration").
- **Zoom**: the field of view, which the fight's camera widens and narrows.

## The free camera

`freecam` (`src/game/freecam.cpp`):

1. Replaces the matrix handed to the master camera's SetTransform with a flown one, starting from the
   game camera's placement. The game builds its own view from it, as it would from its own camera.
2. Before a flight, measures X on each frame's main pass (the first). While flying, each pass whose
   view the flown camera reproduces (the same rotation, a position within a shake's reach) gets the
   flown position; that removes the shake. Other passes (the far background) keep the game's view,
   which follows the flown camera by itself.
3. Holds the field of view at its value from before the flight, which removes the zoom.
4. Holds player 1's input back from the game (`src/input/input.cpp`) and reads the keyboard, mouse
   and first controller itself.

Outside a flight every hook passes the game's values through unchanged; the game shakes and zooms
as always. A level change or restart turns the free camera off.

Mistakes worth avoiding, all tried:

- Treating the renderer's matrix as the camera's placement (it is the inverse): a flown camera then
  turns the wrong way as soon as it rotates.
- Numbering passes by call order and keeping a measurement per pass: the far background pass comes
  and goes, so the numbers shift and a pass gets another's camera (a black screen, a stray sky).
- Holding one field of view for every call, and replacing every pass with the flown camera.

## Other useful variables

- `hud` (HUD alpha): `set hud 0` hides the HUD, not the on-screen tips.
- `timeScale`: 0 freezes the game; the master camera keeps updating.
- Versus launch settings: `v_versusMode`, `v_arena`, `v_roundcount`, `v_roundsp1` (a rounds setting
  the menus do not offer; not tried yet), `v_p1`-`v_p4` (numbers, not characters).

## For a photo mode

Photo mode can reuse all of this: freeze time (`timeScale 0`), hide the HUD (`hud 0`, plus the tips),
fly the free camera, and change the held field of view instead of just keeping it. Its menu is meant
to use the game's own UI (the Settings menu's controls), which the port can add to as menus load (see
[the versus roster](versus-roster.md) for how a menu is edited).
