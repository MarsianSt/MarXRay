#include "xrArchiver.h"
#include "xrFS.h"
#include "TaskManager.h"
#include <fstream>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <algorithm>
#include "zstd/zstd.h"
#include "_types.h"
#include "rt_compressor.h"

#pragma pack(push, 1)

struct ZipLocalHeader {
    uint32_t signature = 0x04034b50;
    uint16_t versionNeeded = 63;
    uint16_t flags = 0;
    uint16_t compressionMethod = 93; // Zstandard
    uint16_t modTime = 0;
    uint16_t modDate = 0;
    uint32_t crc32 = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint16_t fileNameLen = 0;
    uint16_t extraFieldLen = 0;
};

struct ZipCentralDir {
    uint32_t signature = 0x02014b50;
    uint16_t versionMadeBy = 63;
    uint16_t versionNeeded = 63;
    uint16_t flags = 0;
    uint16_t compressionMethod = 93;
    uint16_t modTime = 0;
    uint16_t modDate = 0;
    uint32_t crc32 = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint16_t fileNameLen = 0;
    uint16_t extraFieldLen = 0;
    uint16_t commentLen = 0;
    uint16_t diskStart = 0;
    uint16_t internalAttr = 0;
    uint32_t externalAttr = 0;
    uint32_t localHeaderOffset = 0;
};

struct ZipEndRecord {
    uint32_t signature = 0x06054b50;
    uint16_t diskNumber = 0;
    uint16_t startDisk = 0;
    uint16_t totalEntriesDisk = 0;
    uint16_t totalEntries = 0;
    uint32_t centralDirSize = 0;
    uint32_t centralDirOffset = 0;
    uint16_t commentLen = 0;
};

struct Zip64EndRecord {
    uint32_t signature = 0x06064b50;
    uint64_t recordSize = 44;
    uint16_t versionMadeBy = 63;
    uint16_t versionNeeded = 63;
    uint32_t diskNumber = 0;
    uint32_t startDisk = 0;
    uint64_t totalEntriesDisk = 0;
    uint64_t totalEntries = 0;
    uint64_t centralDirSize = 0;
    uint64_t centralDirOffset = 0;
};

struct Zip64EndLocator {
    uint32_t signature = 0x07064b50;
    uint32_t startDisk = 0;
    uint64_t zip64EocdOffset = 0;
    uint32_t diskCount = 1;
};

#pragma pack(pop)

// CRC32 вЂ” thread safe via std::once_flag
static uint32_t crc32_compute(const void* data, size_t size) {
    static uint32_t table[256] = {};
    static std::once_flag flag;
    std::call_once(flag, []() {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++)
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
    });
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

uint32_t xrArchiver::crc32(const void* data, size_t size) { return crc32_compute(data, size); }

namespace
{
    struct Zip64Extra {
        bool hasUncompSize = false;
        bool hasCompSize = false;
        bool hasLocalOffset = false;
        uint64_t uncompSize = 0;
        uint64_t compSize = 0;
        uint64_t localOffset = 0;
    };

    inline void put_u64(std::vector<char>& out, uint64_t v)
    {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
        out.push_back(static_cast<char>((v >> 32) & 0xFF));
        out.push_back(static_cast<char>((v >> 40) & 0xFF));
        out.push_back(static_cast<char>((v >> 48) & 0xFF));
        out.push_back(static_cast<char>((v >> 56) & 0xFF));
    }

    inline void patch_u64(std::vector<char>& out, size_t pos, uint64_t v)
    {
        out[pos + 0] = static_cast<char>(v & 0xFF);
        out[pos + 1] = static_cast<char>((v >> 8) & 0xFF);
        out[pos + 2] = static_cast<char>((v >> 16) & 0xFF);
        out[pos + 3] = static_cast<char>((v >> 24) & 0xFF);
        out[pos + 4] = static_cast<char>((v >> 32) & 0xFF);
        out[pos + 5] = static_cast<char>((v >> 40) & 0xFF);
        out[pos + 6] = static_cast<char>((v >> 48) & 0xFF);
        out[pos + 7] = static_cast<char>((v >> 56) & 0xFF);
    }

