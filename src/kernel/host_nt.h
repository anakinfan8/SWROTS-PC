#pragma once
// Host (Windows) native API used by the kernel layer. The Xbox kernel is an NT
// derivative, so file, event and wait services map almost 1:1 onto ntdll.

#include <windows.h>
#include <winternl.h>

extern "C" {

NTSYSAPI NTSTATUS NTAPI NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);
NTSYSAPI NTSTATUS NTAPI NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key);
NTSYSAPI NTSTATUS NTAPI NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation, ULONG Length, ULONG FileInformationClass);
NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation, ULONG Length, ULONG FileInformationClass);
NTSYSAPI NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FsInformation, ULONG Length, ULONG FsInformationClass);
NTSYSAPI NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
    ULONG FileInformationClass, BOOLEAN ReturnSingleEntry, PUNICODE_STRING FileName, BOOLEAN RestartScan);
NTSYSAPI NTSTATUS NTAPI NtQueryFullAttributesFile(POBJECT_ATTRIBUTES ObjectAttributes, PVOID FileInformation);
NTSYSAPI NTSTATUS NTAPI NtFlushBuffersFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock);

NTSYSAPI NTSTATUS NTAPI NtCreateEvent(PHANDLE EventHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, ULONG EventType, BOOLEAN InitialState);
NTSYSAPI NTSTATUS NTAPI NtSetEvent(HANDLE EventHandle, PLONG PreviousState);
NTSYSAPI NTSTATUS NTAPI NtPulseEvent(HANDLE EventHandle, PLONG PreviousState);
NTSYSAPI NTSTATUS NTAPI NtCreateSemaphore(PHANDLE SemaphoreHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, LONG InitialCount, LONG MaximumCount);
NTSYSAPI NTSTATUS NTAPI NtReleaseSemaphore(HANDLE SemaphoreHandle, LONG ReleaseCount, PLONG PreviousCount);
NTSYSAPI NTSTATUS NTAPI NtWaitForMultipleObjects(ULONG Count, HANDLE Handles[], ULONG WaitType,
    BOOLEAN Alertable, PLARGE_INTEGER Timeout);
NTSYSAPI NTSTATUS NTAPI NtSuspendThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount);
NTSYSAPI NTSTATUS NTAPI NtResumeThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount);
NTSYSAPI NTSTATUS NTAPI NtYieldExecution();
NTSYSAPI NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);

NTSYSAPI NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits,
    PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect);
NTSYSAPI NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE ProcessHandle, PVOID* BaseAddress, PSIZE_T RegionSize, ULONG FreeType);
NTSYSAPI NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, ULONG MemoryInformationClass,
    PVOID MemoryInformation, SIZE_T MemoryInformationLength, PSIZE_T ReturnLength);

NTSYSAPI VOID NTAPI RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord);
NTSYSAPI VOID NTAPI RtlTimeToTimeFields(PLARGE_INTEGER Time, PVOID TimeFields);
NTSYSAPI BOOLEAN NTAPI RtlTimeFieldsToTime(PVOID TimeFields, PLARGE_INTEGER Time);
}
