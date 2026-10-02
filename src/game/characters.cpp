// Characters: playing a level as another character class.

#include "game/characters.h"

#include <windows.h>

#include <cctype>
#include <cstring>
#include <string>

#include "core/log.h"
#include "core/patch.h"
#include "game/game.h"
#include "game/resources.h"

namespace swrots::game {

namespace {

// The chosen class, in the port's own memory: the game's copy of the name is gone after a reboot
// (every level change or restart), and the choice lasts until the game is closed.
std::string g_PlayerClassName;
const char* g_PlayerClass = nullptr;

// When a level starts, 0x2AB660 creates the player through kSpawnPlayer with the launch settings'
// player (+0xAC, a string: the mission list's or the versus select's class, e.g. IAnakin); the
// character factory creates it by class name. The chosen class replaces that string there, after
// the mission list had its say. Entry: mov eax, [kEngineServices] (5 bytes).
using SpawnPlayerFn = void(__cdecl*)(void* playerClass, int variant);
SpawnPlayerFn g_OriginalSpawnPlayer = nullptr;

void __cdecl SpawnPlayerHook(void* playerClass, int variant)
{
    if (g_PlayerClass && playerClass) {
        auto assign = reinterpret_cast<void(__fastcall*)(void*, void*, const char*)>(uintptr_t(kTStringAssign));
        assign(playerClass, nullptr, g_PlayerClass);
        LOG_INFO("Characters: the player is %s", g_PlayerClass);
    }
    g_OriginalSpawnPlayer(playerClass, variant);
}

// The player's variant: -1 above means the profile's costume, an index into the level's own class's
// variants. The created player gets it through its vfunc +0x2C8 (0xB1D60); at
//   mov eax, [esp + 0xC] / mov edx, [ecx] / push eax   (7 bytes, 0xB1DE1)
// ecx is the new player and [esp + 0xC] the variant. A chosen class gets one whose model is on disc.
constexpr uint32_t kPlayerVariantSite = 0x000B1DE1;
constexpr uint32_t kPlayerVariantContinue = 0x000B1DE8;
constexpr uint8_t kPlayerVariantBytes[] = { 0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x11, 0x50 };

int __stdcall PlayerVariant(const void* player, int variant)
{
    return g_PlayerClass ? VariantOnDisc(player, variant) : variant;
}

__declspec(naked) void PlayerVariantStub()
{
    __asm {
        push ecx
        push dword ptr [esp + 0x10] // the variant ([esp + 0xC] at the site)
        push ecx
        call PlayerVariant
        pop ecx
        mov edx, [ecx]
        push eax
        push kPlayerVariantContinue
        ret
    }
}

// HUD portraits. The game manager ([0x7EB964]) keeps a list of character portraits, loaded at the
// level start for the characters the level expects (0x27BE30): an array at +0x260 {capacity, count,
// data} of {face texture, select-screen head texture, face name}, added to with 0x1F8A00. The HUD
// finds the player's face in it by name (0x27B740: thiscall (name, head?)), the name from the
// game's table of portraits per class (kPortraits: 12 faces, then 0; the heads 0x34 bytes on). A
// class the level did not expect is not in the list -- the HUD then shows the missing-texture
// pattern -- so a portrait of the table missing from the list is loaded when asked for, the way the
// level start loads them, and added. Its textures come from another level's PAK if need be.
constexpr uint32_t kPortraitLookup = 0x0027B740;
constexpr uint32_t kPortraitListAdd = 0x001F8A00; // thiscall (const Record*)
constexpr uint32_t kPortraits = 0x00650C30;
constexpr uint32_t kPortraitHeads = kPortraits + 0x34;
constexpr int kPortraitCount = 12;
constexpr uint8_t kPortraitLookupPrologue[] = { 0x55, 0x8B, 0x6C, 0x24, 0x08, 0x85, 0xED }; // push ebp; mov ebp, [esp+8]; test ebp, ebp

struct PortraitRecord {
    void* face;
    void* head;
    const char* name;
};

using PortraitLookupFn = void*(__fastcall*)(uint8_t* manager, void* edx, const char* name, int head);
PortraitLookupFn g_OriginalPortraitLookup = nullptr;

// Loads an interface texture by path through the engine's texture manager, as 0x27C1D1 does.
void* LoadInterfaceTexture(const char* path)
{
    auto* services = *reinterpret_cast<uint8_t**>(uintptr_t(kEngineServices));
    auto* textures = *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(services + 0x38) + 0x4C);
    alignas(4) uint8_t enginePath[0x100] = {};
    auto pathFromText = reinterpret_cast<void*(__fastcall*)(void*, void*, const char*, int)>(uintptr_t(kEnginePathFromText));
    void* filePath = pathFromText(enginePath, nullptr, path, 1);
    int first = 0, second = 0;
    using LoadFn = void*(__fastcall*)(void*, void*, void*, int*, int*, int, int, int);
    auto load = (*reinterpret_cast<LoadFn* const*>(textures))[1];
    return load(textures, nullptr, filePath, &second, &first, 1, 0, 1);
}

void* __fastcall PortraitLookupHook(uint8_t* manager, void* edx, const char* name, int head)
{
    void* found = g_OriginalPortraitLookup(manager, edx, name, head);
    if (found || !manager || !name)
        return found;
    auto* faces = reinterpret_cast<const char* const*>(uintptr_t(kPortraits));
    auto* heads = reinterpret_cast<const char* const*>(uintptr_t(kPortraitHeads));
    for (int i = 0; i < kPortraitCount; ++i) {
        if (!faces[i] || _stricmp(faces[i], name) != 0)
            continue;
        static const uint8_t* failedFor = nullptr; // the manager (level) a portrait could not load in
        static uint32_t failed = 0;
        if (failedFor != manager) {
            failedFor = manager;
            failed = 0;
        }
        if (failed & (1u << i))
            return nullptr;
        PortraitRecord record = { LoadInterfaceTexture(faces[i]), heads[i] ? LoadInterfaceTexture(heads[i]) : nullptr,
            faces[i] };
        if (!record.face) {
            failed |= 1u << i;
            LOG_WARN("Characters: HUD portrait %s did not load", faces[i]);
            return nullptr;
        }
        // Added even without a head, so it is loaded once per level.
        reinterpret_cast<void(__fastcall*)(void*, void*, const PortraitRecord*)>(uintptr_t(kPortraitListAdd))(
            manager + 0x260, nullptr, &record);
        LOG_INFO("Characters: HUD portrait %s loaded", faces[i]);
        return head ? record.head : record.face;
    }
    return nullptr;
}

} // namespace

