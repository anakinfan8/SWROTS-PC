#pragma once
// Facts about the one XBE this port runs: the retail NTSC release of
// Star Wars: Episode III - Revenge of the Sith (LA-023, v5.0, XDK 5849).
// Every address below is only valid for this exact binary, which is why the
// runtime refuses to start on anything with a different MD5.

#include <cstdint>

namespace swrots::game {

inline constexpr const char* kRetailMd5 = "6460ef37862de3af364a0563a0a18c4e";
inline constexpr uint32_t kTitleId = 0x4C410017;

inline constexpr uint32_t kImageBase = 0x00010000;
inline constexpr uint32_t kImageEnd = 0x0096E000; // end of the last section, page aligned
// Where the loader's PE headers are preserved (unused tail of swrots.exe's
// reserved range; must stay within 256 MiB of the image base for e_lfanew).
inline constexpr uint32_t kPeHeaderCopy = 0x00FF0000;

// XAPI / CRT entry points (statically linked into .text, run natively).
inline constexpr uint32_t kMainCrtStartup = 0x004A363C;
inline constexpr uint32_t kQueryPerformanceCounter = 0x004A341F;
inline constexpr uint32_t kQueryPerformanceFrequency = 0x004A3430; // hardcodes 733,333,333 Hz
// Engine timer "now": rdtsc >> 4, read as 733 MHz / 16 ticks (the timer divides
// by a hardcoded 45833.3 ticks per millisecond). Drives the frame limiter and delta time.
inline constexpr uint32_t kEngineTimerNow = 0x00227170;
// SceneResPacker::Read (thiscall, 7 args): reads one resource from the level PAK,
// or through StdFile when the packer's force-direct flag (+8) is set.
inline constexpr uint32_t kSceneResRead = 0x000527F0;
// EFilePath::Build (thiscall: char* out, bool dirOnly) -> root + dir [+ name].
inline constexpr uint32_t kEnginePathBuild = 0x00233420;
// EFilePath::String (thiscall: bool dirOnly) -> static buffer with root + dir [+ name].
inline constexpr uint32_t kEnginePathString = 0x00233520;
inline constexpr uint32_t kEngineStrCopy = 0x00223300;   // strcpy (cdecl)
inline constexpr uint32_t kEngineFileSize = 0x00230C70;  // cdecl (const EFilePath*)
inline constexpr uint32_t kStdFileCtor = 0x00232A10;     // thiscall (uint32_t), object 0x194 bytes
inline constexpr uint32_t kPackReaderCtor = 0x00232D20;  // thiscall (StdFile*, int), object 0x24 bytes
inline constexpr uint32_t kEngineServices = 0x00645F7C;  // -> services table (+0x50 alloc, +0x38 engine core)
// Renderer object = [[kEngineServices] + 0x38] + 8; its int fpsLimit (vars_xbox.cfg) drives the frame limiter.
inline constexpr uint32_t kRendererFpsLimit = 0xB8;
// Options object byte fields ([[kEngineServices] + 0x38] + 8), registered console variables unless noted.
inline constexpr uint32_t kOptionsFps = 0x38;        // fps: draw the fps counter
inline constexpr uint32_t kOptionsAiDisabled = 0xC4; // aiDisabled
inline constexpr uint32_t kOptionsGod = 0xC9;        // god
inline constexpr uint32_t kOptionsHideDebugDisplays = 0xD7; // not a variable: 1 hides the debug displays
// The engine console (console.cpp), vtable kConsoleVtable, created by the engine at startup and
// kept at [[kLaunchSettingsHolder] + 0x48]. Its commands (help, listvars, set...) reach it
// through the global kConsole, which the Xbox build never sets (the variable setters check it
// for null, the commands do not). Its text output is not in the Xbox build: the print slots are
// empty stubs.
inline constexpr uint32_t kLaunchSettingsHolder = 0x0066F7A4; // [+4] launch settings, [+0x48] console
inline constexpr uint32_t kConsoleInHolder = 0x48;
inline constexpr uint32_t kConsole = 0x0065E360;
inline constexpr uint32_t kConsoleVtable = 0x0055FF58;
inline constexpr uint32_t kConsoleExecuteSlot = 0x04; // thiscall (const char* line, bool echo)
inline constexpr uint32_t kConsolePrintSlot = 0x50;   // thiscall (const TString* text); "> line", results
inline constexpr uint32_t kConsolePrintTextSlot = 0x54; // thiscall (const char* text); command output
// The console's variable registry (console +0x1C, vtable 0x55FF0C) and command table (+0x2B4). Both
// keep a hash table of 37 buckets at +4: {vtable, bucket count, buckets[37]}, nodes {value, name, next}.
// In the Xbox build the built-in commands that enumerate them (help, listvars) and `set` (registry
// slot +0x08) are empty stubs; the registry's find (+0x04) and set (+0x1C, as vars_xbox.cfg uses) work.
inline constexpr uint32_t kConsoleRegistry = 0x1C;
inline constexpr uint32_t kConsoleCommands = 0x2B4;
inline constexpr uint32_t kRegistryFindSlot = 0x04;   // thiscall (const char* name) -> variable or null
inline constexpr uint32_t kRegistrySetSlot = 0x1C;    // thiscall (const char* name, const char* value, int)
inline constexpr uint32_t kHashBuckets = 0x0C;        // from the owner: +4 table, +8 count, +0xC buckets
// Variable objects: vtable {Get(TString* out), Set(const TString* in), TypeName()}, +0x10 -> the value.
inline constexpr uint32_t kTStringCtor = 0x00225100;  // thiscall (): empty string
inline constexpr uint32_t kTStringDtor = 0x00225230;  // thiscall ()
inline constexpr uint32_t kUnhandledExceptionFilter = 0x004A319F;
inline constexpr uint32_t kXMountUtilityDrive = 0x004A0A7B;
inline constexpr uint32_t kXFormatUtilityDrive = 0x004A0B80;
// XAPI startup routine that locates the Xbox kernel's INIT section and edits a
// GDT descriptor (privileged); meaningless on Windows, so it becomes a RET.
inline constexpr uint32_t kXapiReclaimKernelInit = 0x004A4E16;
// XAPI CancelIo: walks the kernel's per-thread IRP list; replaced by the host's.
inline constexpr uint32_t kXapiCancelIo = 0x004EE8F9;

// Instructions that address the Xbox kernel's per-processor/per-thread data
// through the FS segment. On Windows FS points at the TEB instead, so each one
// is rewritten into a call to a stub that reads our emulated KPCR.
enum class FsKind : uint8_t {
    Fs20Eax, // mov eax, fs:[0x20]         KPCR.Prcb
    Fs24Eax, // movzx eax, byte fs:[0x24]  KPCR.Irql
    Fs28Eax, // mov eax, fs:[0x28]         KPCR.PrcbData.CurrentThread
    Fs04Ecx, // mov ecx, fs:[4]            KPCR.NtTib.StackBase (TLS array)
    Fs04Edi, // mov edi, fs:[4]
};

struct FsPatchSite {
    uint32_t address;
    uint8_t length;
    FsKind kind;
};

// fs:[0] (the SEH chain) is layout-compatible with Windows and left alone.
inline constexpr FsPatchSite kFsPatchSites[] = {
    { 0x004A054D, 8, FsKind::Fs24Eax },
    { 0x004A0559, 6, FsKind::Fs28Eax },
    { 0x004A0564, 7, FsKind::Fs04Ecx },
    { 0x004A0575, 8, FsKind::Fs24Eax },
    { 0x004A0581, 6, FsKind::Fs28Eax },
    { 0x004A0587, 7, FsKind::Fs04Ecx },
    { 0x004A18EB, 6, FsKind::Fs20Eax },
    { 0x004A319F, 6, FsKind::Fs20Eax },
    { 0x004A31EB, 6, FsKind::Fs28Eax },
    { 0x004A32B8, 8, FsKind::Fs24Eax },
    { 0x004A32C4, 6, FsKind::Fs28Eax },
    { 0x004A32E1, 6, FsKind::Fs28Eax },
    { 0x004A35D2, 6, FsKind::Fs20Eax },
    { 0x004A35EE, 6, FsKind::Fs28Eax },
    { 0x004A35F4, 7, FsKind::Fs04Edi },
    { 0x004A4B69, 6, FsKind::Fs20Eax },
    { 0x004A4F29, 6, FsKind::Fs20Eax },
    { 0x004A6BFB, 6, FsKind::Fs20Eax },
    { 0x004A6C6F, 6, FsKind::Fs20Eax },
    { 0x004A74EE, 6, FsKind::Fs20Eax },
    { 0x004A751D, 6, FsKind::Fs20Eax },
    { 0x004E8610, 8, FsKind::Fs24Eax },
    { 0x004E8626, 6, FsKind::Fs28Eax },
    { 0x004E8637, 7, FsKind::Fs04Ecx },
    { 0x004E875B, 8, FsKind::Fs24Eax },
    { 0x004E8770, 6, FsKind::Fs28Eax },
    { 0x004E8781, 7, FsKind::Fs04Ecx },
    { 0x004E87D5, 7, FsKind::Fs04Ecx },
};

// Statically linked XDK symbols (see tools/gen_symbols.py).
enum class SymbolKind : uint8_t { Sdk, Xapi, Data };

struct SdkSymbol {
    uint32_t address;
    const char* name;
    SymbolKind kind;
};

const SdkSymbol* SdkSymbols(uint32_t* count);
const SdkSymbol* FindSdkSymbol(const char* name);
// Nearest symbol at or below `address` (for crash reports); may be null.
const SdkSymbol* NearestSdkSymbol(uint32_t address);

// Reconstructed name of the game function containing `address`, from symbols\swrots.map
// next to the exe (written by tools/symbols; absent in normal installs). Fills `out` with
// "Name+0xOFF" and returns true if found.
bool DescribeGameAddress(uint32_t address, char* out, size_t size);

// Debugging: logs up to `max` return addresses into game code found on the stack from `stackTop`
// (values right after a CALL), named where the symbol map knows them. Old frames can show up too.
void LogGameCallers(const char* tag, const void* stackTop, int max = 16);

} // namespace swrots::game
