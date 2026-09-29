#pragma once
// Xbox kernel data structures, laid out exactly as the game's XDK code expects.
// Many are close to their Windows NT counterparts; the differences that matter
// (ANSI object names, smaller OBJECT_ATTRIBUTES, 28-byte critical sections)
// are why these are separate types rather than the winternl.h ones.

#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace xbox {

using NTSTATUS = LONG;

constexpr NTSTATUS X_STATUS_SUCCESS = 0x00000000;
constexpr NTSTATUS X_STATUS_ALERTED = 0x00000101;
constexpr NTSTATUS X_STATUS_TIMEOUT = 0x00000102;
constexpr NTSTATUS X_STATUS_PENDING = 0x00000103;
constexpr NTSTATUS X_STATUS_USER_APC = 0x000000C0;
constexpr NTSTATUS X_STATUS_OBJECT_NAME_EXISTS = 0x40000000;
constexpr NTSTATUS X_STATUS_NO_MORE_FILES = 0x80000006;
constexpr NTSTATUS X_STATUS_UNSUCCESSFUL = 0xC0000001;
constexpr NTSTATUS X_STATUS_NOT_IMPLEMENTED = 0xC0000002;
constexpr NTSTATUS X_STATUS_INVALID_HANDLE = 0xC0000008;
constexpr NTSTATUS X_STATUS_INVALID_PARAMETER = 0xC000000D;
constexpr NTSTATUS X_STATUS_INVALID_DEVICE_REQUEST = 0xC0000010;
constexpr NTSTATUS X_STATUS_NO_MEMORY = 0xC0000017;
constexpr NTSTATUS X_STATUS_BUFFER_TOO_SMALL = 0xC0000023;
constexpr NTSTATUS X_STATUS_OBJECT_TYPE_MISMATCH = 0xC0000024;
constexpr NTSTATUS X_STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034;
constexpr NTSTATUS X_STATUS_OBJECT_PATH_NOT_FOUND = 0xC000003A;
constexpr NTSTATUS X_STATUS_INSUFFICIENT_RESOURCES = 0xC000009A;
constexpr NTSTATUS X_STATUS_NOT_SUPPORTED = 0xC00000BB;


struct ANSI_STRING {
    USHORT Length;
    USHORT MaximumLength;
    char* Buffer;
};
using OBJECT_STRING = ANSI_STRING;

struct UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    wchar_t* Buffer;
};

struct OBJECT_ATTRIBUTES {
    HANDLE RootDirectory;
    OBJECT_STRING* ObjectName;
    ULONG Attributes;
};

struct IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        void* Pointer;
    };
    ULONG_PTR Information;
};

using PIO_APC_ROUTINE = void(__stdcall*)(void* ApcContext, IO_STATUS_BLOCK* IoStatusBlock, ULONG Reserved);

struct LIST_ENTRY {
    LIST_ENTRY* Flink;
    LIST_ENTRY* Blink;
};

// DISPATCHER_HEADER.Type
enum : UCHAR {
    EventNotificationObject = 0,
    EventSynchronizationObject = 1,
    MutantObject = 2,
    ProcessObject = 3,
    QueueObject = 4,
    SemaphoreObject = 5,
    ThreadObject = 6,
    TimerNotificationObject = 8,
    TimerSynchronizationObject = 9,
};

struct DISPATCHER_HEADER {
    UCHAR Type;
    UCHAR Absolute;
    UCHAR Size;
    UCHAR Inserted;
    LONG SignalState;
    LIST_ENTRY WaitListHead;
};
static_assert(sizeof(DISPATCHER_HEADER) == 0x10);

struct KEVENT {
    DISPATCHER_HEADER Header;
};

struct KSEMAPHORE {
    DISPATCHER_HEADER Header;
    LONG Limit;
};

struct KDPC;
using PKDEFERRED_ROUTINE = void(__stdcall*)(KDPC* Dpc, void* DeferredContext, void* SystemArgument1, void* SystemArgument2);

