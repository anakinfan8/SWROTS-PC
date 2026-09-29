#pragma once
// PlayStation controllers (DualShock 4, DualSense, DualSense Edge) over USB or
// Bluetooth, read directly through Windows HID -- no drivers or extra software.

#include <windows.h>

#include <cstdint>

namespace swrots::input {

// A controller's state in Xbox terms.
struct PadState {
    WORD buttons = 0;     // XB_DPAD_*, XB_START, XB_BACK, XB_LEFT_THUMB, XB_RIGHT_THUMB
    BYTE analog[8] = {};  // A, B, X, Y, Black, White, left trigger, right trigger
    SHORT lx = 0, ly = 0, rx = 0, ry = 0;
};

// Starts watching for controllers (they may be plugged in at any time).
void StartPlayStationPads();
// Number of connected PlayStation controllers.
int PlayStationPadCount();
// State of the n-th connected controller; false if there is none.
bool ReadPlayStationPad(int n, PadState& state);
// Rumble (0-65535 per motor).
void SetPlayStationRumble(int n, WORD left, WORD right);

} // namespace swrots::input
