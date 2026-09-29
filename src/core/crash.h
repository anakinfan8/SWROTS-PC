#pragma once
#include <windows.h>

namespace swrots {

void InstallCrashHandler();

// Suspends every game thread and logs where it is (for diagnosing hangs).
void DumpAllThreads(const char* reason);

// Logs probable game return addresses found on the current stack.
void LogGameStack(const char* reason);

// Replacement for XAPI's UnhandledExceptionFilter (the last-chance handler
// wrapped around every Xbox thread).
LONG __stdcall GameUnhandledExceptionFilter(EXCEPTION_POINTERS* info);

} // namespace swrots
