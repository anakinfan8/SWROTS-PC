#pragma once

namespace swrots::game {

// Guards against bugs in the game's own code (fixes.cpp). Each boot, after the image is loaded.
void InstallGameFixes();

} // namespace swrots::game
