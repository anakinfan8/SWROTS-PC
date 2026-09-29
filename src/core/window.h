#pragma once
#include <windows.h>

#include <string>

namespace swrots {

class XbeFile;

// The window is two windows in one process: the frame -- the top-level window with the
// title bar, taskbar button, position and fullscreen -- and the game's child window
// filling its client area, which the game draws into and which takes the keyboard and
// mouse. The game's reboots (level changes, restarts) happen in-process
// (kernel/reboot.cpp), so the window stays.

// Creates and shows the frame window.
HWND CreateMainWindow(const XbeFile& xbe);

// Creates the game's child window inside the frame.
HWND CreateGameWindow(bool fullscreen);
HWND GameWindow();
// The game presented a frame (the window no longer needs painting black).
void NoteGameFramePresented();
// True while the game's window is the active one (keyboard input goes to it).
bool GameWindowActive();
// Mouse buttons count as game input only while the mouse is captured, and not
// the click that captured it (or activated the window) until it is released --
// clicks on the title bar, its buttons or the frame never reach the game.
bool MouseButtonsAreGameInput();
[[noreturn]] void RunMessageLoop();

} // namespace swrots
