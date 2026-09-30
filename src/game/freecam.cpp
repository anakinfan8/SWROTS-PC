// Free camera: detaches the view from the game's camera and flies it with the keyboard, mouse or a
// controller, while the player's input is held back. The game keeps running in whatever state it is
// in (set timeScale 0 to freeze it, hud 0 to hide the HUD).

#include "game/freecam.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "core/patch.h"
#include "input/controls.h"

namespace swrots::game {

namespace {

// The master camera (IMasterCamera.cpp, IMasterCameraVader.cpp: the one camera that drives the view;
// gameplay, duel and scripted cameras feed it) hands its world transform over every frame through
// SetTransform (vtable +0x1F4, thiscall (const Matrix*), ret 4), which only the two master camera
// classes use. The matrix's rows are the camera's right, up and forward axes and its position; the
// world's up is +Y, and right x up = forward (as in Direct3D). It is called while the game is frozen
// (timeScale 0) too. The engine's own "debugCamera" variable swaps in a fixed position aimed at the
// player here (0x12A188).
constexpr uint32_t kSetTransform = 0x00129990;
constexpr uint8_t kSetTransformPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x4C };

struct Matrix {
    float m[4][4];
};

struct Vec3 {
    float x, y, z;
};

Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

using SetTransformFn = void(__fastcall*)(void* camera, void* edx, const Matrix* transform);
SetTransformFn g_OriginalSetTransform = nullptr;

// Speeds in world units (a character is roughly 180 tall) per second.
constexpr float kBaseSpeed = 400.0f;
constexpr float kSpeedStep = 1.25f;         // per wheel notch or D-pad press
constexpr float kMouseTurn = 0.0025f;       // radians per mouse count
constexpr float kStickTurn = 2.0f;          // radians per second at full deflection
constexpr float kPitchLimit = 1.55f;        // just short of straight up or down

std::atomic<bool> g_Wanted = false; // the console's choice
bool g_Flying = false;              // taken over from the game's camera (game thread)
bool g_HaveGameView = false;
Matrix g_GameView = {};             // the game camera's latest transform
Vec3 g_Position = {};
float g_Yaw = 0.0f, g_Pitch = 0.0f;
float g_Speed = kBaseSpeed;
LARGE_INTEGER g_LastTick = {};

// Starts from the game camera's view.
void TakeOver()
{
    const float(*m)[4] = g_GameView.m;
    g_Position = { m[3][0], m[3][1], m[3][2] };
    g_Yaw = std::atan2(m[2][0], m[2][2]);
    g_Pitch = std::asin(std::clamp(m[2][1], -1.0f, 1.0f));
    QueryPerformanceCounter(&g_LastTick);
    g_Flying = true;
    input::HoldPlayerInput(true);
    LOG_INFO("Free camera: on at %.0f %.0f %.0f", g_Position.x, g_Position.y, g_Position.z);
}

void Fly(Matrix& out)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float dt = std::min(float(now.QuadPart - g_LastTick.QuadPart) / float(frequency.QuadPart), 0.1f);
    g_LastTick = now;

    const input::FreeCameraControls c = input::ReadFreeCameraControls();
    if (c.speedSteps)
        g_Speed = std::clamp(g_Speed * std::pow(kSpeedStep, float(c.speedSteps)), kBaseSpeed / 20.0f, kBaseSpeed * 20.0f);
    g_Yaw += c.mouseDx * kMouseTurn + c.lookX * kStickTurn * dt;
    g_Pitch = std::clamp(g_Pitch - c.mouseDy * kMouseTurn + c.lookY * kStickTurn * dt, -kPitchLimit, kPitchLimit);

    const float cp = std::cos(g_Pitch), sp = std::sin(g_Pitch), cy = std::cos(g_Yaw), sy = std::sin(g_Yaw);
    const Vec3 forward = { cp * sy, sp, cp * cy };
    const Vec3 right = { cy, 0.0f, -sy };                 // world up x forward
    const Vec3 up = { -sp * sy, cp, -sp * cy };           // forward x right
    const float step = g_Speed * c.speed * dt;
    g_Position = g_Position + forward * (c.forward * step) + right * (c.right * step) + Vec3{ 0.0f, c.up * step, 0.0f };

    std::memset(&out, 0, sizeof(out));
    const Vec3 rows[4] = { right, up, forward, g_Position };
    for (int r = 0; r < 4; ++r) {
        out.m[r][0] = rows[r].x;
        out.m[r][1] = rows[r].y;
        out.m[r][2] = rows[r].z;
    }
    out.m[3][3] = 1.0f;
}

void __fastcall SetTransformHook(void* camera, void* edx, const Matrix* transform)
{
    const bool wanted = g_Wanted;
    static const bool log = GetEnvironmentVariableA("SWROTS_CAMERA_LOG", nullptr, 0) != 0;
    static DWORD lastLog = 0;
    if (log && transform && GetTickCount() - lastLog > 2000) {
        lastLog = GetTickCount();
        const float(*m)[4] = transform->m;
        LOG_INFO("Camera (game): fwd %.3f %.3f %.3f pos %.1f %.1f %.1f", m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]);
    }
    if (!g_Flying && transform) {
        g_GameView = *transform;
        g_HaveGameView = true;
    }
    if (wanted && !g_Flying && g_HaveGameView)
        TakeOver();
    else if (!wanted && g_Flying) {
        g_Flying = false;
        input::HoldPlayerInput(false);
        LOG_INFO("Free camera: off");
    }
    if (!g_Flying) {
        g_OriginalSetTransform(camera, edx, transform);
        return;
    }
    Matrix view;
    Fly(view);
    g_OriginalSetTransform(camera, edx, &view);
}

} // namespace

void InstallFreeCamera()
{
    // Every level change or restart reboots the game: the free camera is off again. Development aid:
    // SWROTS_FREECAM=1 turns it on from the start, for unattended tests.
    g_Wanted = GetEnvironmentVariableA("SWROTS_FREECAM", nullptr, 0) != 0;
    g_Flying = false;
    g_HaveGameView = false;
    input::HoldPlayerInput(false);
    if (std::memcmp(reinterpret_cast<const void*>(uintptr_t(kSetTransform)), kSetTransformPrologue,
            sizeof(kSetTransformPrologue)) != 0) {
        LOG_WARN("Free camera: the master camera's code is not as expected; no free camera");
        return;
    }
    uint8_t* stub = AllocStub(sizeof(kSetTransformPrologue) + 5);
    std::memcpy(stub, kSetTransformPrologue, sizeof(kSetTransformPrologue));
    stub[sizeof(kSetTransformPrologue)] = 0xE9;
    int32_t back = int32_t(kSetTransform + sizeof(kSetTransformPrologue)) -
        int32_t(uintptr_t(stub) + sizeof(kSetTransformPrologue) + 5);
    std::memcpy(stub + sizeof(kSetTransformPrologue) + 1, &back, 4);
    g_OriginalSetTransform = reinterpret_cast<SetTransformFn>(stub);
    PatchJump(kSetTransform, reinterpret_cast<const void*>(&SetTransformHook));
}

void SetFreeCamera(bool on)
{
    g_Wanted = on;
}

bool FreeCameraOn()
{
    return g_Wanted;
}

} // namespace swrots::game
