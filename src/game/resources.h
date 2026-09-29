#pragma once

#include <string>

namespace swrots::game {

// Hooks the engine's resource reader so loose files under mods\ override
// resources inside the level PAKs. A non-empty `dumpDir` saves every resource's
// raw bytes there as it loads; `logResources` logs every resource read.
void InstallResourceHooks(const std::wstring& gameData, const std::wstring& modsDir, const std::wstring& dumpDir, bool logResources);

} // namespace swrots::game
