#pragma once

namespace swrots::xapi {

// Installs replacements for XAPI routines that touch hardware (TSC clock,
// raw disk partitions) and traps on every XDK library function that has no
// native implementation yet.
void InstallHooks();

// Logs the first calls of every replaced XDK function (debugging aid).
void EnableSdkTrace(bool enable);

// Registers a native implementation for an XDK symbol, replacing its trap.
// Call before InstallHooks.
void RegisterSdkReplacement(const char* symbolName, const void* implementation);

// Marks an XDK function as pure CPU code (no hardware access) that runs natively
// as-is instead of being trapped.
void RegisterSdkPassthrough(const char* symbolName);

} // namespace swrots::xapi

// Replaces the XDK function `symbol` (by its name in game/sdk_symbols.inc).
#define SDK_REPLACE_CAT2(a, b) a##b
#define SDK_REPLACE_CAT(a, b) SDK_REPLACE_CAT2(a, b)
#define SDK_REPLACE(symbol, function) \
    static const bool SDK_REPLACE_CAT(kSdkReplaced_, __COUNTER__) = \
        (::swrots::xapi::RegisterSdkReplacement(symbol, reinterpret_cast<const void*>(&function)), true)

// Lets the original XDK function `symbol` run natively (pure CPU code).
#define SDK_PASSTHROUGH(symbol) \
    static const bool SDK_REPLACE_CAT(kSdkPassthrough_, __COUNTER__) = \
        (::swrots::xapi::RegisterSdkPassthrough(symbol), true)
