// PlayStation controllers through raw HID reports.
//
// Report layouts (input report 0x01 over USB; over Bluetooth the full report
// is 0x11 (DualShock 4) / 0x31 (DualSense), enabled by reading a feature
// report, with the same fields shifted by 2 / 1 bytes):
//   DualShock 4: 1 LX, 2 LY, 3 RX, 4 RY, 5 hat + face buttons, 6 shoulders/
//     share/options/sticks, 7 PS/touchpad, 8 L2, 9 R2
//   DualSense:   1 LX, 2 LY, 3 RX, 4 RY, 5 L2, 6 R2, 8 hat + face buttons,
//     9 shoulders/create/options/sticks, 10 PS/touchpad
// Face buttons: bit 4 square, 5 cross, 6 circle, 7 triangle; hat 0-7 from up
// clockwise, 8 = centered.

#include "input/playstation.h"

#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"

namespace swrots::input {

namespace {

enum : WORD {
    kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008,
    kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080,
};
enum { kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger };

constexpr WORD kSony = 0x054C;

enum class Model { DualShock4, DualSense };

struct Pad {
    std::wstring path;
    Model model;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool bluetooth = false;
    std::atomic<bool> alive = true;
    std::mutex lock;
    PadState state;
    WORD rumbleLeft = 0, rumbleRight = 0;
    std::thread reader;
};

std::mutex g_PadsLock;
std::vector<std::shared_ptr<Pad>> g_Pads;
bool g_Started = false;

uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc = 0xFFFFFFFFu)
{
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return crc;
}

// Bluetooth output reports end with a CRC-32 of 0xA2 (the HID "output" header) + the report.
void AppendBluetoothCrc(uint8_t* report, size_t size)
{
    const uint8_t header = 0xA2;
    uint32_t crc = Crc32(&header, 1);
    crc = ~Crc32(report, size - 4, crc);
    memcpy(report + size - 4, &crc, 4);
}

SHORT Axis(uint8_t v, bool invert)
{
    int x = (int(v) - 128) * 256;
    if (invert)
        x = -x - 1;
    return SHORT(std::clamp(x, -32768, 32767));
}

void Decode(const uint8_t* d, bool dualSense, PadState& s)
{
    s = {};
    s.lx = Axis(d[1], false);
    s.ly = Axis(d[2], true);
    s.rx = Axis(d[3], false);
    s.ry = Axis(d[4], true);
    const uint8_t face = dualSense ? d[8] : d[5], misc = dualSense ? d[9] : d[6];
    s.analog[kLeftTrigger] = dualSense ? d[5] : d[8];
    s.analog[kRightTrigger] = dualSense ? d[6] : d[9];
    const uint8_t system = dualSense ? d[10] : d[7];
    static const WORD kHat[8] = { kDpadUp, kDpadUp | kDpadRight, kDpadRight, kDpadDown | kDpadRight, kDpadDown,
        kDpadDown | kDpadLeft, kDpadLeft, kDpadUp | kDpadLeft };
    if ((face & 0x0F) < 8)
        s.buttons |= kHat[face & 0x0F];
    s.analog[kX] = (face & 0x10) ? 0xFF : 0; // square
    s.analog[kA] = (face & 0x20) ? 0xFF : 0; // cross
    s.analog[kB] = (face & 0x40) ? 0xFF : 0; // circle
    s.analog[kY] = (face & 0x80) ? 0xFF : 0; // triangle
    s.analog[kWhite] = (misc & 0x01) ? 0xFF : 0; // L1
    s.analog[kBlack] = (misc & 0x02) ? 0xFF : 0; // R1
    if (misc & 0x10) s.buttons |= kBack;         // share / create
    if (misc & 0x20) s.buttons |= kStart;        // options
    if (misc & 0x40) s.buttons |= kLeftThumb;
    if (misc & 0x80) s.buttons |= kRightThumb;
    if (system & 0x02) s.buttons |= kBack;       // touchpad click
}

bool Io(HANDLE file, bool write, void* data, DWORD size, DWORD* done, DWORD timeoutMs)
{
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = write ? WriteFile(file, data, size, nullptr, &ov) : ReadFile(file, data, size, nullptr, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, timeoutMs) != WAIT_OBJECT_0) {
            CancelIoEx(file, &ov);
            GetOverlappedResult(file, &ov, done, TRUE);
            CloseHandle(ov.hEvent);
            return false;
        }
        ok = TRUE;
    }
    DWORD n = 0;
    ok = ok && GetOverlappedResult(file, &ov, &n, FALSE);
    CloseHandle(ov.hEvent);
    if (done)
        *done = n;
    return ok != FALSE;
}

void SendRumble(Pad& pad)
{
    uint8_t report[78] = {};
    size_t size;
    const uint8_t strong = uint8_t(pad.rumbleLeft >> 8), weak = uint8_t(pad.rumbleRight >> 8);
    if (pad.model == Model::DualShock4) {
        uint8_t* p = report;
        if (pad.bluetooth) {
            report[0] = 0x11;
            report[1] = 0xC0; // HID + CRC
            p = report + 2;
            size = 78;
        } else {
            report[0] = 0x05;
            size = 32;
        }
        p[1] = 0x07; // rumble + lightbar
        p[4] = weak;
        p[5] = strong;
        p[6] = 0x00; p[7] = 0x00; p[8] = 0x40; // lightbar: blue
    } else {
        uint8_t* p = report;
        if (pad.bluetooth) {
            report[0] = 0x31;
            report[1] = 0x02;
            p = report + 1;
            size = 78;
        } else {
            report[0] = 0x02;
            size = 48;
        }
        p[1] = 0x03; // compatible vibration + haptics select
        p[3] = weak;
        p[4] = strong;
    }
    if (pad.bluetooth)
        AppendBluetoothCrc(report, size);
    Io(pad.file, true, report, DWORD(size), nullptr, 100);
}

