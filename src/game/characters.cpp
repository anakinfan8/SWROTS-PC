// Characters: playing a level as another character class, in any of its costumes or any mesh.

#include "game/characters.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

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
std::string g_PlayerVariant; // a costume: its name, a part of it or its number; empty for the usual one
std::string g_PlayerMesh;    // a mesh under meshes/chars ("folder\\file"); empty for the costume's own
std::string g_PlayerSkin;    // a texture set: its number or name; empty for the costume's usual one
std::vector<uint8_t> g_PlayerVariants; // the player's own copy of its class's costumes, for a mesh

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

int __stdcall PlayerVariant(void* player, int variant)
{
    if (!player)
        return variant;
    uint8_t*& list = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(player) + kCharacterVariants);
    int chosen = g_PlayerClass ? VariantOnDisc(player, variant) : variant;
    if (!g_PlayerVariant.empty() && list) {
        const int wanted = FindVariant(list, g_PlayerVariant);
        if (wanted < 0)
            LOG_WARN("Characters: no costume '%s'; the usual one instead", g_PlayerVariant.c_str());
        else if (!g_PlayerMesh.empty() || MeshOnDisc(VariantMesh(list, wanted)))
            chosen = wanted;
        else
            LOG_WARN("Characters: costume %s's model (%s) is not on the disc; the usual one instead",
                VariantName(list, wanted), VariantMesh(list, wanted));
    }
    if (!g_PlayerMesh.empty()) {
        // A name as given (SWROTS_PLAYER takes any): "folder\file" as the disc or mods\ has it.
        std::string error;
        const std::string mesh = ResolveMesh(g_PlayerMesh, error);
        const std::string file = Lower("meshes\\chars\\" + mesh + ".msh");
        if (mesh.empty() || (!DiscHasResource(file) && !HasLooseResource(file))) {
            LOG_WARN("Characters: no mesh '%s' on the disc or under mods; the costume's own instead%s%s",
                g_PlayerMesh.c_str(), error.empty() ? "" : ": ", error.c_str());
            g_PlayerMesh.clear();
        } else {
            g_PlayerMesh = mesh;
        }
    }
    if (!g_PlayerMesh.empty() && list) {
        // The player's own copy of the list, with the mesh in its costume's record: other characters of
        // the class (which share the game's list) keep theirs.
        const int count = VariantCount(list);
        const int slot = chosen >= 0 && chosen < count ? chosen : 0;
        const size_t size = kVariantRecords + (count + 1) * kVariantRecordSize;
        g_PlayerVariants.assign(list, list + size - kVariantRecordSize);
        g_PlayerVariants.resize(size, 0); // the null record that ends it
        *reinterpret_cast<const char**>(g_PlayerVariants.data() + kVariantRecords + slot * kVariantRecordSize + 4) =
            g_PlayerMesh.c_str();
        list = g_PlayerVariants.data();
        chosen = slot;
        LOG_INFO("Characters: the player's mesh is %s", g_PlayerMesh.c_str());
    }
    if (list && chosen >= 0 && chosen < VariantCount(list))
        LOG_INFO("Characters: costume %s (%d)", VariantName(list, chosen), chosen);
    if (!g_PlayerSkin.empty() && list) {
        const int set = FindTextureSet(list, g_PlayerSkin);
        if (set < 0) {
            LOG_WARN("Characters: no texture set '%s'; the usual textures instead", g_PlayerSkin.c_str());
        } else {
            *reinterpret_cast<int*>(static_cast<uint8_t*>(player) + kCharacterTextureSet) = set;
            if (set > 0) {
                // The set's textures from wherever the disc has them: those in the costume mesh's folder
                // ending with the suffix (e.g. meshes\chars\clonetrooper\hordetrooper_var01.stx).
                const std::string suffix = Lower(TextureSets(list)[set - 1]) + ".stx";
                const int slot = chosen >= 0 && chosen < VariantCount(list) ? chosen : 0;
                const std::string mesh = Lower(VariantMesh(list, slot));
                const std::string folder = "meshes\\chars\\" + mesh.substr(0, mesh.find('\\') + 1);
                int declared = 0;
                for (const std::string& name : DiscResourceNames(folder))
                    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                        DeclareDiscResource(name))
                        ++declared;
                LOG_INFO("Characters: texture set %s (%d)%s", TextureSets(list)[set - 1], set,
                    declared ? ", textures from other levels" : "");
            }
        }
    }
    return chosen;
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
    environmentRead = true;
    g_PlayerSpawned = false;
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
    // A header's name, then (at +0x2C) a first record: a costume name without a backslash and its mesh
    // ("Folder\File") with one.
    for (uint32_t a = kDataStart; a + kVariantRecords + kVariantRecordSize <= kDataEnd; a += 4) {
        const uint32_t name = *reinterpret_cast<const uint32_t*>(uintptr_t(a));
        if (!isText(name))
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
    std::vector<std::string> names = DiscResourceNames(prefix);
    for (const std::string& name : LooseResourceNames(prefix)) // a mod's own meshes
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
