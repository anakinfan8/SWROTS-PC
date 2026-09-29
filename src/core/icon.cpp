#include "core/icon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "core/xbe.h"

namespace swrots {

namespace {

// $$XTIMAGE holds an XPR resource: a 32-byte header (magic, total size,
// header size, then a D3D texture header with Format at +24) followed by the
// texture data -- a DXT1 image.
constexpr uint32_t kFormatDxt1 = 0x0C;

uint32_t Expand565(uint16_t c)
{
    const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

uint32_t Mix(uint32_t a, uint32_t b, int wa, int wb, int div)
{
    uint32_t out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
        out |= ((ca * wa + cb * wb + div / 2) / div) << shift;
    }
    return out;
}

// Area-averaged resize of a square BGRA image (alpha-weighted, so transparent
// pixels do not darken the edges).
std::vector<uint32_t> Resize(const std::vector<uint32_t>& src, int srcSize, int size)
{
    std::vector<uint32_t> dst(size_t(size) * size);
    const double scale = double(srcSize) / size;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double x0 = x * scale, x1 = (x + 1) * scale, y0 = y * scale, y1 = (y + 1) * scale;
            double sum[4] = {}, weight = 0;
            for (int sy = int(y0); sy < int(std::ceil(y1)) && sy < srcSize; ++sy) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                for (int sx = int(x0); sx < int(std::ceil(x1)) && sx < srcSize; ++sx) {
                    const double w = wy * (std::min(x1, sx + 1.0) - std::max(x0, double(sx)));
                    const uint32_t p = src[size_t(sy) * srcSize + sx];
                    const double a = (p >> 24) / 255.0;
                    for (int c = 0; c < 3; ++c)
                        sum[c] += ((p >> (c * 8)) & 0xFF) * a * w;
                    sum[3] += a * w;
                    weight += w;
                }
            }
            const double alpha = weight ? sum[3] / weight : 0;
            uint32_t p = uint32_t(alpha * 255 + 0.5) << 24;
            for (int c = 0; c < 3; ++c)
                p |= uint32_t(std::clamp(sum[3] ? sum[c] / sum[3] : 0.0, 0.0, 255.0) + 0.5) << (c * 8);
            dst[size_t(y) * size + x] = p;
        }
    }
    return dst;
}

// The emblem centered on a transparent square canvas, with a small margin.
std::vector<uint32_t> IconImage(const XbeFile& xbe, int size)
{
    int w = 0, h = 0;
    std::vector<uint32_t> emblem = DecodeTitleImage(xbe, w, h);
    if (emblem.empty() || w != h)
        return {};
    const int inner = std::max(1, int(size * 0.9 + 0.5)), off = (size - inner) / 2;
    std::vector<uint32_t> scaled = Resize(emblem, w, inner), out(size_t(size) * size, 0);
    for (int y = 0; y < inner; ++y)
        std::memcpy(&out[size_t(y + off) * size + off], &scaled[size_t(y) * inner], inner * 4);
    return out;
}

HICON CreateIconFromPixels(const std::vector<uint32_t>& pixels, int size)
{
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color)
        return nullptr;
    std::memcpy(bits, pixels.data(), pixels.size() * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

} // namespace

