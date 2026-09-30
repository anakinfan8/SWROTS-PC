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
//   u32 rawSize (inline data size, 0 if the data lives elsewhere or for manual entries, whose data
//   fills the rest of the entry); u32 manual;
//   u32 nameLength; char name[nameLength] (relative path, e.g. meshes\x\y.msh);
//   u32 flag2; u32 segmented; u32 typeId; i32 segment; i32 memoryImageOffset;
//   i32 memoryImageSize
// followed by the inline data. Entries are found by their self-referencing
// offset field, which needs no knowledge of the PAK's leading tables.
// Memory-image resources (animations) keep their data in the PAK's memory-image
// block instead (file header +0x14 offset, +0x18 size), which the engine loads
// whole and hands to the type's in-place loader.
class PakIndex {
public:
    struct Entry {
        uint32_t offset;
        uint32_t size;
        uint32_t typeId;
        uint32_t dataOffset;  // file offset of the inline data
        uint32_t rawSize;     // inline data size (0: none, e.g. a repeat of an earlier entry)
        int32_t imageOffset;  // in the memory-image block, or -1
        int32_t imageSize;
        std::string name; // lower case, relative
        bool segmented = false; // a stub for data in a _segNN.pak file (the full copy is another entry)
        bool HasData() const { return (rawSize > 0 && !segmented) || (imageOffset >= 0 && imageSize > 0); }
    };

    bool Load(const std::wstring& path);
    const Entry* AtOffset(uint32_t offset) const;
    // The first entry named `lowerName` at or after `offset`.
    const Entry* FindAfter(const std::string& lowerName, uint32_t offset) const;
    // The first entry named `lowerName` that carries its data.
    const Entry* FindWithData(const std::string& lowerName) const;
    // The same by path without extension and type (the engine asks for some resources that way).
    const Entry* FindStemWithData(const std::string& lowerStem, uint32_t typeId) const;
    // The same by file name alone (any folder), e.g. an animation the engine names without its folder.
    const Entry* FindFileWithData(const std::string& lowerFileName, uint32_t typeId) const;
    // Entries with data whose name starts with `lowerPrefix` (one per name), in file order.
    std::vector<const Entry*> WithPrefix(const std::string& lowerPrefix) const;
    // The entry's data: its memory image if it has one, else its inline data.
    bool ReadData(const Entry& entry, std::vector<uint8_t>& out) const;
    size_t Count() const { return m_ByOffset.size(); }
    const std::wstring& Path() const { return m_Path; }

private:
    std::wstring m_Path;
    uint32_t m_ImageBlock = 0;
    std::unordered_map<uint32_t, Entry> m_ByOffset;
    std::unordered_map<std::string, std::vector<uint32_t>> m_ByName; // offsets, ascending
    std::unordered_map<std::string, std::vector<uint32_t>> m_ByStem; // name without extension
    std::unordered_map<std::string, std::vector<uint32_t>> m_ByFile; // file name without folder
};

} // namespace swrots::game
