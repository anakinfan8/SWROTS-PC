#pragma once
// In-process reboot (see reboot.cpp).

#include <windows.h>

namespace swrots::kernel {

// Set by core/main.cpp. On the reboot worker thread, once the old instance's threads are gone:
//  - resetRuntime releases what the runtime created for it (audio voices, the D3D device, input),
//  - boot starts the game again, given the launch data page (4096 bytes) or null.
// fallback restarts the process instead, if the in-process reboot cannot stop the game.
struct RebootHandlers {
    void (*resetRuntime)() = nullptr;
    void (*boot)(const void* launchData) = nullptr;
    void (*fallback)(const void* launchData) = nullptr;
};
void SetRebootHandlers(const RebootHandlers& handlers);

// Called on a game thread (HalReturnToFirmware); does not return.
[[noreturn]] void RebootInProcess(const void* launchData);
bool Rebooting();

// Waits like WaitForMultipleObjectsEx (count 0: sleeps `ms`). On a game thread, the wait
// also ends when a reboot starts, and then the thread exits.
DWORD GameWait(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD ms, BOOL alertable);
[[noreturn]] void ExitGameThread();

// Host handles the game owns (files, events, threads...): closed when it reboots.
void TrackGameHandle(HANDLE h);
bool UntrackGameHandle(HANDLE h);

// Implemented by the kernel modules: wake threads parked in host waits (the DPC thread),
// and release what the old instance created (ke.cpp, thread.cpp, mm.cpp, file.cpp).
void WakeKernelThreads();
void ResetDispatcherForReboot();
void ResetKernelForReboot();

} // namespace swrots::kernel
