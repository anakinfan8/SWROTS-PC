#pragma once
// The game's official icon: the 128x128 dashboard emblem stored in the XBE's
// $$XTIMAGE section. It is read from the player's own game files at run time,
// so no game art is part of this project.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace swrots {

class XbeFile;

// The emblem as 128x128 BGRA pixels (top-down, straight alpha); empty if the XBE has none.
std::vector<uint32_t> DecodeTitleImage(const XbeFile& xbe, int& width, int& height);
// Sets the window's title bar and taskbar icons.
void SetWindowIconFromXbe(HWND window, const XbeFile& xbe);
// Writes a multi-size .ico (16-256 px) for shortcuts. Returns false on failure.
bool WriteIconFile(const XbeFile& xbe, const std::wstring& path);

} // namespace swrots
