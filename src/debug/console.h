#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swrots::debug {

// Bridge to the engine's own console (console.cpp in the game): commands typed
// in the debug menu run through the game's command handling (`set`, `listvars`,
// `help` and the game's commands), and the text the console prints -- which the
// Xbox build discards -- is collected for display.

enum class LineKind : uint8_t { Output, Error, Engine, Port };

struct ConsoleLine {
    LineKind kind;
    std::string text;
};

// Routes the console's print methods to the collected log. Call once at startup.
void InstallConsoleHooks();

// Adds a line to the log (any thread).
void AddConsoleLine(LineKind kind, const char* text);

// Queues a command line (any thread); it runs on the game thread at the next frame.
void QueueConsoleCommand(const std::string& line);

// Game thread, between frames: runs the queued commands.
void RunQueuedConsoleCommands();

// A copy of the log lines from index `first` on; returns the total count so far.
size_t CopyConsoleLines(size_t first, std::vector<ConsoleLine>& out);
void ClearConsole();
// True once after the log was cleared (the `clear` command).
bool ConsoleCleared();

} // namespace swrots::debug
