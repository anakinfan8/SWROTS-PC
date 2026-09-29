#include "core/xbe.h"

#include <windows.h>
#include <bcrypt.h>

namespace swrots {

// Retail XOR keys for the encoded entry point and kernel thunk address.
static const uint32_t kRetailEntryKey = 0xA8FC57AB;
static const uint32_t kRetailThunkKey = 0x5B6D40B6;

bool XbeFile::Load(const std::wstring& path, std::string& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "default.xbe was not found in the game data folder.";
        return false;
    }
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    m_bytes.resize(size_t(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(file, m_bytes.data(), DWORD(m_bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != m_bytes.size() || m_bytes.size() < sizeof(XbeHeader) || Header().Magic != 'HEBX') {
        error = "default.xbe is not a valid Xbox executable.";
        return false;
    }
    return true;
}

const XbeSectionHeader* XbeFile::Sections() const
{
    return reinterpret_cast<const XbeSectionHeader*>(m_bytes.data() + (Header().SectionHeadersAddress - Header().BaseAddress));
}

std::string XbeFile::SectionName(const XbeSectionHeader& s) const
{
    return reinterpret_cast<const char*>(m_bytes.data() + (s.SectionNameAddress - Header().BaseAddress));
}

uint32_t XbeFile::EntryPoint() const { return Header().EntryPoint ^ kRetailEntryKey; }
uint32_t XbeFile::KernelThunkAddress() const { return Header().KernelThunkAddress ^ kRetailThunkKey; }

std::string XbeFile::Md5Hex() const { return swrots::Md5Hex(m_bytes.data(), m_bytes.size()); }

std::string Md5Hex(const uint8_t* data, size_t size)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[16] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(hash, const_cast<PUCHAR>(data), ULONG(size), 0);
            BCryptFinishHash(hash, digest, sizeof(digest), 0);
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (uint8_t b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

} // namespace swrots
