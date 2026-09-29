#pragma once
// Keyboard and mouse bindings, kept in controls.ini next to the executable.
// Each game action (named as in the game's manual) maps to one or more keys;
// the keys drive the Xbox controller inputs of player 1. Controllers keep
// their normal layout.

#include <windows.h>

#include <string>

namespace swrots::input {

// Xbox controller state produced by the keyboard (matches XINPUT_GAMEPAD's
// meaning, with the Xbox's analog buttons).
struct KeyboardPad {
    WORD buttons;            // D-pad, Start, Back, thumb clicks (Xbox bit layout)
    BYTE analog[8];          // A, B, X, Y, Black, White, left trigger, right trigger
    SHORT lx, ly, rx, ry;    // sticks
};

// Loads `path` (creating it with the default layout if missing).
void LoadControls(const std::wstring& path);
// Current keyboard/mouse state as controller input; empty when the game
// window is not focused.
KeyboardPad ReadKeyboardPad();
// Raw mouse motion while the game has the mouse (window thread): read as the right stick.
void AddMouseMotion(LONG dx, LONG dy);

// The game rebooted in-process: its controller ports are closed again.
void ResetPortsForReboot();

} // namespace swrots::input
