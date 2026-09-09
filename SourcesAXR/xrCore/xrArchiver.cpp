#include "xrArchiver.h"
#include "xrFS.h"
#include "TaskManager.h"
#include <fstream>
#include "zstd/zstd.h"

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

#pragma pack(pop)

// CRC32 — thread safe via std::once_flag
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

static void write_zip_local(std::vector<char>& out, const std::string& name,
    const std::vector<char>& compData, uint32_t uncompSize, uint32_t crc) {
    ZipLocalHeader hdr;
    hdr.crc32 = crc;
    hdr.compSize = static_cast<uint32_t>(compData.size());
    hdr.uncompSize = uncompSize;
    hdr.fileNameLen = static_cast<uint16_t>(name.size());
    out.insert(out.end(), (char*)&hdr, (char*)&hdr + sizeof(hdr));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), compData.begin(), compData.end());
}

static void write_zip_central(std::vector<char>& out, const std::string& name,
    uint32_t compSize, uint32_t uncompSize, uint32_t localOffset, uint32_t crc) {
    ZipCentralDir cdr;
    cdr.crc32 = crc;
    cdr.compSize = compSize;
    cdr.uncompSize = uncompSize;
    cdr.fileNameLen = static_cast<uint16_t>(name.size());
    cdr.localHeaderOffset = localOffset;
    out.insert(out.end(), (char*)&cdr, (char*)&cdr + sizeof(cdr));
    out.insert(out.end(), name.begin(), name.end());
}

bool xrArchiver::compress_files(
    const std::string& output_dir,
    const std::string& archive_name,
    const std::vector<std::string>& file_paths,
    int compression_level)
{
    if (file_paths.empty()) return false;

    struct CompressedEntry {
        std::string name;
        uint64_t rawSize = 0;
        uint64_t compSize = 0;
        std::vector<char> data;
    };

    std::vector<CompressedEntry> entries(file_paths.size());

    // Phase 1: read all files in parallel via TaskManager
    std::vector<std::vector<char>> rawFiles;
    if (!fs().read_files_parallel(file_paths, rawFiles))
        return false;

    // Phase 2: compress all files in parallel via TaskManager
    uint32_t count = static_cast<uint32_t>(file_paths.size());
    CTaskManager::AddTaskRange(
        [&](uint32_t start, uint32_t end, uint32_t) {
            for (uint32_t i = start; i < end; ++i) {
                const std::vector<char>& raw = rawFiles[i];
                if (raw.empty()) return;
                CompressedEntry& entry = entries[i];

                size_t rawSize = raw.size();
                size_t bound = ZSTD_compressBound(rawSize);
                entry.data.resize(bound);

                size_t compSize = ZSTD_compress(entry.data.data(), bound,
                    raw.data(), rawSize, compression_level);

                if (ZSTD_isError(compSize)) {
                    entry.data.clear();
                    return;
                }

                entry.data.resize(compSize);
                entry.name = fs().get_filename(file_paths[i]);
                entry.rawSize = rawSize;
                entry.compSize = compSize;
            }
        },
        count, 1
    );
    CTaskManager::WaitAll();

    for (auto& e : entries) {
        if (e.data.empty()) return false;
    }

    // Phase 3: sequential merge
    std::vector<char> archiveData;
    auto write_bytes = [&](const void* d, size_t s) {
        const char* p = static_cast<const char*>(d);
        archiveData.insert(archiveData.end(), p, p + s);
    };

    write_bytes(&MAGIC, sizeof(MAGIC));
    write_bytes(&VERSION, sizeof(VERSION));
    write_bytes(&count, sizeof(count));

    for (auto& e : entries) {
        uint32_t nameLen = static_cast<uint32_t>(e.name.size());
        write_bytes(&nameLen, sizeof(nameLen));
        write_bytes(e.name.data(), nameLen);
        write_bytes(&e.rawSize, sizeof(e.rawSize));
        write_bytes(&e.compSize, sizeof(e.compSize));
        write_bytes(e.data.data(), e.data.size());
    }

    std::string fullPath = fs().join_path(output_dir, archive_name);
    return fs().write_file(fullPath, archiveData);
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
        uint32_t compSize;
        uint32_t uncompSize;
        uint32_t localOffset;
        uint32_t crc;
    };

    std::vector<FileEntry> entries;
    uint32_t currentOffset = 0;

    for (const auto& path : file_paths) {
        std::vector<char> raw;
        if (!fs().read_file(path, raw)) return false;

        uint32_t uncompSize = static_cast<uint32_t>(raw.size());
        uint32_t crc = crc32_compute(raw.data(), raw.size());

        size_t bound = ZSTD_compressBound(raw.size());
        std::vector<char> compressed(bound);
        size_t compSize = ZSTD_compress(compressed.data(), bound,
            raw.data(), raw.size(), compression_level);
        if (ZSTD_isError(compSize)) return false;
        compressed.resize(compSize);

        std::string name = fs().get_filename(path);
        uint32_t localSize = static_cast<uint32_t>(sizeof(ZipLocalHeader) + name.size() + compressed.size());

        entries.push_back({ name, static_cast<uint32_t>(compSize), uncompSize, currentOffset, crc });
        write_zip_local(zipData, name, compressed, uncompSize, crc);
        currentOffset += localSize;
    }

    uint32_t cdOffset = static_cast<uint32_t>(zipData.size());
    uint32_t cdSize = 0;
    for (const auto& e : entries) {
        write_zip_central(zipData, e.name, e.compSize, e.uncompSize, e.localOffset, e.crc);
        cdSize += static_cast<uint32_t>(sizeof(ZipCentralDir) + e.name.size());
    }

    ZipEndRecord eocd;
    eocd.totalEntriesDisk = static_cast<uint16_t>(entries.size());
    eocd.totalEntries = static_cast<uint16_t>(entries.size());
    eocd.centralDirSize = cdSize;
    eocd.centralDirOffset = cdOffset;
    zipData.insert(zipData.end(), (char*)&eocd, (char*)&eocd + sizeof(eocd));

    std::string fullPath = fs().join_path(output_dir, archive_name);
    return fs().write_file(fullPath, zipData);
}
