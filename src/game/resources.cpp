// Loose-file resource overrides.
//
// The engine's resource reader (SceneResPacker::Read, game::kSceneResRead)
// reads each resource from the level PAK: it parses the resource's PAK header
// (which records where the resource ends), hands a PackFile reader to a
// per-type decode callback, then seeks the PAK to the resource's end. The
// engine also has a direct file I/O branch (development builds) that opens the
// resource's own path, e.g. d:\textures\levels\...\floor.stx, through StdFile.
//
// When mods\ has a loose copy of a resource, the hook keeps the PAK path (so
// the PAK stream stays in step) but wraps the decode callback: the wrapper
// builds the same StdFile + PackFile reader objects the direct I/O branch uses
// and decodes the loose file instead of the packed copy. The file system layer
// maps d:\ paths to mods\ when an override exists.

#include "game/resources.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "core/log.h"
#include "core/patch.h"
#include "debug/console.h"
#include "game/game.h"
#include "game/pak.h"

namespace swrots::game {

namespace {

// Engine path object (EFilePath): the full path is root(rootIndex) + dir + name.
struct EnginePath {
    uint8_t unknown0[4];
    uint8_t dirIsRoot;
    uint8_t pad;
    uint16_t rootIndex;
    char dir[64];
    char name[64];
};

// Engine objects with a vtable (StdFile, PackFile reader).
struct EngineObject {
    void** vtable;
};

// What SceneResPacker::Read hands its decode callback (a stack local in Read).
struct ReadSource {
    EngineObject* reader; // PackFile reader positioned at the resource data
    EnginePath* path;
    uint8_t fromPak;      // 1: PAK read, decoders may defer to the PAK (textures
                          //    become placeholders filled when precaching)
    uint8_t pad[3];
    void* record;         // resource record; +0x10 = raw data size
};

// Decode callback passed to SceneResPacker::Read: (scene, source, typeId, extra).
using DecodeFn = int(__cdecl*)(int scene, ReadSource* source, int typeId, int extra);

using ReadFn = int(__fastcall*)(void* packer, void* edx, DecodeFn decode, EnginePath* path, int typeId,
    void* record, int a5, int extra, int context);

// Engine helpers (see game.h).
using BuildPathFn = void(__fastcall*)(const EnginePath* path, void* edx, char* out, int dirOnly);
using PathStringFn = const char*(__fastcall*)(const EnginePath* path, void* edx, int dirOnly);
using StrCopyFn = char*(__cdecl*)(char* dst, const char* src);
using FileSizeFn = uint32_t(__cdecl*)(const EnginePath* path);
using StdFileCtorFn = EngineObject*(__fastcall*)(void* mem, void* edx, uint32_t arg);
using PackReaderCtorFn = EngineObject*(__fastcall*)(void* mem, void* edx, EngineObject* file, int arg);
using AllocFn = void*(__cdecl*)(uint32_t size);

const auto BuildPath = reinterpret_cast<BuildPathFn>(uintptr_t(kEnginePathBuild));
const auto PathString = reinterpret_cast<PathStringFn>(uintptr_t(kEnginePathString));
const auto StrCopy = reinterpret_cast<StrCopyFn>(uintptr_t(kEngineStrCopy));
const auto FileSize = reinterpret_cast<FileSizeFn>(uintptr_t(kEngineFileSize));
const auto StdFileCtor = reinterpret_cast<StdFileCtorFn>(uintptr_t(kStdFileCtor));
const auto PackReaderCtor = reinterpret_cast<PackReaderCtorFn>(uintptr_t(kPackReaderCtor));

// Global engine services table (alloc at +0x50, StdFile gate at +0x38).
uint8_t* Services() { return *reinterpret_cast<uint8_t**>(uintptr_t(kEngineServices)); }

ReadFn g_OriginalRead = nullptr;
bool g_EngineMessageHookInstalled = false; // per boot: the services table is the game's
bool g_LogResources = false;
std::wstring g_Mods;
std::wstring g_DumpDir; // empty: dumping disabled

// Reads in flight on this thread. Decoding a resource can load others (a level
// loads its contents), so Read re-enters; each wrapped decode takes the top
// entry on entry, before any nested read pushes its own.
struct PendingRead {
    DecodeFn decode;
    const char* path;
    uint8_t* packer;
    bool used;
    std::string* capture; // dumper: bytes read while decoding
    PendingRead* outer;
};
thread_local PendingRead* t_Top = nullptr;

template <typename Fn> Fn VirtualAt(EngineObject* object, uint32_t offset)
{
    return reinterpret_cast<Fn>(object->vtable[offset / 4]);
}

std::wstring LooseHostPath(const char* fullPath);

uint32_t LooseFileSize(const char* fullPath)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(LooseHostPath(fullPath).c_str(), GetFileExInfoStandard, &data))
        return 0;
    return data.nFileSizeLow;
}

