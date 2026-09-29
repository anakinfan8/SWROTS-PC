#pragma once
// First-run setup: fills GameData\ from the player's own disc image.
//
// Accepts an Xbox disc image (.iso: an XISO, or a full Redump-style image) or
// an archive containing one (.7z/.zip/.rar, unpacked with Windows' built-in
// tar.exe). The game files are extracted to GameData\, the executable is
// checked against the supported release, and the game's icon and optional
// shortcuts are created.

#include <string>

namespace swrots {

// Runs setup if `gameData` has no default.xbe. `imagePath` preselects the disc
// image (from --install); empty asks the player. Returns false if the player
// cancelled or setup failed (after telling them why).
bool EnsureGameData(const std::wstring& exeDir, const std::wstring& gameData, const std::wstring& imagePath);

} // namespace swrots
