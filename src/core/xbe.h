#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace swrots {

#pragma pack(push, 1)
struct XbeHeader {
    uint32_t Magic;                 // 0x000 'XBEH'
    uint8_t  Signature[256];        // 0x004
    uint32_t BaseAddress;           // 0x104
    uint32_t SizeOfHeaders;         // 0x108
    uint32_t SizeOfImage;           // 0x10C
    uint32_t SizeOfImageHeader;     // 0x110
    uint32_t TimeDateStamp;         // 0x114
    uint32_t CertificateAddress;    // 0x118
    uint32_t NumberOfSections;      // 0x11C
    uint32_t SectionHeadersAddress; // 0x120
    uint32_t InitFlags;             // 0x124
    uint32_t EntryPoint;            // 0x128 (XOR-encoded)
    uint32_t TlsAddress;            // 0x12C
    uint32_t PeStackCommit;         // 0x130
    uint32_t PeHeapReserve;         // 0x134
    uint32_t PeHeapCommit;          // 0x138
    uint32_t PeBaseAddress;         // 0x13C
    uint32_t PeSizeOfImage;         // 0x140
    uint32_t PeChecksum;            // 0x144
    uint32_t PeTimeDateStamp;       // 0x148
    uint32_t DebugPathnameAddress;  // 0x14C
    uint32_t DebugFilenameAddress;  // 0x150
    uint32_t DebugUnicodeFilenameAddress; // 0x154
    uint32_t KernelThunkAddress;    // 0x158 (XOR-encoded)
    uint32_t NonKernelImportDirectoryAddress; // 0x15C
    uint32_t NumberOfLibraryVersions; // 0x160
    uint32_t LibraryVersionsAddress;  // 0x164
    uint32_t KernelLibraryVersionAddress; // 0x168
    uint32_t XapiLibraryVersionAddress;   // 0x16C
    uint32_t LogoBitmapAddress;     // 0x170
    uint32_t LogoBitmapSize;        // 0x174
};

struct XbeSectionHeader {
    uint32_t Flags;
    uint32_t VirtualAddress;
    uint32_t VirtualSize;
    uint32_t RawAddress;
    uint32_t RawSize;
    uint32_t SectionNameAddress;
    uint32_t SectionReferenceCount;
    uint32_t HeadSharedPageReferenceCountAddress;
    uint32_t TailSharedPageReferenceCountAddress;
    uint8_t  SectionDigest[20];
};

struct XbeTls {
    uint32_t DataStartAddress;
    uint32_t DataEndAddress;
    uint32_t TlsIndexAddress;
    uint32_t TlsCallbackAddress;
    uint32_t SizeOfZeroFill;
    uint32_t Characteristics;
};

struct XbeCertificate {
    uint32_t Size;
    uint32_t TimeDateStamp;
    uint32_t TitleId;
    wchar_t  TitleName[40];
    uint32_t AlternateTitleIds[16];
    uint32_t AllowedMedia;
    uint32_t GameRegion;
    uint32_t GameRatings;
    uint32_t DiskNumber;
    uint32_t Version;
    uint8_t  LanKey[16];
    uint8_t  SignatureKey[16];
    uint8_t  AlternateSignatureKeys[16][16];
};
#pragma pack(pop)

enum XbeSectionFlags : uint32_t {
    kXbeSectionWritable = 0x01,
    kXbeSectionPreload = 0x02,
    kXbeSectionExecutable = 0x04,
};

// The retail XBE as read from disc. Kept in memory for the process lifetime so
// XeLoadSection can restore non-preloaded sections.
class XbeFile {
public:
    bool Load(const std::wstring& path, std::string& error);

    const std::vector<uint8_t>& Bytes() const { return m_bytes; }
    const XbeHeader& Header() const { return *reinterpret_cast<const XbeHeader*>(m_bytes.data()); }
    const XbeSectionHeader* Sections() const;
    std::string SectionName(const XbeSectionHeader& s) const;
    const uint8_t* RawSectionData(const XbeSectionHeader& s) const { return m_bytes.data() + s.RawAddress; }

    uint32_t EntryPoint() const;
    uint32_t KernelThunkAddress() const;
    std::string Md5Hex() const;

private:
    std::vector<uint8_t> m_bytes;
};

// Lower-case hex MD5 of `size` bytes.
std::string Md5Hex(const uint8_t* data, size_t size);

} // namespace swrots
