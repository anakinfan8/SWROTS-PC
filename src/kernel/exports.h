#pragma once
// Registration of native kernel exports. Each implementation file declares its
// functions with KERNEL_EXPORT(ordinal, function); InstallThunks resolves the
// game's import table from the registry.

#include <cstdint>

#include "kernel/xbox.h"

namespace swrots::kernel {

bool RegisterExport(uint32_t ordinal, const char* name, void* address);

} // namespace swrots::kernel

#define KERNEL_EXPORT(ordinal, symbol) \
    static const bool kRegistered_##symbol = ::swrots::kernel::RegisterExport(ordinal, #symbol, (void*)&symbol)

// Calling conventions used by xboxkrnl.exe.
#define XBAPI __stdcall
#define XBFASTCALL __fastcall
