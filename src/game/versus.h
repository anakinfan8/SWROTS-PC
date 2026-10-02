#pragma once

namespace swrots::game {

// Versus mode (duels). At every boot, after the image is loaded: re-applies the select screen's slot
// changes (the duel starts after a reboot and reads the table again) and installs the duel camera
// fallback.
void InstallVersus();

// Puts a character class (e.g. "IYoda") in a versus select slot (0-8) until the game is closed.
// False when the game knows no such class.
bool SetDuelist(int slot, const char* className);

// The class in a slot, as the game currently has it.
const char* Duelist(int slot);

} // namespace swrots::game
