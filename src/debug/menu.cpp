#include "debug/menu.h"

#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "core/log.h"
#include "debug/console.h"
#include "game/game.h"
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace swrots::debug {

namespace {

UINT g_ToggleKey = VK_OEM_3; // the ~ / ` key on US layouts
std::string g_KeyName = "~";
bool g_SwallowChar = false;  // window thread: the character the toggle key types

bool g_Enabled = false;
std::atomic<bool> g_Open = false;
bool g_Ready = false;       // ImGui backends initialized (game thread)
bool g_JustOpened = false;  // game thread: focus the console input

struct QueuedMessage {
    HWND hwnd;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
};
std::mutex g_MessageLock;
std::vector<QueuedMessage> g_Messages;
std::atomic<bool> g_OpenedSinceLastFrame = false;

bool IsInputMessage(UINT msg)
{
    switch (msg) {
    case WM_MOUSEMOVE: case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR:
        return true;
    }
    return false;
}

// The options object: [[kEngineServices] + 0x38] + 8.
uint8_t* Options()
{
    auto* services = *reinterpret_cast<uint8_t**>(uintptr_t(game::kEngineServices));
    auto* core = services ? *reinterpret_cast<uint8_t**>(services + 0x38) : nullptr;
    return core ? *reinterpret_cast<uint8_t**>(core + 8) : nullptr;
}

// A checkbox bound to an options byte; `inverted` shows the opposite of the byte.
void OptionCheckbox(const char* label, uint32_t offset, bool inverted, const char* tooltip)
{
    uint8_t* options = Options();
    if (!options) {
        ImGui::BeginDisabled();
        bool dummy = false;
        ImGui::Checkbox(label, &dummy);
        ImGui::EndDisabled();
        return;
    }
    bool value = (options[offset] != 0) != inverted;
    if (ImGui::Checkbox(label, &value))
        options[offset] = uint8_t(value != inverted);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
}

// --- Console tab -------------------------------------------------------------------------------

std::vector<ConsoleLine> g_Log;
size_t g_LogCount = 0;
char g_Input[512] = "";
std::vector<std::string> g_History;
int g_HistoryPos = -1;
bool g_ScrollToBottom = false;

int HistoryCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || g_History.empty())
        return 0;
    const int previous = g_HistoryPos;
    if (data->EventKey == ImGuiKey_UpArrow)
        g_HistoryPos = g_HistoryPos == -1 ? int(g_History.size()) - 1 : std::max(0, g_HistoryPos - 1);
    else if (data->EventKey == ImGuiKey_DownArrow && g_HistoryPos != -1)
        g_HistoryPos = g_HistoryPos + 1 >= int(g_History.size()) ? -1 : g_HistoryPos + 1;
    if (previous != g_HistoryPos) {
        data->DeleteChars(0, data->BufTextLen);
        if (g_HistoryPos != -1)
            data->InsertChars(0, g_History[g_HistoryPos].c_str());
    }
    return 0;
}

void Submit(const std::string& line)
{
    if (line.empty())
        return;
    if (g_History.empty() || g_History.back() != line)
        g_History.push_back(line);
    g_HistoryPos = -1;
    QueueConsoleCommand(line);
    g_ScrollToBottom = true;
}