    inline void append_zip64_extra(std::vector<char>& extra, const Zip64Extra& z)
    {
        const size_t dataSize =
            (z.hasUncompSize ? 8 : 0) + (z.hasCompSize ? 8 : 0) + (z.hasLocalOffset ? 8 : 0);
        extra.push_back(static_cast<char>(0x01));
        extra.push_back(static_cast<char>(0x00));
        extra.push_back(static_cast<char>(dataSize & 0xFF));
        extra.push_back(static_cast<char>((dataSize >> 8) & 0xFF));
        if (z.hasUncompSize) put_u64(extra, z.uncompSize);
        if (z.hasCompSize) put_u64(extra, z.compSize);
        if (z.hasLocalOffset) put_u64(extra, z.localOffset);
    }

    inline void append_zip64_eocd(std::vector<char>& out,
        uint64_t totalEntries, uint64_t cdOffset, uint64_t cdSize)
    {
        Zip64EndRecord rec;
        rec.totalEntriesDisk = totalEntries;
        rec.totalEntries = totalEntries;
        rec.centralDirSize = cdSize;
        rec.centralDirOffset = cdOffset;
        out.insert(out.end(), (char*)&rec, (char*)&rec + sizeof(rec));
    }

    inline void append_zip64_eocd_locator(std::vector<char>& out, uint64_t zip64EocdOffset)
    {
        Zip64EndLocator loc;
        loc.zip64EocdOffset = zip64EocdOffset;
        out.insert(out.end(), (char*)&loc, (char*)&loc + sizeof(loc));
    }
}

static size_t write_zip_local(std::vector<char>& out, const std::string& name,
    const std::vector<char>& compData, uint64_t uncompSize, uint32_t crc) {
    ZipLocalHeader hdr;
    hdr.crc32 = crc;
    const uint64_t compSize = uint64_t(compData.size());
    const bool needComp = compSize > 0xFFFFFFFE;
    const bool needUncomp = uncompSize > 0xFFFFFFFE;
    const bool zip64 = needComp || needUncomp;
    hdr.compSize = needComp ? 0xFFFFFFFF : static_cast<uint32_t>(compSize);
    hdr.uncompSize = needUncomp ? 0xFFFFFFFF : static_cast<uint32_t>(uncompSize);
    hdr.fileNameLen = static_cast<uint16_t>(name.size());

    std::vector<char> extra;
    if (zip64) {
        Zip64Extra z;
        z.hasUncompSize = needUncomp;
        z.hasCompSize = needComp;
        z.uncompSize = uncompSize;
        z.compSize = compSize;
        append_zip64_extra(extra, z);
    }
    hdr.extraFieldLen = static_cast<uint16_t>(extra.size());

    out.insert(out.end(), (char*)&hdr, (char*)&hdr + sizeof(hdr));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), extra.begin(), extra.end());
    out.insert(out.end(), compData.begin(), compData.end());
    return sizeof(ZipLocalHeader) + name.size() + extra.size() + compData.size();
}

static void write_zip_central(std::vector<char>& out, const std::string& name,
    uint64_t compSize, uint64_t uncompSize, uint64_t localOffset, uint32_t crc) {
    ZipCentralDir cdr;
    cdr.crc32 = crc;
    const bool needComp = compSize > 0xFFFFFFFE;
    const bool needUncomp = uncompSize > 0xFFFFFFFE;
    const bool needOffset = localOffset > 0xFFFFFFFE;
    const bool zip64 = needComp || needUncomp || needOffset;
    cdr.compSize = needComp ? 0xFFFFFFFF : static_cast<uint32_t>(compSize);
    cdr.uncompSize = needUncomp ? 0xFFFFFFFF : static_cast<uint32_t>(uncompSize);
    cdr.localHeaderOffset = needOffset ? 0xFFFFFFFF : static_cast<uint32_t>(localOffset);
    cdr.fileNameLen = static_cast<uint16_t>(name.size());

    std::vector<char> extra;
    if (zip64) {
        Zip64Extra z;
        z.hasUncompSize = needUncomp;
        z.hasCompSize = needComp;
        z.hasLocalOffset = needOffset;
        z.uncompSize = uncompSize;
        z.compSize = compSize;
        z.localOffset = localOffset;
        append_zip64_extra(extra, z);
    }
    cdr.extraFieldLen = static_cast<uint16_t>(extra.size());

    out.insert(out.end(), (char*)&cdr, (char*)&cdr + sizeof(cdr));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), extra.begin(), extra.end());
}

