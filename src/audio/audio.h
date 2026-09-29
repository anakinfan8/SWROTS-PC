#pragma once

namespace swrots::audio {

// Points DirectSound library globals at runtime-owned data (call once the game
// image is mapped).
void InitXboxGlobals();

// Stops all sound output (used when the game restarts itself).
void Silence();

// The game rebooted in-process: destroys every buffer and stream it created (they point
// into its memory) and forgets its pending stream callbacks.
void ResetForReboot();

} // namespace swrots::audio