void ConsoleTab()
{
    const size_t before = g_LogCount;
    if (ConsoleCleared())
        g_Log.clear();
    g_LogCount = CopyConsoleLines(g_LogCount, g_Log);
    if (g_LogCount != before)
        g_ScrollToBottom = true;

    if (ImGui::Button("help"))
        Submit("help");
    ImGui::SameLine();
    if (ImGui::Button("listvars"))
        Submit("listvars");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        ClearConsole();
        g_Log.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("set <variable> <value>, e.g. set god true");

    const float inputHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("log", ImVec2(0, -inputHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(int(g_Log.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const ConsoleLine& line = g_Log[i];
                ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                switch (line.kind) {
                case LineKind::Error: color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); break;
                case LineKind::Engine: color = ImVec4(1.0f, 0.85f, 0.45f, 1.0f); break;
                case LineKind::Port: color = ImVec4(0.65f, 0.65f, 0.65f, 1.0f); break;
                case LineKind::Output: break;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(line.text.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (g_ScrollToBottom)
            ImGui::SetScrollHereY(1.0f);
        g_ScrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (g_JustOpened)
        ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("##input", g_Input, sizeof(g_Input),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, HistoryCallback)) {
        Submit(g_Input);
        g_Input[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
}

// --- Switches tab ------------------------------------------------------------------------------

void SwitchesTab()
{
    ImGui::SeparatorText("Debug displays");
    OptionCheckbox("Show debug displays", game::kOptionsHideDebugDisplays, true,
        "Lets the engine draw its debug displays (options +0xD7 cleared).\nSame as [Debug] DebugDisplays=1.");
    OptionCheckbox("FPS counter (fps)", game::kOptionsFps, false, "Needs the debug displays.");
    ImGui::SeparatorText("Gameplay");
    OptionCheckbox("God mode (god)", game::kOptionsGod, false, "The player takes no damage.");
    OptionCheckbox("AI disabled (aiDisabled)", game::kOptionsAiDisabled, false, "Characters stop acting.");
    if (!Options())
        ImGui::TextDisabled("The engine's options do not exist yet.");
}

// Square corners, in the colours of the Slayer engine's own debug windows (Indiana Jones and
// Marc Ecko's Getting Up): white text, grey title bars, a dark see-through body.
void ApplyTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = style.FrameRounding = style.TabRounding = style.ScrollbarRounding = style.GrabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f; // checkboxes and input boxes stand out from the dark body
    ImVec4* c = style.Colors;
    const ImVec4 white(0.92f, 0.92f, 0.92f, 1.0f), grey(0.45f, 0.45f, 0.45f, 1.0f);
    const ImVec4 bar(0.2f, 0.2f, 0.2f, 0.95f), hover(0.3f, 0.3f, 0.3f, 0.9f), active(0.4f, 0.4f, 0.4f, 0.9f);
    c[ImGuiCol_Text] = white;
    c[ImGuiCol_TextDisabled] = grey;
    c[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.75f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    c[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.95f);
    c[ImGuiCol_Border] = ImVec4(0.55f, 0.55f, 0.55f, 0.8f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.9f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.26f, 0.26f, 0.9f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.34f, 0.34f, 0.9f);
    c[ImGuiCol_TitleBg] = bar;
    c[ImGuiCol_TitleBgActive] = ImVec4(0.27f, 0.27f, 0.27f, 0.95f);
    c[ImGuiCol_TitleBgCollapsed] = bar;
    c[ImGuiCol_Button] = bar;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = active;
    c[ImGuiCol_Header] = bar;
    c[ImGuiCol_HeaderHovered] = hover;
    c[ImGuiCol_HeaderActive] = active;
    c[ImGuiCol_Tab] = bar;
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = ImVec4(0.35f, 0.35f, 0.35f, 0.95f);
    c[ImGuiCol_CheckMark] = white;
    c[ImGuiCol_SliderGrab] = white;
    c[ImGuiCol_Separator] = ImVec4(0.6f, 0.6f, 0.6f, 0.8f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.4f, 0.4f, 0.4f, 0.8f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.5f, 0.5f, 0.5f, 0.3f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
}

// The menu's pixel font is only sharp at whole multiples of its 13 px, rendered at that size
// (stretching it with FontGlobalScale blurs it): 1x up to about 1440 lines, then 2x, 3x.
int g_Scale = 0;
ImGuiStyle g_BaseStyle;
HWND g_MenuWindow = nullptr;

void UpdateScale()
{
    RECT client = {};
    GetClientRect(g_MenuWindow, &client);
    const float height = float(client.bottom - client.top);
    const int scale = std::clamp(int(height / 720.0f + 0.25f), 1, 4);
    if (scale == g_Scale)
        return;
    g_Scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig font;
    font.SizePixels = 13.0f * float(scale);
    io.Fonts->AddFontDefault(&font);
    ImGui_ImplDX9_InvalidateDeviceObjects(); // the font texture is rebuilt by the next NewFrame
    ImGui::GetStyle() = g_BaseStyle;
    ImGui::GetStyle().ScaleAllSizes(float(scale));
}

void BuildMenu()
{
    // The default layout, applied again whenever the game window changes size (the menu
    // follows it); in between the player can move and resize the menu.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    static ImVec2 laidOutFor(0, 0);
    const bool resized = viewport->WorkSize.x != laidOutFor.x || viewport->WorkSize.y != laidOutFor.y;
    laidOutFor = viewport->WorkSize;
    const ImGuiCond cond = resized ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20), cond);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.6f, viewport->WorkSize.y * 0.6f), cond);
    ImGui::SetNextWindowBgAlpha(0.9f);
    // The engine's own source path for its console, as the title.
    if (ImGui::Begin("CONSOLE  (F:\\Slayer\\ENGINE\\EConsole\\console.cpp)###debugmenu")) {
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Console")) {
                ConsoleTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Switches")) {
                SwitchesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    g_JustOpened = false;
}

// Key names for [Debug] MenuKey.
UINT ParseKey(const wchar_t* name, std::string& shown)
{
    std::wstring k = name ? name : L"";
    while (!k.empty() && iswspace(k.back()))
        k.pop_back();
    while (!k.empty() && iswspace(k.front()))
        k.erase(0, 1);
    for (wchar_t& c : k)
        c = wchar_t(towupper(c));
    shown.clear();
    for (wchar_t c : k)
        shown += char(c < 128 ? c : '?');
    if (k.empty() || k == L"~" || k == L"`" || k == L"TILDE" || k == L"GRAVE") {
        shown = "~";
        return VK_OEM_3;
    }
    if (k.size() >= 2 && k[0] == L'F' && iswdigit(k[1])) {
        int n = _wtoi(k.c_str() + 1);
        if (n >= 1 && n <= 24)
            return UINT(VK_F1 + n - 1);
    }
    if (k.size() == 1 && (iswalpha(k[0]) || iswdigit(k[0])))
        return UINT(k[0]);
    struct Named { const wchar_t* name; UINT vk; };
    static const Named kNames[] = { { L"INSERT", VK_INSERT }, { L"DELETE", VK_DELETE }, { L"HOME", VK_HOME },
        { L"END", VK_END }, { L"PAGEUP", VK_PRIOR }, { L"PAGEDOWN", VK_NEXT }, { L"PAUSE", VK_PAUSE },
        { L"SCROLLLOCK", VK_SCROLL }, { L"BACKSLASH", VK_OEM_5 } };
    for (const Named& n : kNames)
        if (k == n.name)
            return n.vk;
    LOG_WARN("Unknown MenuKey '%ls'; using ~", name);
    shown = "~";
    return VK_OEM_3;
}

} // namespace

void ConfigureMenu(bool enabled, const wchar_t* key)
{
    g_Enabled = enabled;
    g_ToggleKey = ParseKey(key, g_KeyName);
    if (enabled)
        LOG_INFO("Debug menu enabled (%s)", g_KeyName.c_str());
}

bool MenuOpen() { return g_Open; }

bool MenuWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    if (!g_Enabled)
        return false;
    if (msg == WM_KEYDOWN && wParam == g_ToggleKey) {
        if (!(lParam & (1 << 30))) { // not an auto-repeat
            g_Open = !g_Open;
            if (g_Open)
                g_OpenedSinceLastFrame = true;
        }
        g_SwallowChar = true; // TranslateMessage already queued the key's character
        *result = 0;
        return true;
    }
    if (msg == WM_CHAR && g_SwallowChar) {
        g_SwallowChar = false;
        *result = 0;
        return true;
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
        g_SwallowChar = false;
    if (!g_Open)
        return false;
    if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        *result = TRUE;
        return true;
    }
    if (!IsInputMessage(msg))
        return false;
    {
        std::lock_guard<std::mutex> lock(g_MessageLock);
        g_Messages.push_back({ hwnd, msg, wParam, lParam });
    }
    *result = 0;
    return true;
}

void MenuDeviceCreated(IDirect3DDevice9* device, HWND window)
{
    if (!g_Enabled)
        return;
    if (!ImGui::GetCurrentContext()) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // the window thread sets the cursor
        ApplyTheme();
        g_BaseStyle = ImGui::GetStyle();
        ImGui_ImplWin32_Init(window);
    }
    ImGui_ImplDX9_Init(device);
    g_MenuWindow = window;
    g_Ready = true;
}

void MenuDeviceReleasing()
{
    if (g_Ready)
        ImGui_ImplDX9_Shutdown();
    g_Ready = false;
}

void RenderMenu()
{
    if (!g_Ready || !g_Open)
        return;
    if (g_OpenedSinceLastFrame.exchange(false)) {
        g_JustOpened = true;
        ImGui::GetIO().ClearInputKeys(); // keys released while the menu was closed
    }

    std::vector<QueuedMessage> messages;
    {
        std::lock_guard<std::mutex> lock(g_MessageLock);
        messages.swap(g_Messages);
    }
    for (const QueuedMessage& m : messages)
        ImGui_ImplWin32_WndProcHandler(m.hwnd, m.msg, m.wParam, m.lParam);

    UpdateScale();
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    BuildMenu();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void MenuAfterFrame()
{
    RunQueuedConsoleCommands();
}

} // namespace swrots::debug