bool xrArchiver::compress_files_sequential(
    const std::string& output_dir,
    const std::string& archive_name,
    const std::vector<std::string>& file_paths,
    int compression_level)
{
    if (file_paths.empty()) return false;

    std::vector<char> zipData;
    struct FileEntry {
        std::string name;
        uint64_t compSize;
        uint64_t uncompSize;
        uint64_t localOffset;
        uint32_t crc;
    };

    std::vector<FileEntry> entries;
    uint64_t currentOffset = 0;

    for (const auto& path : file_paths) {
        std::vector<char> raw;
        if (!fs().read_file(path, raw)) return false;

        uint64_t uncompSize = static_cast<uint64_t>(raw.size());
        uint32_t crc = crc32_compute(raw.data(), raw.size());

        size_t bound = ZSTD_compressBound(raw.size());
        std::vector<char> compressed(bound);
        size_t compSize = ZSTD_compress(compressed.data(), bound,
            raw.data(), raw.size(), compression_level);
        if (ZSTD_isError(compSize)) return false;
        compressed.resize(compSize);

        std::string name = fs().get_filename(path);

        entries.push_back({ name, uint64_t(compSize), uncompSize, currentOffset, crc });
        currentOffset += uint64_t(write_zip_local(zipData, name, compressed, uncompSize, crc));
    }

    uint64_t cdOffset = uint64_t(zipData.size());
    uint64_t cdSize = 0;
    for (const auto& e : entries) {
        const size_t cdStart = zipData.size();
        write_zip_central(zipData, e.name, e.compSize, e.uncompSize, e.localOffset, e.crc);
        cdSize += uint64_t(zipData.size() - cdStart);
    }

    const bool zip64 = entries.size() > 0xFFFE || cdOffset > 0xFFFFFFFE || cdSize > 0xFFFFFFFE;
    if (zip64) {
        append_zip64_eocd(zipData, entries.size(), cdOffset, cdSize);
        append_zip64_eocd_locator(zipData, cdOffset + cdSize);
    }

    ZipEndRecord eocd;
    eocd.totalEntriesDisk = static_cast<uint16_t>(entries.size() > 0xFFFE ? 0xFFFF : entries.size());
    eocd.totalEntries = eocd.totalEntriesDisk;
    eocd.centralDirSize = cdSize > 0xFFFFFFFE ? 0xFFFFFFFF : static_cast<uint32_t>(cdSize);
    eocd.centralDirOffset = cdOffset > 0xFFFFFFFE ? 0xFFFFFFFF : static_cast<uint32_t>(cdOffset);
    zipData.insert(zipData.end(), (char*)&eocd, (char*)&eocd + sizeof(eocd));

    std::string fullPath = fs().join_path(output_dir, archive_name);
    return fs().write_file(fullPath, zipData);
}

namespace
{
    constexpr uint32_t DB_HEADER_CHUNK_ID = 666;
    constexpr uint32_t ARCHIVE_FILE_HEADER_FIXED_SIZE = 16;
    constexpr uint32_t DB_DATA_CHUNK = 0;
    constexpr uint32_t DB_FILE_TABLE_CHUNK = 1;

    inline void put_u16(std::vector<char>& out, uint16_t v)
    {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
    }

    inline void put_u32(std::vector<char>& out, uint32_t v)
    {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    }

    inline void patch_u32(std::vector<char>& out, size_t pos, uint32_t v)
    {
        out[pos + 0] = static_cast<char>(v & 0xFF);
        out[pos + 1] = static_cast<char>((v >> 8) & 0xFF);
        out[pos + 2] = static_cast<char>((v >> 16) & 0xFF);
        out[pos + 3] = static_cast<char>((v >> 24) & 0xFF);
    }

    inline void put_blob(std::vector<char>& out, const void* data, size_t size)
    {
        if (!size) return;
        const char* p = static_cast<const char*>(data);
        out.insert(out.end(), p, p + size);
    }

