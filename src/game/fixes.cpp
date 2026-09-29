// Guards against bugs in the game's own code, found through the port's developer tools.

#include "game/fixes.h"

#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "core/patch.h"

namespace swrots::game {

namespace {

// The time manager (TManager_Time, 0xB7490) keeps a stack of time-scale states for slow-motion
// effects. When an effect's timer runs out it pops the stack (0xA64E0) without checking that
// the stack has an entry. The timeScale variable's change callback (0x98940, run when it is set
// through the console) sets the manager's "effect active" flag (+0x14) without pushing one, so
// the pop read before the stack's storage and crashed (writing the float in memory skips the
// callback, which is why that never crashed). Guarded: an empty stack counts as no
// effect active (the function then restores the normal scale). The site
//   mov al, [esi + 0x14] / xor ebx, ebx / cmp al, bl   (7 bytes; je 0xA6555 follows)
// becomes a call that sets ZF for "inactive or empty" and ebx = 0.
constexpr uint32_t kTimeStackPopSite = 0x000A64E5;
constexpr uint8_t kTimeStackPopBytes[] = { 0x8A, 0x46, 0x14, 0x33, 0xDB, 0x3A, 0xC3, 0x74 };

__declspec(naked) void TimeStackPopGuard()
{
    __asm {
        xor ebx, ebx
        mov al, byte ptr [esi + 0x14]
        test al, al
        jz done
        cmp dword ptr [esi + 0x38], 0
    done:
        ret
    }
}

} // namespace

void InstallGameFixes()
{
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kTimeStackPopSite)), kTimeStackPopBytes,
            sizeof(kTimeStackPopBytes)) == 0)
        PatchCall(kTimeStackPopSite, reinterpret_cast<const void*>(&TimeStackPopGuard), 7);
    else
        LOG_WARN("Game fix: time-scale stack site does not match; not patched");
}

} // namespace swrots::game
