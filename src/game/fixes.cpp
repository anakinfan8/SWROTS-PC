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

// StdHashString (StdHashString.h) tables with a node pool take new entries from the pool's free list
// (0x62640) without checking that it has one; the pools are sized for exactly what a level's PAK
// holds, so a level that loads more than that (a character added by the port's resource loading)
// ran out and dereferenced null. Tables without a pool allocate a 20-byte node instead (0x6267A),
// which is what an empty pool now does. The site
//   mov ecx, [eax + 0xC] / mov edi, [eax + 4] / inc ecx / mov [eax + 0xC], ecx   (10 bytes)
// becomes a jump to the guard, which continues at 0x62668 or takes the allocating path.
constexpr uint32_t kHashPoolTakeSite = 0x0006265E;
constexpr uint32_t kHashPoolTakeContinue = 0x00062668;
constexpr uint32_t kHashPoolAllocate = 0x0006267A;
constexpr uint8_t kHashPoolTakeBytes[] = { 0x8B, 0x48, 0x0C, 0x8B, 0x78, 0x04, 0x41, 0x89, 0x48, 0x0C };

__declspec(naked) void HashPoolTakeGuard()
{
    __asm {
        mov edi, dword ptr [eax + 4]
        test edi, edi
        jz allocate
        mov ecx, dword ptr [eax + 0xC]
        inc ecx
        mov dword ptr [eax + 0xC], ecx
        push kHashPoolTakeContinue
        ret
    allocate:
        push kHashPoolAllocate
        ret
    }
}

} // namespace

void InstallGameFixes()
{
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kHashPoolTakeSite)), kHashPoolTakeBytes,
            sizeof(kHashPoolTakeBytes)) == 0) {
        PatchJump(kHashPoolTakeSite, reinterpret_cast<const void*>(&HashPoolTakeGuard));
        PatchNop(kHashPoolTakeSite + 5, sizeof(kHashPoolTakeBytes) - 5);
    } else {
        LOG_WARN("Game fix: hash string pool site does not match; not patched");
    }
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kTimeStackPopSite)), kTimeStackPopBytes,
            sizeof(kTimeStackPopBytes)) == 0)
        PatchCall(kTimeStackPopSite, reinterpret_cast<const void*>(&TimeStackPopGuard), 7);
    else
        LOG_WARN("Game fix: time-scale stack site does not match; not patched");
}

} // namespace swrots::game