    std::string archive_entry_name(const std::string& base_dir, const std::string& file_path)
    {
        std::error_code ec;
        std::filesystem::path rel =
            std::filesystem::relative(file_path, base_dir, ec);
        if (ec || rel.empty() || rel.is_absolute())
            rel = std::filesystem::path(file_path).filename();

        std::string name;
        name.reserve(rel.string().size());
        for (char c : rel.string())
            name.push_back(c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return name;
    }

    std::string zip_entry_name(const std::string& base_dir, const std::string& file_path)
    {
        if (base_dir.empty())
            return std::filesystem::path(file_path).filename().string();

        std::error_code ec;
        std::filesystem::path rel =
            std::filesystem::relative(file_path, base_dir, ec);
        if (ec || rel.empty() || rel.is_absolute())
            rel = std::filesystem::path(file_path).filename();

        std::string name = rel.string();
        for (char& c : name)
            if (c == '\\') c = '/';
        return name;
    }
}

bool xrArchiver::pack_zdb(
    const std::string& output_dir,
    const std::string& archive_name,
    const std::vector<std::string>& file_paths,
    const std::string& base_dir,
    int compression_level)
{
    if (file_paths.empty()) return false;

    std::vector<char> zipData;
    struct FileEntry {
        std::string name;
        uint64_t compSize;
        uint64_t uncompSize;
        uint64_t localOffset;
        uint32_t crc;
    };

    std::vector<FileEntry> entries;
    uint64_t currentOffset = 0;

    for (const auto& path : file_paths) {
        std::vector<char> raw;
        if (!fs().read_file(path, raw)) return false;

        uint64_t uncompSize = static_cast<uint64_t>(raw.size());
        uint32_t crc = crc32_compute(raw.data(), raw.size());

        std::vector<char> compressed;
        if (raw.empty()) {
            compressed.clear();
        } else {
            // Entries larger than a chunk are stored as a sequence of
            // independent zstd frames (concatenated). Method 93 stays the
            // same, 7-Zip's zstd decoder reads concatenated frames fine,
            // and the VFS can decompress such entries in parallel.
            static const size_t kChunkSize = 8 * 1024 * 1024;
            compressed.reserve(ZSTD_compressBound(raw.size()));
            size_t rawOff = 0;
            while (rawOff < raw.size()) {
                const size_t chunk = std::min(kChunkSize, raw.size() - rawOff);
                const size_t bound = ZSTD_compressBound(chunk);
                const size_t oldSize = compressed.size();
                compressed.resize(oldSize + bound);
                const size_t cs = ZSTD_compress(compressed.data() + oldSize, bound,
                    raw.data() + rawOff, chunk, compression_level);
                if (ZSTD_isError(cs)) return false;
                compressed.resize(oldSize + cs);
                rawOff += chunk;
            }
        }

        std::string name = zip_entry_name(base_dir, path);
        if (name.size() > UINT16_MAX) return false;

        entries.push_back({ name, uint64_t(compressed.size()), uncompSize, currentOffset, crc });
        currentOffset += uint64_t(write_zip_local(zipData, name, compressed, uncompSize, crc));
    }

    uint64_t cdOffset = uint64_t(zipData.size());
    uint64_t cdSize = 0;
    for (const auto& e : entries) {
        const size_t cdStart = zipData.size();
        write_zip_central(zipData, e.name, e.compSize, e.uncompSize, e.localOffset, e.crc);
        cdSize += uint64_t(zipData.size() - cdStart);
    }

    const bool zip64 = entries.size() > 0xFFFE || cdOffset > 0xFFFFFFFE || cdSize > 0xFFFFFFFE;
    if (zip64) {
        append_zip64_eocd(zipData, entries.size(), cdOffset, cdSize);
        append_zip64_eocd_locator(zipData, cdOffset + cdSize);
    }

    ZipEndRecord eocd;
    eocd.totalEntriesDisk = static_cast<uint16_t>(entries.size() > 0xFFFE ? 0xFFFF : entries.size());
    eocd.totalEntries = eocd.totalEntriesDisk;
    eocd.centralDirSize = cdSize > 0xFFFFFFFE ? 0xFFFFFFFF : static_cast<uint32_t>(cdSize);
    eocd.centralDirOffset = cdOffset > 0xFFFFFFFE ? 0xFFFFFFFF : static_cast<uint32_t>(cdOffset);
    zipData.insert(zipData.end(), (char*)&eocd, (char*)&eocd + sizeof(eocd));

    const std::string fullPath = fs().join_path(output_dir, archive_name);
    return fs().write_file(fullPath, zipData);
}