// Mirrors the engine's direct I/O branch: open the resource path with StdFile,
// wrap it in a PackFile reader, decode it as a non-PAK read, then close and
// delete both.
int __cdecl DecodeLooseFile(int scene, ReadSource* pakSource, int typeId, int extra)
{
    PendingRead* pending = t_Top;
    pending->used = true;
    auto alloc = *reinterpret_cast<AllocFn*>(Services() + 0x50);
    uint8_t* gate = *reinterpret_cast<uint8_t**>(Services() + 0x38);
    EnginePath* path = pakSource->path;

    EngineObject* file = StdFileCtor(alloc(0x194), nullptr, gate ? *reinterpret_cast<uint32_t*>(gate + 0x24) : 0);
    StrCopy(reinterpret_cast<char*>(file) + 0x64, PathString(path, nullptr, 1));
    VirtualAt<void(__fastcall*)(EngineObject*, void*, EnginePath*, int)>(file, 0x0C)(file, nullptr, path, 1);
    EngineObject* reader = PackReaderCtor(alloc(0x24), nullptr, file, 0);
    if (pakSource->record) {
        // The decoder's size (record +0x10): the loose file's own size, from the host.
        uint32_t engineSize = FileSize(path);
        uint32_t hostSize = LooseFileSize(pending->path);
        if (hostSize != engineSize)
            LOG_WARN("Loose resource %s: engine size %u, file size %u", pending->path, engineSize, hostSize);
        *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pakSource->record) + 0x10) = hostSize;
    }

    ReadSource source = *pakSource;
    source.reader = reader;
    source.fromPak = 0;
    int result = pending->decode(scene, &source, typeId, extra);

    VirtualAt<void(__fastcall*)(EngineObject*, void*)>(file, 0x10)(file, nullptr);
    VirtualAt<void(__fastcall*)(EngineObject*, void*, int)>(file, 0x00)(file, nullptr, 1);
    VirtualAt<void(__fastcall*)(EngineObject*, void*, int)>(reader, 0x00)(reader, nullptr, 1);
    return result;
}

// Resource dumper: saves each resource's raw bytes to dump\<path> as the game
// reads them. While a resource decodes, the underlying file object's read
// method (vtable +0x2C, which every PackFile read goes through) is swapped for
// a recording shim, so nothing is read twice and the stream is never moved.
constexpr int kFileVtableSlots = 32;
constexpr int kFileReadSlot = 0x2C / 4;
constexpr uint32_t kProxyMarker = 0x5357524F; // stored after the copied slots

using FileReadFn = int(__fastcall*)(EngineObject* file, void* edx, void* buffer, uint32_t size);

int __fastcall RecordingRead(EngineObject* file, void* edx, void* buffer, uint32_t size)
{
    auto original = reinterpret_cast<FileReadFn>(file->vtable[kFileVtableSlots + 1]);
    int result = original(file, edx, buffer, size);
    if (PendingRead* top = t_Top; top && top->capture)
        top->capture->append(static_cast<const char*>(buffer), size);
    return result;
}

void WriteDump(const char* fullPath, const void* data, uint32_t size)
{
    std::wstring out = g_DumpDir;
    for (const char* p = fullPath + 2; *p; ++p) { // skip "d:"
        wchar_t c = wchar_t(static_cast<unsigned char>(*p));
        if (c == L'\\')
            CreateDirectoryW(out.c_str(), nullptr);
        out += c;
    }
    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES)
        return; // first copy wins (resources repeat across levels)
    HANDLE f = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(f, data, size, &written, nullptr);
    CloseHandle(f);
}