void ReadLoop(std::shared_ptr<Pad> pad)
{
    HIDP_CAPS caps = {};
    PHIDP_PREPARSED_DATA pre = nullptr;
    if (HidD_GetPreparsedData(pad->file, &pre)) {
        HidP_GetCaps(pre, &caps);
        HidD_FreePreparsedData(pre);
    }
    // Reading the calibration feature report switches Bluetooth controllers to full reports.
    uint8_t feature[64] = { uint8_t(pad->model == Model::DualShock4 ? 0x02 : 0x05) };
    HidD_GetFeature(pad->file, feature, sizeof(feature));

    std::vector<uint8_t> buf(std::max<USHORT>(caps.InputReportByteLength, 64) + 16);
    int failures = 0;
    while (pad->alive) {
        DWORD n = 0;
        if (!Io(pad->file, false, buf.data(), DWORD(buf.size()), &n, 500)) {
            if (GetLastError() == ERROR_DEVICE_NOT_CONNECTED || ++failures > 20)
                break;
            continue;
        }
        failures = 0;
        const bool ds = pad->model == Model::DualSense;
        const uint8_t* d = nullptr;
        if (buf[0] == 0x01 && n >= 11)
            d = buf.data(); // USB, or Bluetooth before full reports
        else if (buf[0] == 0x11 && !ds && n >= 12)
            d = buf.data() + 2, pad->bluetooth = true;
        else if (buf[0] == 0x31 && ds && n >= 12)
            d = buf.data() + 1, pad->bluetooth = true;
        if (!d)
            continue;
        PadState s;
        // Bluetooth DualSense before full reports uses the DualShock 4 layout.
        Decode(d, ds && !(buf[0] == 0x01 && pad->bluetooth), s);
        std::lock_guard<std::mutex> g(pad->lock);
        pad->state = s;
    }
    pad->alive = false;
}

bool IsPlayStationPad(WORD vendor, WORD product, Model& model)
{
    if (vendor != kSony)
        return false;
    switch (product) {
    case 0x05C4: case 0x09CC: case 0x0BA0: model = Model::DualShock4; return true;
    case 0x0CE6: case 0x0DF2: model = Model::DualSense; return true;
    }
    return false;
}

void Scan()
{
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return;
    SP_DEVICE_INTERFACE_DATA iface = { sizeof(iface) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &iface); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        std::vector<uint8_t> detailBuf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, nullptr))
            continue;
        const std::wstring path = detail->DevicePath;
        {
            std::lock_guard<std::mutex> g(g_PadsLock);
            if (std::any_of(g_Pads.begin(), g_Pads.end(), [&](auto& p) { return p->path == path; }))
                continue;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        HIDD_ATTRIBUTES attrs = { sizeof(attrs) };
        Model model;
        if (!HidD_GetAttributes(file, &attrs) || !IsPlayStationPad(attrs.VendorID, attrs.ProductID, model)) {
            CloseHandle(file);
            continue;
        }
        auto pad = std::make_shared<Pad>();
        pad->path = path;
        pad->model = model;
        pad->file = file;
        pad->bluetooth = path.find(L"{00001124-0000-1000-8000-00805f9b34fb}") != std::wstring::npos;
        LOG_INFO("Controller connected: %s (%s)", model == Model::DualSense ? "DualSense" : "DualShock 4",
            pad->bluetooth ? "Bluetooth" : "USB");
        pad->reader = std::thread(ReadLoop, pad);
        std::lock_guard<std::mutex> g(g_PadsLock);
        g_Pads.push_back(pad);
    }
    SetupDiDestroyDeviceInfoList(set);
}

void RemoveDisconnected()
{
    std::vector<std::shared_ptr<Pad>> gone;
    {
        std::lock_guard<std::mutex> g(g_PadsLock);
        auto it = std::partition(g_Pads.begin(), g_Pads.end(), [](auto& p) { return p->alive.load(); });
        gone.assign(it, g_Pads.end());
        g_Pads.erase(it, g_Pads.end());
    }
    for (auto& p : gone) {
        LOG_INFO("Controller disconnected");
        if (p->reader.joinable())
            p->reader.join();
        CloseHandle(p->file);
    }
}

} // namespace

void StartPlayStationPads()
{
    if (g_Started)
        return;
    g_Started = true;
    std::thread([] {
        for (;;) {
            RemoveDisconnected();
            Scan();
            Sleep(2000);
        }
    }).detach();
}

int PlayStationPadCount()
{
    std::lock_guard<std::mutex> g(g_PadsLock);
    return int(std::count_if(g_Pads.begin(), g_Pads.end(), [](auto& p) { return p->alive.load(); }));
}

bool ReadPlayStationPad(int n, PadState& state)
{
    std::lock_guard<std::mutex> g(g_PadsLock);
    for (auto& p : g_Pads) {
        if (!p->alive)
            continue;
        if (n-- == 0) {
            std::lock_guard<std::mutex> pg(p->lock);
            state = p->state;
            return true;
        }
    }
    return false;
}

void SetPlayStationRumble(int n, WORD left, WORD right)
{
    std::shared_ptr<Pad> pad;
    {
        std::lock_guard<std::mutex> g(g_PadsLock);
        for (auto& p : g_Pads)
            if (p->alive && n-- == 0) {
                pad = p;
                break;
            }
    }
    if (!pad || (pad->rumbleLeft == left && pad->rumbleRight == right))
        return;
    pad->rumbleLeft = left;
    pad->rumbleRight = right;
    SendRumble(*pad);
}

} // namespace swrots::input
