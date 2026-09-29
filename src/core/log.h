#pragma once
#include <cstdarg>

namespace swrots {

enum class LogLevel { Debug, Info, Warn, Error };

// append: keep the file's contents (the game rebooting itself within a session).
void LogInit(const wchar_t* path, bool console, bool append = false);
void LogWrite(LogLevel level, const char* fmt, ...);
void LogWriteV(LogLevel level, const char* fmt, va_list args);
void LogFlush();
// Closes the log file (later messages only go to the debugger/console).
void LogClose();

// Terminates the process after logging and showing a message box.
[[noreturn]] void Fatal(const char* fmt, ...);

} // namespace swrots

#define LOG_DEBUG(...) ::swrots::LogWrite(::swrots::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::swrots::LogWrite(::swrots::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::swrots::LogWrite(::swrots::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::swrots::LogWrite(::swrots::LogLevel::Error, __VA_ARGS__)
