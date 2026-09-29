#pragma once
#include <windows.h>

#include <cstdint>

namespace swrots::kernel {

// Xbox contiguous memory lives in a region reserved by the loader at
// 0x80000000; "physical" addresses are offsets into it.
void ContiguousInit(void* base, uint32_t size);
bool IsContiguous(const void* p);
uint32_t ContiguousToPhysical(const void* p);
void* PhysicalToContiguous(uint32_t physical);

// True if any page of [p, p+size) was written since the last reset (and resets
// it). Ranges outside contiguous memory always report dirty.
bool ConsumeWrites(const void* p, uint32_t size);

// Kernel export, also used by the D3D layer to allocate Xbox-side surfaces.
void* __stdcall MmAllocateContiguousMemoryEx(ULONG NumberOfBytes, ULONG_PTR LowestAcceptableAddress,
    ULONG_PTR HighestAcceptableAddress, ULONG Alignment, ULONG ProtectionType);

} // namespace swrots::kernel
