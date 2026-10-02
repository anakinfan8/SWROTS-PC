// Characters: playing a level as another character class, in any of its costumes or any mesh.

#include "game/characters.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "core/patch.h"
#include "game/game.h"
#include "game/resources.h"
#include "kernel/kernel.h"

namespace swrots::game {

namespace {

// The chosen class, in the port's own memory: the game's copy of the name is gone after a reboot
// (every level change or restart), and the choice lasts until the game is closed.
std::string g_PlayerClassName;
const char* g_PlayerClass = nullptr;
std::string g_PlayerVariant; // a costume: its name, a part of it or its number; empty for the usual one
std::string g_PlayerMesh;    // a mesh under meshes/chars ("folder\\file"); empty for the costume's own
std::string g_PlayerSkin;    // a texture set: its number or name; empty for the costume's usual one
bool g_SaberColorSet = false; // the player's own saber colour (see ApplySaberColor)
float g_SaberColor[3] = {};

std::string Lower(std::string text)
{
    for (char& c : text)
        c = char(tolower(static_cast<unsigned char>(c)));
    return text;
}

bool MeshOnDisc(const char* mesh)
{
    return mesh && DiscHasResource(Lower(std::string("meshes\\chars\\") + mesh + ".msh"));
}

// A costume list's records, up to the first null name.
int VariantCount(const uint8_t* list)
{
    int count = 0;
    while (list && count < 32 && *reinterpret_cast<const char* const*>(list + kVariantRecords + count * kVariantRecordSize))
        ++count;
    return count;
}

const char* VariantName(const uint8_t* list, int i)
{
    return *reinterpret_cast<const char* const*>(list + kVariantRecords + i * kVariantRecordSize);
}

const char* VariantMesh(const uint8_t* list, int i)
{
    return *reinterpret_cast<const char* const*>(list + kVariantRecords + i * kVariantRecordSize + 4);
}

// A costume by its number, its name or a part of its name (the shortest name containing it: "duel"
// is Anakin_Duel); -1 when none match or two equally short ones do.
int FindVariant(const uint8_t* list, const std::string& spec)
{
    const int count = VariantCount(list);
    if (spec.empty() || !count)
        return -1;
    if (std::all_of(spec.begin(), spec.end(), [](char c) { return isdigit(static_cast<unsigned char>(c)) != 0; })) {
        int i = atoi(spec.c_str());
        return i < count ? i : -1;
    }
    for (int i = 0; i < count; ++i)
        if (_stricmp(VariantName(list, i), spec.c_str()) == 0)
            return i;
    // The shortest name containing it: "duel" is Anakin_Duel rather than Anakin_NPC_Duel.
    int found = -1;
    size_t shortest = 0;
    bool tie = false;
    for (int i = 0; i < count; ++i) {
        const std::string name = Lower(VariantName(list, i));
        if (name.find(Lower(spec)) == std::string::npos)
            continue;
        if (found < 0 || name.size() < shortest) {
            found = i;
            shortest = name.size();
            tie = false;
        } else if (name.size() == shortest) {
            tie = true;
        }
    }
    return tie ? -1 : found;
}

// A pointer to a short printable string in .rdata (where the costume tables' texts are).
bool IsRDataText(uint32_t p)
{
    if (p < kRDataStart || p >= kRDataEnd)
        return false;
    const char* t = reinterpret_cast<const char*>(uintptr_t(p));
    for (int i = 0; i < 80 && p + i < kRDataEnd; ++i) {
        if (!t[i])
            return i > 0;
        if (t[i] < 0x20 || t[i] > 0x7E)
            return false;
    }
    return false;
}

// A costume list's texture sets ("Starting texture set" in the game's level data): after the header's
// name and four numbers, up to six texture name suffixes, e.g. the clone trooper's "_var01" and
// "_var02". A character's +0x1EC picks one (1 the first; 0 the plain textures): when its mesh loads
// (0x155763), each texture is looked for as <name><suffix> first (0x14F7E0), where the level has it.
constexpr uint32_t kTextureSets = 0x14;
constexpr int kMaxTextureSets = 6;
constexpr uint32_t kCharacterTextureSet = 0x1EC;

std::vector<const char*> TextureSets(const uint8_t* list)
{
    std::vector<const char*> sets;
    for (int i = 0; list && i < kMaxTextureSets; ++i) {
        const uint32_t p = *reinterpret_cast<const uint32_t*>(list + kTextureSets + i * 4);
        if (!IsRDataText(p))
            break;
        sets.push_back(reinterpret_cast<const char*>(uintptr_t(p)));
    }
    return sets;
}

// A texture set by number (0 the plain textures) or name, with or without its "_" ("var01"); -1 when
// there is none.
int FindTextureSet(const uint8_t* list, const std::string& spec)
{
    const std::vector<const char*> sets = TextureSets(list);
    if (spec.empty())
        return -1;
    if (std::all_of(spec.begin(), spec.end(), [](char c) { return isdigit(static_cast<unsigned char>(c)) != 0; })) {
        const int i = atoi(spec.c_str());
        return i <= int(sets.size()) ? i : -1;
    }
    const std::string want = Lower(spec[0] == '_' ? spec.substr(1) : spec);
    if (want == "default" || want == "plain")
        return 0;
    for (size_t i = 0; i < sets.size(); ++i) {
        const std::string name = Lower(sets[i][0] == '_' ? sets[i] + 1 : sets[i]);
        if (name == want)
            return int(i) + 1;
    }
    return -1;
}

// When a level starts, 0x2AB660 creates the player through kSpawnPlayer with the launch settings'
// player (+0xAC, a string: the mission list's or the versus select's class, e.g. IAnakin); the
// character factory creates it by class name. The chosen class replaces that string there, after
// the mission list had its say. Entry: mov eax, [kEngineServices] (5 bytes).
using SpawnPlayerFn = void(__cdecl*)(void* playerClass, int variant);
SpawnPlayerFn g_OriginalSpawnPlayer = nullptr;

bool g_PlayerSpawned = false;  // a level started this boot (its player was created)

// A restart that falls back to a new game process (core/window.h) would lose the choice, which lives
// in this process: it is handed over as SWROTS_PLAYER, which the new process reads at its start.
void HandChoiceToRelaunch()
{
    char saber[64] = {};
    if (g_SaberColorSet)
        sprintf_s(saber, "%d %d %d", int(g_SaberColor[0] * 255 + 0.5f), int(g_SaberColor[1] * 255 + 0.5f),
            int(g_SaberColor[2] * 255 + 0.5f));
    SetEnvironmentVariableA("SWROTS_SABER", g_SaberColorSet ? saber : nullptr);
    if (!g_PlayerClass && g_PlayerVariant.empty() && g_PlayerSkin.empty() && g_PlayerMesh.empty()) {
        SetEnvironmentVariableA("SWROTS_PLAYER", nullptr);
        return;
    }
    std::string spec = g_PlayerClass ? g_PlayerClass : "-";
    if (g_PlayerClass && !g_PlayerVariant.empty())
        spec += " " + g_PlayerVariant;
    if (g_PlayerClass && !g_PlayerSkin.empty())
        spec += " skin " + g_PlayerSkin;
    if (!g_PlayerMesh.empty())
        spec += " mesh " + g_PlayerMesh;
    SetEnvironmentVariableA("SWROTS_PLAYER", spec.c_str());
}
bool g_RestartOnChange = true; // a change restarts the mission ([Debug] AutoRestart)

void __cdecl SpawnPlayerHook(void* playerClass, int variant)
{
    g_PlayerSpawned = true;
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

// The player's saber colour. A character's +0xF20 points at a colour (float r, g, b, 0-1) that overrides
// its sabers' own: the equip (0x280480, at 0x28054E) gives each saber that instead of its default, and
// the power-up switch (0x286B90, saber vfunc +0x614) leaves the colour alone while it is set (as Versus
// does for its fighters, from 0x650C98). The game's own `sabercolor` instead remaps a colour for every
// saber that has it. A saber's colour is set with its vfunc +0x604 (thiscall (const float rgb[3]));
// the pure colours (1,0,0), (0,1,0), (0,0,1), (1,0,1) are drawn as the game's tuned red, green, blue,
// purple, any other as it is (0x2E1600).
constexpr uint32_t kCharacterSaberColor = 0xF20;
constexpr uint32_t kCharacterWeapons = 0x1080; // 4 slots, 0x20 apart: the weapon object, or null
constexpr int kWeaponSlots = 4;
constexpr uint32_t kSaberSetColor = 0x604;
constexpr uint32_t kSaberVtables[] = { 0x005B1FF0, 0x005B2620, 0x005B2C90, 0x005B32F8, 0x005B3958 };

uint8_t* g_Player = nullptr;      // the level's player, from its creation (this boot)
uint32_t g_PlayerVtable = 0;      // its vtable then: the player is still there while it matches

bool PlayerAlive()
{
    return g_Player && *reinterpret_cast<uint32_t*>(g_Player) == g_PlayerVtable;
}

// Only the Jedi-like characters (IJedi and those built on it) have the colour override and the weapon
// slots; asked as the power-up switch asks (0x286BB2): IsA, vfunc +4, with the type's function as its
// key (a type reference is just that address, 0x23C820).
constexpr uint32_t kJediType = 0x00249950;

bool HasSaberColor(uint8_t* character)
{
    const auto isA = reinterpret_cast<bool(__fastcall*)(uint8_t*, void*, uint32_t)>(
        (*reinterpret_cast<void* const* const*>(character))[1]);
    return isA(character, nullptr, kJediType);
}

// Gives the player's sabers the chosen colour (or, with none, nothing: they keep theirs until the
// next level start).
void ApplySaberColor()
{
    if (!PlayerAlive() || !HasSaberColor(g_Player))
        return;
    *reinterpret_cast<const float**>(g_Player + kCharacterSaberColor) = g_SaberColorSet ? g_SaberColor : nullptr;
    if (!g_SaberColorSet)
        return;
    for (int i = 0; i < kWeaponSlots; ++i) {
        uint8_t* weapon = *reinterpret_cast<uint8_t**>(g_Player + kCharacterWeapons + i * 0x20);
        if (!weapon)
            continue;
        const uint32_t vtable = *reinterpret_cast<uint32_t*>(weapon);
        if (std::find(std::begin(kSaberVtables), std::end(kSaberVtables), vtable) == std::end(kSaberVtables))
            continue;
        const auto setColor = reinterpret_cast<void(__fastcall*)(uint8_t*, void*, const float*)>(
            reinterpret_cast<void* const*>(uintptr_t(vtable))[kSaberSetColor / 4]);
        setColor(weapon, nullptr, g_SaberColor);
    }
}

// A character's own copy of its class's costume list, with another body in its costume's record:
// others of the class (which share the game's list) keep theirs. Kept for as long as the character
// may load its body (the level; cleared at every boot).
struct OwnedList {
    std::string mesh;
    std::vector<uint8_t> list;
};

// Dresses a character being created (before its ICharacter::Init loads the body): the costume
// `costume` (a name, a part of one or a number; empty for `chosen`), texture set `skin` and body
// `mesh` (resolved in place: emptied when not usable). `who` names it in the log. Returns the costume
// index to use.
int Dress(uint8_t* character, int chosen, const std::string& costume, const std::string& skin, std::string& mesh,
    OwnedList& owned, const char* who)
{
    uint8_t*& list = *reinterpret_cast<uint8_t**>(character + kCharacterVariants);
    if (!costume.empty() && list) {
        const int wanted = FindVariant(list, costume);
        if (wanted < 0)
            LOG_WARN("Characters: no costume '%s'; the usual one instead", costume.c_str());
        else if (!mesh.empty() || MeshOnDisc(VariantMesh(list, wanted)))
            chosen = wanted;
        else
            LOG_WARN("Characters: costume %s's model (%s) is not on the disc; the usual one instead",
                VariantName(list, wanted), VariantMesh(list, wanted));
    }
    if (!mesh.empty()) {
        // A name as given (SWROTS_PLAYER takes any): "folder\file" as the disc or mods\ has it.
        std::string error;
        const std::string resolved = ResolveMesh(mesh, error);
        const std::string file = Lower("meshes\\chars\\" + resolved + ".msh");
        if (resolved.empty() || (!DiscHasResource(file) && !HasLooseResource(file))) {
            LOG_WARN("Characters: mesh '%s' not used (%s); the costume's own instead", mesh.c_str(),
                error.empty() ? "not on the disc or under mods" : error.c_str());
            mesh.clear();
        } else {
            mesh = resolved;
        }
    }
    if (!mesh.empty() && list) {
        const int count = VariantCount(list);
        const int slot = chosen >= 0 && chosen < count ? chosen : 0;
        const size_t size = kVariantRecords + (count + 1) * kVariantRecordSize;
        owned.mesh = mesh;
        owned.list.assign(list, list + size - kVariantRecordSize);
        owned.list.resize(size, 0); // the null record that ends it
        *reinterpret_cast<const char**>(owned.list.data() + kVariantRecords + slot * kVariantRecordSize + 4) =
            owned.mesh.c_str();
        list = owned.list.data();
        chosen = slot;
        LOG_INFO("Characters: %s's mesh is %s", who, mesh.c_str());
    }
    if (list && chosen >= 0 && chosen < VariantCount(list))
        LOG_INFO("Characters: %s's costume %s (%d)", who, VariantName(list, chosen), chosen);
    if (!skin.empty() && list) {
        const int set = FindTextureSet(list, skin);
        if (set < 0) {
            LOG_WARN("Characters: no texture set '%s'; the usual textures instead", skin.c_str());
        } else {
            *reinterpret_cast<int*>(character + kCharacterTextureSet) = set;
            if (set > 0) {
                // The set's textures from wherever the disc has them: those in the costume mesh's folder
                // ending with the suffix (e.g. meshes\chars\clonetrooper\hordetrooper_var01.stx).
                const std::string suffix = Lower(TextureSets(list)[set - 1]) + ".stx";
                const int slot = chosen >= 0 && chosen < VariantCount(list) ? chosen : 0;
                const std::string body = Lower(VariantMesh(list, slot));
                const std::string folder = "meshes\\chars\\" + body.substr(0, body.find('\\') + 1);
                int declared = 0;
                for (const std::string& name : DiscResourceNames(folder))
                    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                        DeclareDiscResource(name))
                        ++declared;
                LOG_INFO("Characters: %s's texture set %s (%d)%s", who, TextureSets(list)[set - 1], set,
                    declared ? ", textures from other levels" : "");
            }
        }
    }
    return chosen;
}

OwnedList g_PlayerList;

// Spawning a character into the running level, the way the game's own AI respawner does (AI.cpp,
// 0x19C1E0): create the class's object (the class registry's lookup 0xE9B60, then its create 0xE2350:
// class info vfunc +0x30), clear +0xC's 0x08000000, set the costume (+0x1E8), and hand it to the spawn
// (0xA2DE0: cdecl bool (object, owner or null, const float transform[16], 1)), which places it
// (object vfunc +0x1F4) and adds it to the level's instance manager, which initializes it (loading
// what the level lacks through the port's resource hooks). A character's transform is at +0x150:
// rows right, up, forward, position (as the dev item spawner at 0x2DF600 builds one in front of the
// player). The spawned character takes its class's own AI and teams.
constexpr uint32_t kClassLookup = 0x000E9B60;    // cdecl (const char* class) -> class info
constexpr uint32_t kCreateInstance = 0x000E2350; // cdecl (class info) -> object
constexpr uint32_t kSpawnInstance = 0x000A2DE0;  // cdecl bool (object, owner, const float m[16], int 1)
constexpr uint32_t kInstanceFlags = 0x0C;
constexpr uint32_t kInstanceInactive = 0x08000000;
constexpr uint32_t kCharacterCostume = 0x1E8;
constexpr uint32_t kCharacterTransform = 0x150;
constexpr uint32_t kActivate = 0xB4;           // object vfunc, thiscall ()
constexpr float kSpawnDistance = 120.0f; // in front of the player (a character is about 70 tall)

std::list<OwnedList> g_SpawnLists; // the spawned characters' own costume lists (this boot)
int g_Spawned = 0;

int __stdcall PlayerVariant(void* player, int variant)
{
    if (!player)
        return variant;
    g_Player = static_cast<uint8_t*>(player);
    g_PlayerVtable = *reinterpret_cast<uint32_t*>(player);
    if (g_SaberColorSet && HasSaberColor(g_Player)) // before it equips its saber (ICharacter::Init)
        *reinterpret_cast<const float**>(g_Player + kCharacterSaberColor) = g_SaberColor;
    const int chosen = g_PlayerClass ? VariantOnDisc(player, variant) : variant;
    return Dress(g_Player, chosen, g_PlayerVariant, g_PlayerSkin, g_PlayerMesh, g_PlayerList, "the player");
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
    if (preferred >= VariantCount(list))
        LOG_INFO("Characters: costume %s (%d): the class has no costume %d (the level's)", name, first, preferred);
    else
        LOG_INFO("Characters: costume %s (%d) instead of %d, whose model is not on the disc", name, first, preferred);
    return first;
}

bool ClassHasBody(const char* className)
{
    const uint8_t* list = FindVariantList(className);
    if (!list)
        return true; // no costume list: nothing known against it
    // A body: a mesh shipped with its animation binding (Poggle's static model has none).
    for (int i = 0, n = VariantCount(list); i < n; ++i)
        if (MeshOnDisc(VariantMesh(list, i)) &&
            DiscHasResource(Lower(std::string("meshes\\chars\\") + VariantMesh(list, i) + ".ban")))
            return true;
    return false;
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
        // "<class>[ <costume>][ mesh <mesh>]", as the console's player command; "-" keeps the level's class.
        std::vector<std::string> words;
        char* context = nullptr;
        for (char* w = strtok_s(spec, " ", &context); w; w = strtok_s(nullptr, " ", &context))
            words.push_back(w);
        for (size_t i = 0; i < words.size(); ++i) {
            if (i == 0) {
                if (words[0] != "-") {
                    g_PlayerClassName = words[0];
                    g_PlayerClass = g_PlayerClassName.c_str();
                }
            } else if (_stricmp(words[i].c_str(), "mesh") == 0 && i + 1 < words.size()) {
                g_PlayerMesh = words[++i]; // as given; resolved at the spawn (PlayerVariant)
            } else if (_stricmp(words[i].c_str(), "skin") == 0 && i + 1 < words.size()) {
                g_PlayerSkin = words[++i];
            } else {
                g_PlayerVariant = words[i];
            }
        }
    }
    char saber[64] = {};
    if (!environmentRead && GetEnvironmentVariableA("SWROTS_SABER", saber, sizeof(saber))) {
        float rgb[3];
        g_SaberColorSet = ParseSaberColor(saber, rgb);
        if (g_SaberColorSet)
            std::copy(rgb, rgb + 3, g_SaberColor);
    }
    environmentRead = true;
    g_PlayerSpawned = false;
    g_Player = nullptr;
    g_SpawnLists.clear(); // the level's characters went with the reboot
    kernel::SetBeforeRelaunch(&HandChoiceToRelaunch);
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

// Every costume list in the image, by the name its header gives (lower case), found once: the lists
// are static data, the same at every boot.
const std::unordered_map<std::string, const uint8_t*>& VariantLists()
{
    static std::unordered_map<std::string, const uint8_t*> lists;
    static bool scanned = false;
    if (scanned)
        return lists;
    scanned = true;
    const auto isText = IsRDataText;
    // A header's name and four small numbers (the weapon, font and skeleton tables built alike have
    // pointers there), then (at +0x2C) a first record: a costume name without a backslash and its mesh
    // ("Folder\File") with one.
    for (uint32_t a = kDataStart; a + kVariantRecords + kVariantRecordSize <= kDataEnd; a += 4) {
        const uint32_t name = *reinterpret_cast<const uint32_t*>(uintptr_t(a));
        if (!isText(name))
            continue;
        const auto* numbers = reinterpret_cast<const uint32_t*>(uintptr_t(a + 4));
        if (numbers[0] == 0 || numbers[0] >= 0x10000 || numbers[1] >= 0x10000 || numbers[2] >= 0x10000 ||
            numbers[3] >= 0x10000)
            continue;
        const uint32_t first = *reinterpret_cast<const uint32_t*>(uintptr_t(a + kVariantRecords));
        const uint32_t mesh = *reinterpret_cast<const uint32_t*>(uintptr_t(a + kVariantRecords + 4));
        if (isText(first) && isText(mesh) && !std::strchr(reinterpret_cast<const char*>(uintptr_t(first)), '\\') &&
            std::strchr(reinterpret_cast<const char*>(uintptr_t(mesh)), '\\'))
            lists.emplace(Lower(reinterpret_cast<const char*>(uintptr_t(name))), reinterpret_cast<const uint8_t*>(uintptr_t(a)));
    }
    return lists;
}

const uint8_t* FindVariantList(const char* className)
{
    if (!className)
        return nullptr;
    // The list names the class without its "I" (IAnakin -> Anakin).
    const char* wanted = (className[0] == 'I' || className[0] == 'i') && className[1] ? className + 1 : className;
    const auto& lists = VariantLists();
    const auto it = lists.find(Lower(wanted));
    return it != lists.end() ? it->second : nullptr;
}

std::vector<std::string> CharacterClasses(bool all)
{
    std::vector<std::string> out;
    auto* registry = *reinterpret_cast<uint8_t**>(uintptr_t(kClassRegistry));
    if (!registry)
        return out;
    auto** buckets = reinterpret_cast<uint8_t**>(registry + kClassRegistryBuckets);
    for (int b = 0; b < kClassRegistryBucketCount; ++b) {
        for (uint8_t* node = buckets[b]; node; node = *reinterpret_cast<uint8_t**>(node + 8)) {
            const char* className = *reinterpret_cast<const char**>(node + 4);
            if (className && (all || FindVariantList(className)))
                out.push_back(className);
        }
    }
    std::sort(out.begin(), out.end(), [](const std::string& x, const std::string& y) { return _stricmp(x.c_str(), y.c_str()) < 0; });
    return out;
}

bool PlayerInLevel()
{
    return g_PlayerSpawned;
}

void SetRestartOnChange(bool enabled)
{
    g_RestartOnChange = enabled;
}

bool RestartOnChange()
{
    return g_RestartOnChange;
}

int ClassVariantIndex(const char* className, const std::string& spec)
{
    return FindVariant(FindVariantList(className), spec);
}

std::vector<Variant> ClassVariants(const char* className)
{
    std::vector<Variant> out;
    const uint8_t* list = FindVariantList(className);
    for (int i = 0, n = VariantCount(list); i < n; ++i)
        out.push_back({ VariantName(list, i), VariantMesh(list, i), MeshOnDisc(VariantMesh(list, i)) });
    return out;
}

std::vector<std::string> CharacterMeshes(const std::string& filter)
{
    std::vector<std::string> out;
    const std::string want = Lower(filter);
    const std::string prefix = "meshes\\chars\\";
    // The disc's character bodies: the meshes shipped with an animation binding (.ban). The others are
    // limbs, debris, vehicles and effects (skeleton\skeleton_lightningfx), which crash the game as a
    // character (0x611B4). A mod's own meshes count: the engine makes their binding.
    const std::vector<std::string> disc = DiscResourceNames(prefix);
    std::vector<std::string> names;
    for (const std::string& name : disc) {
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".msh") == 0 &&
            std::find(disc.begin(), disc.end(), name.substr(0, name.size() - 4) + ".ban") != disc.end())
            names.push_back(name);
    }
    for (const std::string& name : LooseResourceNames(prefix))
        names.push_back(name);
    for (const std::string& name : names) {
        if (name.size() < prefix.size() + 4 || name.compare(name.size() - 4, 4, ".msh") != 0)
            continue;
        std::string mesh = name.substr(prefix.size(), name.size() - prefix.size() - 4);
        if (mesh.find('\\') == std::string::npos || (!want.empty() && mesh.find(want) == std::string::npos))
            continue;
        out.push_back(mesh);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string ResolveMesh(const std::string& text, std::string& error)
{
    std::string want = Lower(text);
    std::replace(want.begin(), want.end(), '/', '\\');
    const std::string prefix = "meshes\\chars\\";
    if (want.rfind(prefix, 0) == 0)
        want = want.substr(prefix.size());
    if (want.size() > 4 && want.compare(want.size() - 4, 4, ".msh") == 0)
        want.resize(want.size() - 4);
    std::vector<std::string> matches;
    for (const std::string& mesh : CharacterMeshes("")) {
        const size_t slash = mesh.find('\\');
        if (mesh == want || mesh.substr(0, slash) == want || mesh.substr(slash + 1) == want)
            matches.push_back(mesh);
    }
    if (matches.size() == 1)
        return matches[0];
    if (matches.size() > 1) {
        error = "several meshes match: " + matches[0] + ", " + matches[1] + (matches.size() > 2 ? ", ..." : "");
        return "";
    }
    if (want.find('\\') != std::string::npos && DiscHasResource(prefix + want + ".msh")) {
        error = want + " is not a character body (limbs, debris, a vehicle or an effect; see meshes)";
        return "";
    }
    if (want.find('\\') != std::string::npos)
        return want; // not on the disc: a loose copy under mods may provide it
    error = "no character mesh '" + text + "' on the disc (see meshes)";
    return "";
}

void SetPlayerVariant(const std::string& variant)
{
    g_PlayerVariant = variant;
}

void SetPlayerMesh(const std::string& mesh)
{
    g_PlayerMesh = mesh;
}

std::string PlayerVariantChoice()
{
    return g_PlayerVariant;
}

std::string PlayerMesh()
{
    return g_PlayerMesh;
}

void SetPlayerSkin(const std::string& skin)
{
    g_PlayerSkin = skin;
}

std::string PlayerSkin()
{
    return g_PlayerSkin;
}

std::vector<std::string> ClassTextureSets(const char* className)
{
    std::vector<std::string> out;
    for (const char* set : TextureSets(FindVariantList(className)))
        out.push_back(set);
    return out;
}

int ClassTextureSetIndex(const char* className, const std::string& spec)
{
    const uint8_t* list = FindVariantList(className);
    return list ? FindTextureSet(list, spec) : -1;
}

bool SpawnCharacter(const char* className, const std::string& costume, const std::string& skin,
    const std::string& mesh, std::string& error)
{
    if (!PlayerAlive()) {
        error = "no mission is running";
        return false;
    }
    const char* name = RegisteredClassName(className);
    if (!name) {
        error = std::string(className) + ": not a class the game knows";
        return false;
    }
    if (!ClassHasBody(name)) {
        error = std::string(name) + " was cut from the game (none of its costumes is on the disc)";
        return false;
    }
    void* info = reinterpret_cast<void*(__cdecl*)(const char*)>(uintptr_t(kClassLookup))(name);
    auto* object = info ? reinterpret_cast<uint8_t*(__cdecl*)(void*)>(uintptr_t(kCreateInstance))(info) : nullptr;
    if (!object) {
        error = std::string(name) + ": the game could not create it";
        return false;
    }
    *reinterpret_cast<uint32_t*>(object + kInstanceFlags) &= ~kInstanceInactive;
    char who[96];
    sprintf_s(who, "spawned %s #%d", name, ++g_Spawned);
    g_SpawnLists.emplace_back();
    std::string body = mesh;
    const int chosen = Dress(object, VariantOnDisc(object, 0), costume, skin, body, g_SpawnLists.back(), who);
    *reinterpret_cast<int*>(object + kCharacterCostume) = chosen;

    // In front of the player, facing it: right and forward turned round.
    float m[16];
    std::memcpy(m, g_Player + kCharacterTransform, sizeof(m));
    for (int i = 0; i < 3; ++i)
        m[12 + i] += m[8 + i] * kSpawnDistance;
    for (int i = 0; i < 3; ++i) {
        m[i] = -m[i];
        m[8 + i] = -m[8 + i];
    }
    const bool spawned = reinterpret_cast<bool(__cdecl*)(uint8_t*, void*, const float*, int)>(
        uintptr_t(kSpawnInstance))(object, nullptr, m, 1);
    if (!spawned) {
        error = std::string(name) + ": the game refused to spawn it";
        return false;
    }
    // Then activated, as the dev item spawner (0x2DF849) and the player's creation (0xB1D60) do.
    reinterpret_cast<void(__fastcall*)(uint8_t*, void*)>((*reinterpret_cast<void* const* const*>(object))[kActivate / 4])(
        object, nullptr);
    LOG_INFO("Characters: %s at %.0f %.0f %.0f", who, m[12], m[13], m[14]);
    return true;
}

void SetPlayerSaberColor(const float* rgb)
{
    g_SaberColorSet = rgb != nullptr;
    if (rgb)
        std::copy(rgb, rgb + 3, g_SaberColor);
    if (rgb)
        ApplySaberColor(); // live: the player's sabers change now
    else if (PlayerAlive() && HasSaberColor(g_Player))
        *reinterpret_cast<const float**>(g_Player + kCharacterSaberColor) = nullptr;
}

bool ParseSaberColor(const std::string& spec, float* rgb)
{
    static const struct { const char* name; float rgb[3]; } kNamed[] = {
        { "red", { 1, 0, 0 } }, { "green", { 0, 1, 0 } }, { "blue", { 0, 0, 1 } }, { "purple", { 1, 0, 1 } } };
    for (const auto& named : kNamed) {
        if (_stricmp(spec.c_str(), named.name) == 0) {
            std::copy(named.rgb, named.rgb + 3, rgb);
            return true;
        }
    }
    int r, g, b;
    char end;
    if (sscanf_s(spec.c_str(), "%d %d %d %c", &r, &g, &b, &end, 1) != 3 || r < 0 || g < 0 || b < 0 || r > 255 ||
        g > 255 || b > 255)
        return false;
    rgb[0] = r / 255.0f;
    rgb[1] = g / 255.0f;
    rgb[2] = b / 255.0f;
    return true;
}

bool PlayerSaberColor(float* rgb)
{
    if (g_SaberColorSet && rgb)
        std::copy(g_SaberColor, g_SaberColor + 3, rgb);
    return g_SaberColorSet;
}

bool SetPlayerClass(const char* className)
{
    if (!className) {
        g_PlayerClass = nullptr;
        g_PlayerVariant.clear();
        g_PlayerMesh.clear();
        g_PlayerSkin.clear();
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
