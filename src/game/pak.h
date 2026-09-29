#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace swrots::game {

// Index of the resource entries in a level PAK (res_<level>.pak).
//
// A PAK is a stream of entries, each starting with a header:
//   u32 flag; u32 offset (the header's own file offset); u32 size (whole entry);
//   u32 rawSize (inline data size, 0 if the data lives elsewhere); u32 manual;
//   u32 nameLength; char name[nameLength] (relative path, e.g. meshes\x\y.msh);
//   u32 flag2; u32 segmented; u32 typeId; i32 segment; i32 memoryImageOffset;
//   i32 memoryImageSize
// followed by the inline data. Entries are found by their self-referencing
// offset field, which needs no knowledge of the PAK's leading tables.
class PakIndex {
public:
    struct Entry {
        uint32_t offset;
        uint32_t size;
        uint32_t typeId;
        std::string name; // lower case, relative
    };

    bool Load(const std::wstring& path);
    const Entry* AtOffset(uint32_t offset) const;
    // The first entry named `lowerName` at or after `offset`.
    const Entry* FindAfter(const std::string& lowerName, uint32_t offset) const;
    size_t Count() const { return m_ByOffset.size(); }

private:
    std::unordered_map<uint32_t, Entry> m_ByOffset;
    std::unordered_map<std::string, std::vector<uint32_t>> m_ByName; // offsets, ascending
};

} // namespace swrots::game