std::vector<uint32_t> DecodeTitleImage(const XbeFile& xbe, int& width, int& height)
{
    width = height = 0;
    const XbeSectionHeader* sections = xbe.Sections();
    for (uint32_t i = 0; i < xbe.Header().NumberOfSections; ++i) {
        const XbeSectionHeader& s = sections[i];
        if (xbe.SectionName(s) != "$$XTIMAGE" || s.RawSize < 32)
            continue;
        const uint8_t* d = xbe.RawSectionData(s);
        uint32_t headerSize, format;
        std::memcpy(&headerSize, d + 8, 4);
        std::memcpy(&format, d + 24, 4);
        const int w = 1 << ((format >> 20) & 0xF), h = 1 << ((format >> 24) & 0xF);
        if (((format >> 8) & 0xFF) != kFormatDxt1 || headerSize + size_t(w) * h / 2 > s.RawSize || w < 4 || h < 4)
            return {};
        std::vector<uint32_t> pixels(size_t(w) * h);
        const uint8_t* block = d + headerSize;
        for (int by = 0; by < h / 4; ++by) {
            for (int bx = 0; bx < w / 4; ++bx, block += 8) {
                const uint16_t c0 = uint16_t(block[0] | block[1] << 8), c1 = uint16_t(block[2] | block[3] << 8);
                uint32_t palette[4] = { Expand565(c0) | 0xFF000000, Expand565(c1) | 0xFF000000 };
                if (c0 > c1) {
                    palette[2] = Mix(palette[0], palette[1], 2, 1, 3) | 0xFF000000;
                    palette[3] = Mix(palette[0], palette[1], 1, 2, 3) | 0xFF000000;
                } else {
                    palette[2] = Mix(palette[0], palette[1], 1, 1, 2) | 0xFF000000;
                    palette[3] = 0; // transparent
                }
                for (int row = 0; row < 4; ++row)
                    for (int col = 0; col < 4; ++col)
                        pixels[size_t(by * 4 + row) * w + bx * 4 + col] = palette[(block[4 + row] >> (col * 2)) & 3];
            }
        }
        width = w;
        height = h;
        return pixels;
    }
    return {};
}

void SetWindowIconFromXbe(HWND window, const XbeFile& xbe)
{
    const struct {
        WPARAM which;
        int metric;
    } kinds[] = { { ICON_BIG, SM_CXICON }, { ICON_SMALL, SM_CXSMICON } };
    for (const auto& k : kinds) {
        // Size for the window's DPI (title bar and taskbar scale with it).
        const UINT dpi = GetDpiForWindow(window);
        const int size = GetSystemMetricsForDpi(k.metric, dpi ? dpi : 96);
        std::vector<uint32_t> pixels = IconImage(xbe, size);
        if (pixels.empty())
            return;
        if (HICON icon = CreateIconFromPixels(pixels, size))
            SendMessageW(window, WM_SETICON, k.which, reinterpret_cast<LPARAM>(icon));
    }
}

bool WriteIconFile(const XbeFile& xbe, const std::wstring& path)
{
    const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
    std::vector<std::vector<uint8_t>> images;
    for (int size : sizes) {
        std::vector<uint32_t> pixels = IconImage(xbe, size);
        if (pixels.empty())
            return false;
        // Icon image: BITMAPINFOHEADER (double height: color + mask), bottom-up
        // 32-bit pixels, then a 1-bit AND mask (all zero; alpha decides).
        const size_t maskStride = ((size + 31) / 32) * 4;
        std::vector<uint8_t> img(sizeof(BITMAPINFOHEADER) + pixels.size() * 4 + maskStride * size, 0);
        BITMAPINFOHEADER bih = {};
        bih.biSize = sizeof(bih);
        bih.biWidth = size;
        bih.biHeight = size * 2;
        bih.biPlanes = 1;
        bih.biBitCount = 32;
        std::memcpy(img.data(), &bih, sizeof(bih));
        for (int y = 0; y < size; ++y)
            std::memcpy(img.data() + sizeof(bih) + size_t(size - 1 - y) * size * 4, &pixels[size_t(y) * size], size * 4);
        images.push_back(std::move(img));
    }
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f)
        return false;
    const uint16_t header[3] = { 0, 1, uint16_t(images.size()) };
    fwrite(header, sizeof(header), 1, f);
    uint32_t offset = 6 + 16 * uint32_t(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const uint8_t dim = uint8_t(sizes[i] == 256 ? 0 : sizes[i]);
        const uint8_t entry[8] = { dim, dim, 0, 0, 1, 0, 32, 0 };
        const uint32_t bytes = uint32_t(images[i].size());
        fwrite(entry, 8, 1, f);
        fwrite(&bytes, 4, 1, f);
        fwrite(&offset, 4, 1, f);
        offset += bytes;
    }
    for (const auto& img : images)
        fwrite(img.data(), img.size(), 1, f);
    const bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

} // namespace swrots
