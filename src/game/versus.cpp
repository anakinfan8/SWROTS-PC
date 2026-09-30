// Versus mode: what a character that was never made for versus needs to take part in a duel.

#include "game/versus.h"

#include <windows.h>

#include <cctype>
#include <cstring>
#include <string>

#include "core/log.h"
#include "core/patch.h"
#include "game/characters.h"
#include "game/game.h"
#include "game/resources.h"
#include "game/roster.h"

namespace swrots::game {

namespace {

// Fighter variants. The duel gives each fighter a variant of its class from a table per slot and
// player (kDuelistVariants: Anakin's and Obi-Wan's duel variants, 0 for the rest), which has the nine
// fighters only. An extra fighter (roster.cpp, slots from 9 in the duel) gets the first of its own
// variants whose model is on the disc instead: the first is not always shipped (the battle droid's
// plain "BattleDroid" model is not; its "hordeBattleDroid" is).
// The fighter's variant list is at +0x1E0: records of 20 bytes from +0x2C, {name, mesh path, 0,
// code, 0}. At the site
//   mov edx, [ecx * 4 + kDuelistVariants] / mov [edi + 0x1E8], edx   (13 bytes, 0x27B87E)
// ecx is slot * 2 + player, ebx the slot and edi the fighter; the flags are the caller's (a jne
// follows).
constexpr uint32_t kVariantSite = 0x0027B87E;
constexpr uint32_t kVariantContinue = 0x0027B88B;
constexpr uint8_t kVariantBytes[] = { 0x8B, 0x14, 0x8D, 0xC0, 0xC1, 0x5C, 0x00, 0x89, 0x97, 0xE8, 0x01, 0x00, 0x00 };

int32_t __stdcall FighterVariant(int tableIndex, int slot, const uint8_t* fighter)
{
    if (slot >= kDuelistCount)
        return VariantOnDisc(fighter, 0);
    return reinterpret_cast<const int32_t*>(uintptr_t(kDuelistVariants))[tableIndex];
}

__declspec(naked) void FighterVariantStub()
{
    __asm {
        pushfd
        pushad
        push edi
        push ebx
        push ecx
        call FighterVariant
        mov [esp + 20], eax // becomes edx
        popad
        popfd
        mov dword ptr [edi + 0x1E8], edx
        push kVariantContinue
        ret
    }
}

// Duel cameras. For every fighter the duel loads cinematics\introcamera\<name>_Intro_Cam.cin and
// cinematics\outrocamera\<name>_Outro_Cam.cin, <name> being the character's name (OldObiwan as
// "obiold"); without them there is no intro, and a win freezes on the missing outro. The disc has
// cameras only for the nine duelists, so an extra fighter without its own uses another duelist's
// (roster.cpp). At the site
//   push ecx / mov ecx, esp / push 0x1D12E0   (8 bytes, 0x27BAAD)
// ebp holds the name and ebx the slot.
constexpr uint32_t kCameraNameSite = 0x0027BAAD;
constexpr uint32_t kCameraNameContinue = 0x0027BAB5;
constexpr uint8_t kCameraNameBytes[] = { 0x51, 0x8B, 0xCC, 0x68, 0xE0, 0x12, 0x1D, 0x00 };

bool HasCamera(const char* name, const char* kind)
{
    std::string path = std::string("cinematics\\") + kind + "camera\\" + name + "_" + kind + "_cam.cin";
    for (char& c : path)
        c = char(tolower(static_cast<unsigned char>(c)));
    return DiscHasResource(path);
}

const char* __stdcall CameraName(const char* name, int slot)
{
    if (!name || slot < kDuelistCount || (HasCamera(name, "intro") && HasCamera(name, "outro")))
        return name;
    const char* standIn = ExtraFighterCameras(slot);
    if (standIn)
        LOG_INFO("Versus: %s has no duel cameras; using %s's", name, standIn);
    return standIn ? standIn : name;
}

__declspec(naked) void CameraNameStub()
{
    __asm {
        pushad
        push ebx
        push ebp
        call CameraName
        mov [esp + 8], eax // becomes ebp
        popad
        push ecx
        mov ecx, esp
        push 0x001D12E0
        push kCameraNameContinue
        ret
    }
}

} // namespace

void InstallVersus()
{
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kVariantSite)), kVariantBytes, sizeof(kVariantBytes)) == 0) {
        PatchJump(kVariantSite, reinterpret_cast<const void*>(&FighterVariantStub));
        PatchNop(kVariantSite + 5, sizeof(kVariantBytes) - 5);
    } else {
        LOG_WARN("Versus: fighter variant site does not match; not patched");
    }
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kCameraNameSite)), kCameraNameBytes,
            sizeof(kCameraNameBytes)) == 0) {
        PatchJump(kCameraNameSite, reinterpret_cast<const void*>(&CameraNameStub));
        PatchNop(kCameraNameSite + 5, sizeof(kCameraNameBytes) - 5);
    } else {
        LOG_WARN("Versus: duel camera site does not match; not patched");
    }
}

} // namespace swrots::game
