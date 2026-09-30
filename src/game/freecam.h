#pragma once

namespace swrots::game {

// Free camera (the `freecam` console command). At every boot, after the image is loaded; every
// reboot (level change, restart) turns it off.
void InstallFreeCamera();

// Detaches the view from the game's camera (from the next frame, where the game camera was) and
// holds player 1's input back from the game, or gives both back.
void SetFreeCamera(bool on);
bool FreeCameraOn();

} // namespace swrots::game