int __cdecl DecodeDumped(int scene, ReadSource* source, int typeId, int extra)
{
    PendingRead* pending = t_Top;
    EngineObject* file = source->reader ? *reinterpret_cast<EngineObject**>(reinterpret_cast<uint8_t*>(source->reader) + 0x14) : nullptr;
    if (!file)
        return pending->decode(scene, source, typeId, extra);

    std::string capture;
    pending->capture = &capture;
    void** original = file->vtable;
    bool proxied = reinterpret_cast<uint32_t>(original[kFileVtableSlots]) == kProxyMarker;
    void* proxy[kFileVtableSlots + 2];
    if (!proxied) {
        std::memcpy(proxy, original, kFileVtableSlots * sizeof(void*));
        proxy[kFileVtableSlots] = reinterpret_cast<void*>(kProxyMarker);
        proxy[kFileVtableSlots + 1] = original[kFileReadSlot];
        proxy[kFileReadSlot] = reinterpret_cast<void*>(&RecordingRead);
        file->vtable = proxy;
    }
    int result = pending->decode(scene, source, typeId, extra);
    if (!proxied)
        file->vtable = original;
    pending->capture = nullptr;

    if (!capture.empty())
        WriteDump(pending->path, capture.data(), uint32_t(capture.size()));
    else if (g_LogResources)
        LOG_INFO("Not dumped (data not read at load): %s", pending->path);
    return result;
}

// Engine warnings (services table +0x68, printf-style) are silent in the retail
// "FINAL" build; forward them to our log. Installed on the first resource read,
// once the engine has set up its services table.
void __cdecl EngineWarn(const char* format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    LOG_WARN("[engine] %s", text);
    debug::AddConsoleLine(debug::LineKind::Engine, text);
}

void InstallEngineMessageHook()
{
    static bool& installed = g_EngineMessageHookInstalled;
    if (installed || !Services())
        return;
    installed = true;
    *reinterpret_cast<void**>(Services() + 0x68) = reinterpret_cast<void*>(&EngineWarn);
}

// Resource paths are d:\<dir>\<name>; overrides live at mods\<dir>\<name>.
std::wstring LooseHostPath(const char* fullPath)
{
    std::wstring candidate = g_Mods + L"\\";
    for (const char* p = fullPath + 3; *p; ++p)
        candidate += wchar_t(static_cast<unsigned char>(*p));
    return candidate;
}

