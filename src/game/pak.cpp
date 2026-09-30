#include "game/pak.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace swrots::game {

namespace {

uint32_t U32(const uint8_t* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

} // namespace

bool PakIndex::Load(const std::wstring& path)
{
    m_Path = path;
    m_ImageBlock = 0;
    m_ByOffset.clear();
    m_ByName.clear();
    m_ByStem.clear();
    m_ByFile.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    GetFileSizeEx(file, &size);
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const uint8_t* data = mapping ? static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    if (!data) {
        if (mapping)
            CloseHandle(mapping);
        CloseHandle(file);
        return false;
    }

    const uint32_t length = uint32_t(size.QuadPart);
    if (length >= 0x1C)
        m_ImageBlock = U32(data + 0x14);
    constexpr uint32_t kFixed = 0x18 + 0x18; // header without the name
    for (uint32_t p = 0; p + kFixed + 4 <= length; ++p) {
        if (U32(data + p + 4) != p)
            continue;
        const uint8_t* h = data + p;
        uint32_t flag = U32(h), entrySize = U32(h + 8), manual = U32(h + 16), nameLength = U32(h + 20);
        if (flag > 1 || manual > 1 || nameLength < 4 || nameLength > 200 || p + kFixed + nameLength > length)
            continue;
        if (entrySize < kFixed + nameLength || uint64_t(p) + entrySize > length)
            continue;
        const char* name = reinterpret_cast<const char*>(h + 0x18);
        bool printable = true;
        size_t n = 0;
        for (; n < nameLength && name[n]; ++n)
            printable &= name[n] >= 0x20 && name[n] < 0x7F;
        if (!printable || n < 4)
            continue;
        const uint8_t* tail = h + 0x18 + nameLength;
        if (U32(tail) > 1 || U32(tail + 4) > 1)
            continue;

        Entry e;
        e.offset = p;
        e.size = entrySize;
        e.typeId = U32(tail + 8);
        e.dataOffset = p + kFixed + nameLength;
        e.rawSize = U32(h + 12);
        if (manual && e.rawSize == 0) // manual entries (.ban) record no size; their data fills the entry
            e.rawSize = entrySize - (kFixed + nameLength);
        if (uint64_t(e.dataOffset) + e.rawSize > p + uint64_t(entrySize))
            e.rawSize = 0;
        e.imageOffset = int32_t(U32(tail + 16));
        e.imageSize = int32_t(U32(tail + 20));
        e.segmented = U32(tail + 4) == 1;
        e.name.assign(name, n);
        for (char& c : e.name)
            c = char(tolower(static_cast<unsigned char>(c)));
        // Xbox-converted files keep an "xbl_"/"xb_" extension prefix in the PAK
        // (human.xbl_cap) but are requested by their source name (human.cap).
        size_t dot = e.name.rfind('.');
        if (dot != std::string::npos) {
            if (e.name.compare(dot + 1, 4, "xbl_") == 0)
                e.name.erase(dot + 1, 4);
            else if (e.name.compare(dot + 1, 3, "xb_") == 0)
                e.name.erase(dot + 1, 3);
        }
        m_ByName[e.name].push_back(p);
        size_t stemEnd = e.name.rfind('.');
        m_ByStem[e.name.substr(0, stemEnd)].push_back(p);
        m_ByFile[e.name.substr(e.name.rfind('\\') + 1)].push_back(p);
        m_ByOffset.emplace(p, std::move(e));
    }

    UnmapViewOfFile(data);
    CloseHandle(mapping);
    CloseHandle(file);
    return !m_ByOffset.empty();
}

const PakIndex::Entry* PakIndex::AtOffset(uint32_t offset) const
{
    auto it = m_ByOffset.find(offset);
    return it == m_ByOffset.end() ? nullptr : &it->second;
}

const PakIndex::Entry* PakIndex::FindAfter(const std::string& lowerName, uint32_t offset) const
{
    auto it = m_ByName.find(lowerName);
    if (it == m_ByName.end())
        return nullptr;
    for (uint32_t o : it->second)
        if (o >= offset)
            return AtOffset(o);
    return nullptr;
}

const PakIndex::Entry* PakIndex::FindWithData(const std::string& lowerName) const
{
    auto it = m_ByName.find(lowerName);
    if (it == m_ByName.end())
        return nullptr;
    for (uint32_t o : it->second)
        if (const Entry* e = AtOffset(o); e && e->HasData())
            return e;
    return nullptr;
}

const PakIndex::Entry* PakIndex::FindStemWithData(const std::string& lowerStem, uint32_t typeId) const
{
    auto it = m_ByStem.find(lowerStem);
    if (it == m_ByStem.end())
        return nullptr;
    for (uint32_t o : it->second)
        if (const Entry* e = AtOffset(o); e && e->typeId == typeId && e->HasData())
            return e;
    return nullptr;
}

std::vector<const PakIndex::Entry*> PakIndex::WithPrefix(const std::string& lowerPrefix) const
{
    std::vector<const Entry*> found;
    for (const auto& [name, offsets] : m_ByName) {
        if (name.compare(0, lowerPrefix.size(), lowerPrefix) != 0)
            continue;
        if (const Entry* e = FindWithData(name))
            found.push_back(e);
    }
    std::sort(found.begin(), found.end(), [](const Entry* a, const Entry* b) { return a->offset < b->offset; });
    return found;
}

const PakIndex::Entry* PakIndex::FindFileWithData(const std::string& lowerFileName, uint32_t typeId) const
{
    auto it = m_ByFile.find(lowerFileName);
    if (it == m_ByFile.end())
        return nullptr;
    for (uint32_t o : it->second)
        if (const Entry* e = AtOffset(o); e && e->typeId == typeId && e->HasData())
            return e;
    return nullptr;
}

bool PakIndex::ReadData(const Entry& entry, std::vector<uint8_t>& out) const
{
    bool image = entry.imageOffset >= 0 && entry.imageSize > 0;
    uint64_t offset = image ? uint64_t(m_ImageBlock) + uint32_t(entry.imageOffset) : entry.dataOffset;
    uint32_t size = image ? uint32_t(entry.imageSize) : entry.rawSize;
    HANDLE file = CreateFileW(m_Path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    out.resize(size);
    LARGE_INTEGER at;
    at.QuadPart = LONGLONG(offset);
    DWORD read = 0;
    bool ok = SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && ReadFile(file, out.data(), size, &read, nullptr) &&
        read == size;
    CloseHandle(file);
    return ok;
}

} // namespace swrots::game