int VariantOnDisc(const void* character, int preferred)
{
    if (!character)
        return preferred;
    const uint8_t* list = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(character) + 0x1E0);
    int first = -1;
    for (int i = 0; list && i < 32; ++i) {
        const uint8_t* record = list + 0x2C + i * 20;
        const char* name = *reinterpret_cast<const char* const*>(record);
        const char* mesh = *reinterpret_cast<const char* const*>(record + 4);
        if (!name)
            break;
        if (!mesh)
            continue;
        std::string path = std::string("meshes\\chars\\") + mesh + ".msh";
        for (char& c : path)
            c = char(tolower(static_cast<unsigned char>(c)));
        if (!DiscHasResource(path))
            continue;
        if (i == preferred)
            return i;
        if (first < 0)
            first = i;
    }
    if (first < 0 || first == preferred)
        return preferred;
    const char* name = *reinterpret_cast<const char* const*>(list + 0x2C + first * 20);
    LOG_INFO("Characters: variant %s (%d) instead of %d, whose model is not on the disc", name, first, preferred);
    return first;
}

const char* RegisteredClassName(const char* name)
{
    auto* registry = *reinterpret_cast<uint8_t**>(uintptr_t(kClassRegistry));
    if (!registry || !name)
        return nullptr;
    auto** buckets = reinterpret_cast<uint8_t**>(registry + kClassRegistryBuckets);
    for (int b = 0; b < kClassRegistryBucketCount; ++b) {
        for (uint8_t* node = buckets[b]; node; node = *reinterpret_cast<uint8_t**>(node + 8)) {
            const char* className = *reinterpret_cast<const char**>(node + 4);
            if (className && _stricmp(className, name) == 0)
                return className;
        }
    }
    return nullptr;
}

void InstallCharacters()
{
    // Development aid: SWROTS_PLAYER=<class> from the start, for unattended tests; read at the first
    // boot only, so the console's choice survives reboots. The class name is checked when the
    // registry exists (it does not yet at boot), so it is used as given.
    static bool environmentRead = false;
    char spec[128] = {};
    if (!environmentRead && GetEnvironmentVariableA("SWROTS_PLAYER", spec, sizeof(spec))) {
        g_PlayerClassName = spec;
        g_PlayerClass = g_PlayerClassName.c_str();
    }
    environmentRead = true;
    uint8_t* stub = AllocStub(16);
    std::memcpy(stub, reinterpret_cast<const void*>(uintptr_t(kSpawnPlayer)), 5);
    stub[5] = 0xE9;
    int32_t rel = int32_t(kSpawnPlayer + 5) - int32_t(uintptr_t(stub) + 10);
    std::memcpy(stub + 6, &rel, 4);
    g_OriginalSpawnPlayer = reinterpret_cast<SpawnPlayerFn>(stub);
    PatchJump(kSpawnPlayer, reinterpret_cast<const void*>(&SpawnPlayerHook));
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kPlayerVariantSite)), kPlayerVariantBytes,
            sizeof(kPlayerVariantBytes)) == 0) {
        PatchJump(kPlayerVariantSite, reinterpret_cast<const void*>(&PlayerVariantStub));
        PatchNop(kPlayerVariantSite + 5, sizeof(kPlayerVariantBytes) - 5);
    } else {
        LOG_WARN("Characters: unexpected code at the player variant site; variants unchanged");
    }
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kPortraitLookup)), kPortraitLookupPrologue,
            sizeof(kPortraitLookupPrologue)) == 0) {
        uint8_t* portraitStub = AllocStub(16);
        std::memcpy(portraitStub, kPortraitLookupPrologue, sizeof(kPortraitLookupPrologue));
        portraitStub[7] = 0xE9;
        int32_t back = int32_t(kPortraitLookup + 7) - int32_t(uintptr_t(portraitStub) + 12);
        std::memcpy(portraitStub + 8, &back, 4);
        g_OriginalPortraitLookup = reinterpret_cast<PortraitLookupFn>(portraitStub);
        PatchJump(kPortraitLookup, reinterpret_cast<const void*>(&PortraitLookupHook));
    } else {
        LOG_WARN("Characters: unexpected code at the HUD portrait lookup; portraits unchanged");
    }
}

bool SetPlayerClass(const char* className)
{
    if (!className) {
        g_PlayerClass = nullptr;
        return true;
    }
    const char* name = RegisteredClassName(className);
    if (!name)
        return false;
    g_PlayerClassName = name;
    g_PlayerClass = g_PlayerClassName.c_str();
    return true;
}

const char* PlayerClass()
{
    return g_PlayerClass;
}

} // namespace swrots::game