bool HasLooseCopy(const char* fullPath)
{
    if (g_Mods.empty() || _strnicmp(fullPath, "d:\\", 3) != 0)
        return false;
    DWORD attrs = GetFileAttributesW(LooseHostPath(fullPath).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// PAK indexes by host path, kept for the session.
std::unordered_map<std::wstring, PakIndex> g_PakIndexes;
std::wstring g_GameData;

const PakIndex* IndexForPak(const char* pakPath)
{
    // pakPath is "d:\pak\res_<level>.pak"; a copy under mods\ takes precedence.
    if (_strnicmp(pakPath, "d:\\", 3) != 0)
        return nullptr;
    std::wstring rest;
    for (const char* c = pakPath + 2; *c; ++c)
        rest += wchar_t(static_cast<unsigned char>(*c));
    std::wstring host = g_Mods + rest;
    if (GetFileAttributesW(host.c_str()) == INVALID_FILE_ATTRIBUTES)
        host = g_GameData + rest;
    auto it = g_PakIndexes.find(host);
    if (it == g_PakIndexes.end()) {
        it = g_PakIndexes.emplace(host, PakIndex()).first;
        if (it->second.Load(host))
            LOG_INFO("PAK index %s: %zu entries", pakPath, it->second.Count());
        else
            LOG_WARN("PAK index %s: could not read", pakPath);
    }
    return it->second.Count() ? &it->second : nullptr;
}

// Moves a PAK stream forward by reading and discarding up to `target`. The
// engine's buffered file streams sequentially (it tracks its own disk read
// position), and its seek does not survive jumps of several buffers, so skip
// the way the engine itself advances: by reading.
void SeekForward(EngineObject* reader, uint32_t target)
{
    auto tell = VirtualAt<uint32_t(__fastcall*)(EngineObject*, void*)>(reader, 0x44);
    auto read = VirtualAt<void(__fastcall*)(EngineObject*, void*, void*, uint32_t)>(reader, 0x30);
    PendingRead* top = t_Top; // keep skipped bytes out of the dumper's capture
    t_Top = nullptr;
    static char scratch[0x10000];
    for (uint32_t pos = tell(reader, nullptr); pos < target; pos = tell(reader, nullptr))
        read(reader, nullptr, scratch, target - pos < sizeof(scratch) ? target - pos : uint32_t(sizeof(scratch)));
    t_Top = top;
}

// Loose resources are decoded straight from their files without the PAK read,
// so they never consume PAK entries. PAK reads that find the stream at an entry
// nobody asked for (the packed copy of a loose resource, or the textures of a
// replaced mesh) skip forward to the requested entry.
int __fastcall ReadHook(void* packer, void* edx, DecodeFn decode, EnginePath* path, int typeId, void* record, int a5,
    int extra, int context)
{
    if (!path)
        return g_OriginalRead(packer, edx, decode, path, typeId, record, a5, extra, context);

    InstallEngineMessageHook();
    char full[512] = {};
    BuildPath(path, nullptr, full, 0);
    if (g_LogResources)
        LOG_INFO("Resource type %d: %s", typeId, full);

    PendingRead pending = { decode, full, static_cast<uint8_t*>(packer), false, nullptr, t_Top };
    if (HasLooseCopy(full)) {
        t_Top = &pending;
        ReadSource source = { nullptr, path, 0, {}, record };
        int result = DecodeLooseFile(context, &source, typeId, extra);
        t_Top = pending.outer;
        LOG_INFO("Loose resource: %s", full);
        return result;
    }

    auto* p = static_cast<uint8_t*>(packer);
    auto* reader = *reinterpret_cast<EngineObject**>(p + 4);
    if (*reinterpret_cast<uint32_t*>(p + 0x1D0) == 2 && p[0x88] == 0 && p[8] == 0 && reader &&
        _strnicmp(full, "d:\\", 3) == 0) {
        char pakPath[512] = {};
        BuildPath(reinterpret_cast<EnginePath*>(p + 0x174), nullptr, pakPath, 0);
        if (const PakIndex* index = IndexForPak(pakPath)) {
            std::string wanted = full + 3;
            for (char& c : wanted)
                c = char(tolower(static_cast<unsigned char>(c)));
            uint32_t pos = VirtualAt<uint32_t(__fastcall*)(EngineObject*, void*)>(reader, 0x44)(reader, nullptr);
            const PakIndex::Entry* next = index->AtOffset(pos);
            if (next && next->name != wanted) {
                if (const PakIndex::Entry* target = index->FindAfter(wanted, pos)) {
                    LOG_INFO("PAK: skipping from %s to %s", next->name.c_str(), wanted.c_str());
                    SeekForward(reader, target->offset);
                }
            }
        }
    }

    if (g_DumpDir.empty() || _strnicmp(full, "d:\\", 3) != 0)
        return g_OriginalRead(packer, edx, decode, path, typeId, record, a5, extra, context);
    t_Top = &pending;
    int result = g_OriginalRead(packer, edx, &DecodeDumped, path, typeId, record, a5, extra, context);
    t_Top = pending.outer;
    return result;
}

} // namespace

void InstallResourceHooks(const std::wstring& gameData, const std::wstring& modsDir, const std::wstring& dumpDir,
    bool logResources)
{
    g_EngineMessageHookInstalled = false;
    g_GameData = gameData;
    g_Mods = modsDir;
    g_DumpDir = dumpDir;
    if (!g_DumpDir.empty()) {
        CreateDirectoryW(g_DumpDir.c_str(), nullptr);
        LOG_INFO("Dumping resources to %ls", g_DumpDir.c_str());
    }
    g_LogResources = logResources;
    // Trampoline: the displaced "sub esp, 0x90" (6 bytes), then jump back.
    uint8_t* stub = AllocStub(16);
    static const uint8_t kPrologue[6] = { 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00 };
    std::memcpy(stub, kPrologue, sizeof(kPrologue));
    stub[6] = 0xE9;
    int32_t rel = int32_t(kSceneResRead + 6) - int32_t(uintptr_t(stub) + 11);
    std::memcpy(stub + 7, &rel, 4);
    g_OriginalRead = reinterpret_cast<ReadFn>(stub);
    PatchJump(kSceneResRead, reinterpret_cast<const void*>(&ReadHook));
}

} // namespace swrots::game