struct KDPC {
    SHORT Type;
    BOOLEAN Inserted;
    UCHAR Padding;
    LIST_ENTRY DpcListEntry;
    PKDEFERRED_ROUTINE DeferredRoutine;
    void* DeferredContext;
    void* SystemArgument1;
    void* SystemArgument2;
};
static_assert(sizeof(KDPC) == 0x1C);

struct KTIMER {
    DISPATCHER_HEADER Header;
    ULARGE_INTEGER DueTime;
    LIST_ENTRY TimerListEntry;
    KDPC* Dpc;
    LONG Period;
};
static_assert(sizeof(KTIMER) == 0x28);

struct RTL_CRITICAL_SECTION {
    DISPATCHER_HEADER Event;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
};
static_assert(sizeof(RTL_CRITICAL_SECTION) == 0x1C);

struct TIME_FIELDS {
    SHORT Year, Month, Day, Hour, Minute, Second, Millisecond, Weekday;
};

struct MM_STATISTICS {
    ULONG Length;
    ULONG TotalPhysicalPages;
    ULONG AvailablePages;
    ULONG VirtualMemoryBytesCommitted;
    ULONG VirtualMemoryBytesReserved;
    ULONG CachePagesCommitted;
    ULONG PoolPagesCommitted;
    ULONG StackPagesCommitted;
    ULONG ImagePagesCommitted;
};

struct XBOX_HARDWARE_INFO {
    ULONG Flags;
    UCHAR GpuRevision;
    UCHAR McpRevision;
    UCHAR Unknown3;
    UCHAR Unknown4;
};

struct XBOX_KRNL_VERSION {
    USHORT Major;
    USHORT Minor;
    USHORT Build;
    USHORT Qfe;
};

struct OBJECT_TYPE {
    void* AllocateProcedure;
    void* FreeProcedure;
    void* CloseProcedure;
    void* DeleteProcedure;
    void* ParseProcedure;
    void* DefaultObject;
    ULONG PoolTag;
};

// Xbox ETHREAD/KTHREAD are opaque to the game except for a handful of fields.
constexpr uint32_t KTHREAD_TlsData = 0x28;
constexpr uint32_t ETHREAD_Size = 0x140;

// Xbox KPCR (one per thread in this port; see kernel/thread.cpp).
struct KPCR {
    NT_TIB NtTib;       // 0x00  StackBase doubles as the TLS array pointer
    KPCR* SelfPcr;      // 0x1C
    void* Prcb;         // 0x20  -> PrcbData
    UCHAR Irql;         // 0x24
    UCHAR Pad[3];
    // KPRCB PrcbData (0x28). XAPI reads CurrentThread and DebugMonitorData
    // (+0x250), which must stay null.
    void* CurrentThread; // 0x28
    void* NextThread;    // 0x2C
    void* IdleThread;    // 0x30
    UCHAR PrcbRest[0x400];
};
static_assert(offsetof(KPCR, Irql) == 0x24);
static_assert(offsetof(KPCR, CurrentThread) == 0x28);

// File information classes the XAPI file layer uses.
enum FILE_INFORMATION_CLASS_X : ULONG {
    XFileDirectoryInformation = 1,
    XFileFullDirectoryInformation = 2,
    XFileBothDirectoryInformation = 3,
    XFileBasicInformation = 4,
    XFileStandardInformation = 5,
    XFileInternalInformation = 6,
    XFileNameInformation = 9,
    XFileRenameInformation = 10,
    XFileDispositionInformation = 13,
    XFilePositionInformation = 14,
    XFileModeInformation = 16,
    XFileAlignmentInformation = 17,
    XFileAllocationInformation = 19,
    XFileEndOfFileInformation = 20,
    XFileNetworkOpenInformation = 34,
};

// Xbox FILE_DIRECTORY_INFORMATION: identical to NT's except FileName is ANSI.
struct FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    char FileName[1];
};

} // namespace xbox
