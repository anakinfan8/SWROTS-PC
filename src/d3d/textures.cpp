// Xbox textures -> D3D9 textures.
//
// Xbox texels are either swizzled (Morton order, power-of-two sizes), linear
// (pitched rows) or DXT-compressed. They are converted into host textures on
// first use and re-uploaded only when the game writes to their memory (tracked
// with write-watch on Xbox contiguous memory).

#include "d3d/d3d.h"

#include <cstring>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "d3d/recorder.h"
#include "kernel/mm.h"

namespace swrots::d3d {

using namespace xd3d;

namespace {

enum class Conv : uint8_t {
    None,       // byte-identical to the host format
    AL8,        // L8 used as luminance and alpha -> A8L8
    P8,         // palettized -> A8R8G8B8
    ABGR8,      // A8B8G8R8 -> A8R8G8B8
    BGRA8,      // B8G8R8A8 -> A8R8G8B8
    RGBA8,      // R8G8B8A8 -> A8R8G8B8
    RGBA5551,   // R5G5B5A1 -> A1R5G5B5
    RGBA4444,   // R4G4B4A4 -> A4R4G4B4
    G8B8,       // G8B8 -> A8R8G8B8 (g, b)
    R8B8,       // R8B8 -> A8R8G8B8 (r, b)
    R6G5B5,     // R6G5B5 -> A8R8G8B8
    YUY2,       // YUY2 -> A8R8G8B8
    UYVY,       // UYVY -> A8R8G8B8
};

struct FormatInfo {
    D3DFORMAT host;
    uint8_t bits; // source bits per texel
    bool swizzled;
    bool compressed;
    Conv conv;
};

bool GetFormatInfo(DWORD f, FormatInfo& fi)
{
    switch (f) {
    case FMT_L8: fi = { D3DFMT_L8, 8, true, false, Conv::None }; return true;
    case FMT_LIN_L8: fi = { D3DFMT_L8, 8, false, false, Conv::None }; return true;
    case FMT_AL8: fi = { D3DFMT_A8L8, 8, true, false, Conv::AL8 }; return true;
    case FMT_LIN_AL8: fi = { D3DFMT_A8L8, 8, false, false, Conv::AL8 }; return true;
    case FMT_A8: fi = { D3DFMT_A8, 8, true, false, Conv::None }; return true;
    case FMT_LIN_A8: fi = { D3DFMT_A8, 8, false, false, Conv::None }; return true;
    case FMT_P8: fi = { D3DFMT_A8R8G8B8, 8, true, false, Conv::P8 }; return true;
    case FMT_A1R5G5B5: fi = { D3DFMT_A1R5G5B5, 16, true, false, Conv::None }; return true;
    case FMT_LIN_A1R5G5B5: fi = { D3DFMT_A1R5G5B5, 16, false, false, Conv::None }; return true;
    case FMT_X1R5G5B5: fi = { D3DFMT_X1R5G5B5, 16, true, false, Conv::None }; return true;
    case FMT_LIN_X1R5G5B5: fi = { D3DFMT_X1R5G5B5, 16, false, false, Conv::None }; return true;
    case FMT_A4R4G4B4: fi = { D3DFMT_A4R4G4B4, 16, true, false, Conv::None }; return true;
    case FMT_LIN_A4R4G4B4: fi = { D3DFMT_A4R4G4B4, 16, false, false, Conv::None }; return true;
    case FMT_R5G6B5: fi = { D3DFMT_R5G6B5, 16, true, false, Conv::None }; return true;
    case FMT_LIN_R5G6B5: fi = { D3DFMT_R5G6B5, 16, false, false, Conv::None }; return true;
    case FMT_A8L8: fi = { D3DFMT_A8L8, 16, true, false, Conv::None }; return true;
    case FMT_LIN_A8L8: fi = { D3DFMT_A8L8, 16, false, false, Conv::None }; return true;
    case FMT_L16: fi = { D3DFMT_L16, 16, true, false, Conv::None }; return true;
    case FMT_LIN_L16: fi = { D3DFMT_L16, 16, false, false, Conv::None }; return true;
    case FMT_G8B8: fi = { D3DFMT_A8R8G8B8, 16, true, false, Conv::G8B8 }; return true;
    case FMT_LIN_G8B8: fi = { D3DFMT_A8R8G8B8, 16, false, false, Conv::G8B8 }; return true;
    case FMT_R8B8: fi = { D3DFMT_A8R8G8B8, 16, true, false, Conv::R8B8 }; return true;
    case FMT_LIN_R8B8: fi = { D3DFMT_A8R8G8B8, 16, false, false, Conv::R8B8 }; return true;
    case FMT_R6G5B5: fi = { D3DFMT_A8R8G8B8, 16, true, false, Conv::R6G5B5 }; return true;
    case FMT_LIN_R6G5B5: fi = { D3DFMT_A8R8G8B8, 16, false, false, Conv::R6G5B5 }; return true;
    case FMT_R5G5B5A1: fi = { D3DFMT_A1R5G5B5, 16, true, false, Conv::RGBA5551 }; return true;
    case FMT_LIN_R5G5B5A1: fi = { D3DFMT_A1R5G5B5, 16, false, false, Conv::RGBA5551 }; return true;
    case FMT_R4G4B4A4: fi = { D3DFMT_A4R4G4B4, 16, true, false, Conv::RGBA4444 }; return true;
    case FMT_LIN_R4G4B4A4: fi = { D3DFMT_A4R4G4B4, 16, false, false, Conv::RGBA4444 }; return true;
    case FMT_YUY2: fi = { D3DFMT_A8R8G8B8, 16, false, false, Conv::YUY2 }; return true;
    case FMT_UYVY: fi = { D3DFMT_A8R8G8B8, 16, false, false, Conv::UYVY }; return true;
    case FMT_A8R8G8B8: fi = { D3DFMT_A8R8G8B8, 32, true, false, Conv::None }; return true;
    case FMT_LIN_A8R8G8B8: fi = { D3DFMT_A8R8G8B8, 32, false, false, Conv::None }; return true;
    case FMT_X8R8G8B8: fi = { D3DFMT_X8R8G8B8, 32, true, false, Conv::None }; return true;
    case FMT_LIN_X8R8G8B8: fi = { D3DFMT_X8R8G8B8, 32, false, false, Conv::None }; return true;
    case FMT_A8B8G8R8: fi = { D3DFMT_A8R8G8B8, 32, true, false, Conv::ABGR8 }; return true;
    case FMT_LIN_A8B8G8R8: fi = { D3DFMT_A8R8G8B8, 32, false, false, Conv::ABGR8 }; return true;
    case FMT_B8G8R8A8: fi = { D3DFMT_A8R8G8B8, 32, true, false, Conv::BGRA8 }; return true;
    case FMT_LIN_B8G8R8A8: fi = { D3DFMT_A8R8G8B8, 32, false, false, Conv::BGRA8 }; return true;
    case FMT_R8G8B8A8: fi = { D3DFMT_A8R8G8B8, 32, true, false, Conv::RGBA8 }; return true;
    case FMT_LIN_R8G8B8A8: fi = { D3DFMT_A8R8G8B8, 32, false, false, Conv::RGBA8 }; return true;
    case FMT_V16U16: fi = { D3DFMT_V16U16, 32, true, false, Conv::None }; return true;
    case FMT_LIN_V16U16: fi = { D3DFMT_V16U16, 32, false, false, Conv::None }; return true;
    case FMT_DXT1: fi = { D3DFMT_DXT1, 4, false, true, Conv::None }; return true;
    case FMT_DXT3: fi = { D3DFMT_DXT3, 8, false, true, Conv::None }; return true;
    case FMT_DXT5: fi = { D3DFMT_DXT5, 8, false, true, Conv::None }; return true;
    }
    return false;
}

// Morton-order offset tables for one level.
void BuildSwizzleTables(UINT w, UINT h, std::vector<uint32_t>& xs, std::vector<uint32_t>& ys)
{
    xs.assign(w, 0);
    ys.assign(h, 0);
    uint32_t maskX = 0, maskY = 0, bit = 1, maskBit = 1;
    while (bit < w || bit < h) {
        if (bit < w) { maskX |= maskBit; maskBit <<= 1; }
        if (bit < h) { maskY |= maskBit; maskBit <<= 1; }
        bit <<= 1;
    }
    auto spread = [](uint32_t mask, uint32_t v) {
        uint32_t r = 0;
        for (uint32_t b = 1; v && b; b <<= 1)
            if (mask & b) {
                if (v & 1) r |= b;
                v >>= 1;
            }
        return r;
    };
    for (UINT x = 0; x < w; ++x) xs[x] = spread(maskX, x);
    for (UINT y = 0; y < h; ++y) ys[y] = spread(maskY, y);
}

inline uint8_t Expand(uint32_t v, int bits) { return uint8_t((v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1)); }

inline uint32_t YuvToArgb(int y, int u, int v)
{
    int c = y - 16, d = u - 128, e = v - 128;
    auto clamp = [](int x) { return uint32_t(x < 0 ? 0 : x > 255 ? 255 : x); };
    uint32_t r = clamp((298 * c + 409 * e + 128) >> 8);
    uint32_t g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
    uint32_t b = clamp((298 * c + 516 * d + 128) >> 8);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// Converts one row of `w` texels (already in linear order) to the host layout.
void ConvertRow(Conv conv, const uint8_t* src, uint8_t* dst, UINT w, const uint32_t* palette)
{
    switch (conv) {
    case Conv::None:
        break;
    case Conv::AL8:
        for (UINT x = 0; x < w; ++x) { dst[x * 2] = src[x]; dst[x * 2 + 1] = src[x]; }
        return;
    case Conv::P8:
        for (UINT x = 0; x < w; ++x) reinterpret_cast<uint32_t*>(dst)[x] = palette ? palette[src[x]] : 0xFFFF00FF;
        return;
    case Conv::ABGR8:
        for (UINT x = 0; x < w; ++x) {
            uint32_t v = reinterpret_cast<const uint32_t*>(src)[x];
            reinterpret_cast<uint32_t*>(dst)[x] = (v & 0xFF00FF00) | ((v & 0xFF) << 16) | ((v >> 16) & 0xFF);
        }
        return;
    case Conv::BGRA8:
        for (UINT x = 0; x < w; ++x) {
            uint32_t v = reinterpret_cast<const uint32_t*>(src)[x];
            reinterpret_cast<uint32_t*>(dst)[x] = _byteswap_ulong(v);
        }
        return;
    case Conv::RGBA8:
        for (UINT x = 0; x < w; ++x) {
            uint32_t v = reinterpret_cast<const uint32_t*>(src)[x]; // bytes: A B G R (little endian R8G8B8A8)
            reinterpret_cast<uint32_t*>(dst)[x] = (v >> 8) | (v << 24);
        }
        return;
    case Conv::RGBA5551:
        for (UINT x = 0; x < w; ++x) {
            uint16_t v = reinterpret_cast<const uint16_t*>(src)[x];
            reinterpret_cast<uint16_t*>(dst)[x] = uint16_t((v >> 1) | ((v & 1) << 15));
        }
        return;
    case Conv::RGBA4444:
        for (UINT x = 0; x < w; ++x) {
            uint16_t v = reinterpret_cast<const uint16_t*>(src)[x];
            reinterpret_cast<uint16_t*>(dst)[x] = uint16_t((v >> 4) | ((v & 0xF) << 12));
        }
        return;
    case Conv::G8B8:
        for (UINT x = 0; x < w; ++x) {
            uint8_t b = src[x * 2], g = src[x * 2 + 1];
            reinterpret_cast<uint32_t*>(dst)[x] = 0xFF000000 | (g << 8) | b;
        }
        return;
    case Conv::R8B8:
        for (UINT x = 0; x < w; ++x) {
            uint8_t b = src[x * 2], r = src[x * 2 + 1];
            reinterpret_cast<uint32_t*>(dst)[x] = 0xFF000000 | (r << 16) | b;
        }
        return;
    case Conv::R6G5B5:
        for (UINT x = 0; x < w; ++x) {
            uint16_t v = reinterpret_cast<const uint16_t*>(src)[x];
            reinterpret_cast<uint32_t*>(dst)[x] =
                0xFF000000 | (Expand(v >> 10, 6) << 16) | (Expand((v >> 5) & 31, 5) << 8) | Expand(v & 31, 5);
        }
        return;
    case Conv::YUY2:
    case Conv::UYVY:
        for (UINT x = 0; x + 1 < w; x += 2) {
            const uint8_t* p = src + x * 2;
            int y0, u, y1, v;
            if (conv == Conv::YUY2) { y0 = p[0]; u = p[1]; y1 = p[2]; v = p[3]; }
            else { u = p[0]; y0 = p[1]; v = p[2]; y1 = p[3]; }
            reinterpret_cast<uint32_t*>(dst)[x] = YuvToArgb(y0, u, v);
            reinterpret_cast<uint32_t*>(dst)[x + 1] = YuvToArgb(y1, u, v);
        }
        return;
    }
}

UINT HostBytesPerTexel(D3DFORMAT f)
{
    switch (f) {
    case D3DFMT_L8: case D3DFMT_A8: return 1;
    case D3DFMT_A8L8: case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_R5G6B5: case D3DFMT_L16: return 2;
    default: return 4;
    }
}

struct TextureDesc {
    FormatInfo fi;
    DWORD xboxFormat;
    UINT width, height, depth, levels;
    UINT pitch; // linear formats only
    bool cube;
    bool volume;
    UINT faceBytes; // bytes per cube face (incl. mips, aligned)
};

UINT LevelBytes(const TextureDesc& d, UINT level)
{
    UINT w = d.width >> level, h = d.height >> level, z = d.depth >> level;
    if (!w) w = 1;
    if (!h) h = 1;
    if (!z) z = 1;
    if (d.fi.compressed) {
        UINT bw = (w + 3) / 4, bh = (h + 3) / 4;
        return bw * bh * (d.fi.bits * 2) * z; // DXT1: 8 bytes per block, DXT3/5: 16
    }
    if (!d.fi.swizzled)
        return d.pitch * h;
    return w * h * z * d.fi.bits / 8;
}

bool Describe(const PixelContainer* pc, TextureDesc& d)
{
    d.xboxFormat = (pc->Format & FORMAT_FORMAT_MASK) >> FORMAT_FORMAT_SHIFT;
    if (!GetFormatInfo(d.xboxFormat, d.fi))
        return false;
    UINT dims = (pc->Format & FORMAT_DIMENSION_MASK) >> FORMAT_DIMENSION_SHIFT;
    d.cube = (pc->Format & FORMAT_CUBEMAP) != 0;
    d.volume = dims == 3;
    d.levels = (pc->Format & FORMAT_MIPMAP_MASK) >> FORMAT_MIPMAP_SHIFT;
    if (!d.levels)
        d.levels = 1;
    if (pc->Size) {
        d.width = (pc->Size & SIZE_WIDTH_MASK) + 1;
        d.height = ((pc->Size & SIZE_HEIGHT_MASK) >> SIZE_HEIGHT_SHIFT) + 1;
        d.pitch = (((pc->Size & SIZE_PITCH_MASK) >> SIZE_PITCH_SHIFT) + 1) * 64;
        d.depth = 1;
        d.levels = 1;
        d.fi.swizzled = false;
    } else {
        d.width = 1u << ((pc->Format >> FORMAT_USIZE_SHIFT) & 0xF);
        d.height = 1u << ((pc->Format >> FORMAT_VSIZE_SHIFT) & 0xF);
        d.depth = d.volume ? 1u << ((pc->Format >> FORMAT_PSIZE_SHIFT) & 0xF) : 1;
        d.pitch = d.width * d.fi.bits / 8;
    }
    d.faceBytes = 0;
    for (UINT l = 0; l < d.levels; ++l)
        d.faceBytes += LevelBytes(d, l);
    if (d.cube)
        d.faceBytes = (d.faceBytes + 127) & ~127u;
    return true;
}

// Writes one mip level of Xbox data into a locked host rect.
void UploadLevel(const TextureDesc& d, UINT level, const uint8_t* src, uint8_t* dst, UINT dstPitch, const uint32_t* palette)
{
    UINT w = std::max<UINT>(1, d.width >> level), h = std::max<UINT>(1, d.height >> level);
    if (d.fi.compressed) {
        UINT rowBytes = ((w + 3) / 4) * d.fi.bits * 2, rows = (h + 3) / 4;
        for (UINT y = 0; y < rows; ++y)
            std::memcpy(dst + y * dstPitch, src + y * rowBytes, rowBytes);
        return;
    }
    UINT srcBpp = d.fi.bits / 8;
    UINT hostBpp = HostBytesPerTexel(d.fi.host);
    std::vector<uint8_t> row(size_t(w) * srcBpp);

    if (d.fi.swizzled) {
        static thread_local std::vector<uint32_t> xs, ys;
        BuildSwizzleTables(w, h, xs, ys);
        for (UINT y = 0; y < h; ++y) {
            uint8_t* out = row.data();
            for (UINT x = 0; x < w; ++x, out += srcBpp)
                std::memcpy(out, src + (size_t(xs[x]) | ys[y]) * srcBpp, srcBpp);
            if (d.fi.conv == Conv::None)
                std::memcpy(dst + y * dstPitch, row.data(), size_t(w) * hostBpp);
            else
                ConvertRow(d.fi.conv, row.data(), dst + y * dstPitch, w, palette);
        }
    } else {
        for (UINT y = 0; y < h; ++y) {
            const uint8_t* in = src + size_t(y) * d.pitch;
            if (d.fi.conv == Conv::None)
                std::memcpy(dst + y * dstPitch, in, size_t(w) * hostBpp);
            else
                ConvertRow(d.fi.conv, in, dst + y * dstPitch, w, palette);
        }
    }
}

struct HostTexture {
    IDirect3DBaseTexture9* texture = nullptr;
    DWORD format = 0, size = 0;
    DWORD palette = 0;
    uint32_t bytes = 0;
    DWORD lastUsed = 0;
};

std::unordered_map<DWORD, HostTexture> g_Textures;
DWORD g_TextureFrame = 0;
bool g_LoggedFormats[256] = {};

} // namespace

IDirect3DBaseTexture9* HostRenderTargetTexture(DWORD data);

IDirect3DBaseTexture9* UploadTexture(BaseTexture* texture, Palette* palette)
{
    if (IDirect3DBaseTexture9* rt = HostRenderTargetTexture(texture->Data))
        return rt;

    TextureDesc d;
    if (!Describe(texture, d)) {
        DWORD f = (texture->Format & FORMAT_FORMAT_MASK) >> FORMAT_FORMAT_SHIFT;
        if (!g_LoggedFormats[f & 0xFF]) {
            g_LoggedFormats[f & 0xFF] = true;
            LOG_WARN("Texture format 0x%02lX is not supported yet", f);
        }
        return nullptr;
    }

    const uint8_t* data = ResourceData(texture);
    uint32_t bytes = d.faceBytes * (d.cube ? 6 : 1);
    const uint32_t* pal = nullptr;
    DWORD palKey = 0;
    if (d.fi.conv == Conv::P8 && palette) {
        pal = reinterpret_cast<const uint32_t*>(ResourceData(palette));
        palKey = palette->Data;
    }

    HostTexture& ht = g_Textures[texture->Data];
    bool dataChanged = kernel::ConsumeWrites(data, bytes);
    bool paletteChanged = pal && kernel::ConsumeWrites(pal, 1024);
    if (ht.texture && ht.format == texture->Format && ht.size == texture->Size && ht.palette == palKey && !dataChanged &&
        !paletteChanged) {
        ht.lastUsed = g_TextureFrame;
        return ht.texture;
    }

    bool recreate = !ht.texture || ht.format != texture->Format || ht.size != texture->Size;
    if (recreate && ht.texture) {
        ht.texture->Release();
        ht.texture = nullptr;
    }
    HRESULT hr = D3D_OK;
    if (!ht.texture) {
        // Dynamic default-pool textures: lockable for re-uploads (D3D9Ex has no managed pool).
        if (d.cube)
            hr = Device()->CreateCubeTexture(d.width, d.levels, D3DUSAGE_DYNAMIC, d.fi.host, D3DPOOL_DEFAULT,
                reinterpret_cast<IDirect3DCubeTexture9**>(&ht.texture), nullptr);
        else if (d.volume)
            hr = Device()->CreateVolumeTexture(d.width, d.height, d.depth, d.levels, D3DUSAGE_DYNAMIC, d.fi.host, D3DPOOL_DEFAULT,
                reinterpret_cast<IDirect3DVolumeTexture9**>(&ht.texture), nullptr);
        else
            hr = Device()->CreateTexture(d.width, d.height, d.levels, D3DUSAGE_DYNAMIC, d.fi.host, D3DPOOL_DEFAULT,
                reinterpret_cast<IDirect3DTexture9**>(&ht.texture), nullptr);
        LOG_DEBUG("Texture %08lX: %ux%u fmt %02lX levels %u%s%s", texture->Data, d.width, d.height, d.xboxFormat, d.levels,
            d.cube ? " cube" : "", d.fi.swizzled ? " swizzled" : "");
        if (FAILED(hr)) {
            LOG_WARN("CreateTexture %ux%u fmt %02lX levels %u failed: %08lX", d.width, d.height, d.xboxFormat, d.levels, hr);
            ht = {};
            return nullptr;
        }
    }
    FlightNote("texture %08lX %s: %ux%u fmt %02lX", texture->Data, recreate ? "created" : "re-uploaded", d.width, d.height,
        d.xboxFormat);

    if (d.volume) {
        // Volume textures are uncommon; upload level 0 slices as linear data.
        auto* vt = static_cast<IDirect3DVolumeTexture9*>(ht.texture);
        const uint8_t* src = data;
        for (UINT l = 0; l < d.levels; ++l) {
            D3DLOCKED_BOX box;
            if (SUCCEEDED(vt->LockBox(l, &box, nullptr, 0))) {
                UINT w = std::max<UINT>(1, d.width >> l), h = std::max<UINT>(1, d.height >> l), z = std::max<UINT>(1, d.depth >> l);
                UINT sliceBytes = w * h * d.fi.bits / 8;
                for (UINT s = 0; s < z; ++s) {
                    TextureDesc slice = d;
                    slice.width = w;
                    slice.height = h;
                    slice.depth = 1;
                    UploadLevel(slice, 0, src + s * sliceBytes, static_cast<uint8_t*>(box.pBits) + s * box.SlicePitch,
                        box.RowPitch, pal);
                }
                vt->UnlockBox(l);
            }
            src += LevelBytes(d, l);
        }
    } else {
        UINT faces = d.cube ? 6 : 1;
        for (UINT f = 0; f < faces; ++f) {
            const uint8_t* src = data + f * d.faceBytes;
            for (UINT l = 0; l < d.levels; ++l) {
                D3DLOCKED_RECT lr;
                HRESULT lhr = d.cube
                    ? static_cast<IDirect3DCubeTexture9*>(ht.texture)->LockRect(D3DCUBEMAP_FACES(f), l, &lr, nullptr, 0)
                    : static_cast<IDirect3DTexture9*>(ht.texture)->LockRect(l, &lr, nullptr, 0);
                if (SUCCEEDED(lhr)) {
                    UploadLevel(d, l, src, static_cast<uint8_t*>(lr.pBits), UINT(lr.Pitch), pal);
                    if (d.cube)
                        static_cast<IDirect3DCubeTexture9*>(ht.texture)->UnlockRect(D3DCUBEMAP_FACES(f), l);
                    else
                        static_cast<IDirect3DTexture9*>(ht.texture)->UnlockRect(l);
                }
                src += LevelBytes(d, l);
            }
        }
    }

    ht.format = texture->Format;
    ht.size = texture->Size;
    ht.palette = palKey;
    ht.bytes = bytes;
    ht.lastUsed = g_TextureFrame;
    return ht.texture;
}

// Frees host copies of textures that have not been drawn with for a while.
void CollectTextures(DWORD frame)
{
    g_TextureFrame = frame;
    if (frame % 120 != 0)
        return;
    for (auto it = g_Textures.begin(); it != g_Textures.end();) {
        if (frame - it->second.lastUsed > 600) {
            if (it->second.texture)
                it->second.texture->Release();
            it = g_Textures.erase(it);
        } else {
            ++it;
        }
    }
}

void ReleaseTextures()
{
    for (auto& [key, t] : g_Textures)
        if (t.texture)
            t.texture->Release();
    g_Textures.clear();
}

} // namespace swrots::d3d
