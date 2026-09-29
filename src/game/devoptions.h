#pragma once

#include <cstdint>

namespace swrots::game {

// Developer options around the engine's console variables (vars_xbox.cfg).
// `debugDisplays` ([Debug] DebugDisplays in settings.ini) lets the engine draw its debug
// displays, e.g. the fps counter of `fps=true`.
void InstallDevOptions(bool debugDisplays);

// Called every frame with the engine's options object ([[kEngineServices] + 0x38] + 8), or null.
void ApplyDevOptions(uint8_t* options);

} // namespace swrots::game
