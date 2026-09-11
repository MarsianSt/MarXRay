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

bool xrArchiver::pack_db(
    const std::string& output_dir,
    const std::string& archive_name,
    const std::vector<std::string>& file_paths,
    const std::string& base_dir,
    const std::string& entry_point,
    bool auto_load)
{
    if (file_paths.empty()) return false;

    struct PackedEntry {
        std::string name;
        uint32_t sizeReal = 0;
        uint32_t sizeCompr = 0;
        uint32_t crc = 0;
        uint32_t ptr = 0;
        std::vector<char> blob;
    };

    std::vector<PackedEntry> entries(file_paths.size());

    static std::once_flag lzo_once;
    std::call_once(lzo_once, []() { rtc_initialize(); });

    std::vector<std::vector<char>> rawFiles;
    if (!fs().read_files_parallel(file_paths, rawFiles)) return false;

    const uint32_t count = static_cast<uint32_t>(file_paths.size());
    CTaskManager::AddTaskRange(
        [&](uint32_t start, uint32_t end, uint32_t) {
            for (uint32_t i = start; i < end; ++i) {
                PackedEntry& e = entries[i];
                const std::vector<char>& raw = rawFiles[i];

                e.name = archive_entry_name(base_dir, file_paths[i]);
                if (e.name.size() > 65535 - ARCHIVE_FILE_HEADER_FIXED_SIZE) return;
                e.crc = crc32_compute(raw.data(), raw.size());
                e.sizeReal = static_cast<uint32_t>(raw.size());

                if (raw.empty()) {
                    e.sizeCompr = 0;
                    continue;
                }

                u32 bound = rtc_csize(static_cast<u32>(raw.size()));
                std::vector<char> comp(bound);
                size_t compSize = rtc_compress(comp.data(), bound, raw.data(), static_cast<u32>(raw.size()));
                if (compSize && compSize < raw.size()) {
                    e.sizeCompr = static_cast<uint32_t>(compSize);
                    e.blob.assign(comp.data(), comp.data() + compSize);
                }
                else {
                    e.sizeCompr = e.sizeReal;
                    e.blob = raw;
                }
            }
        },
        count, 1
    );
    CTaskManager::WaitAll();

    for (const auto& e : entries)
        if (e.blob.empty() && e.sizeReal != 0) return false;

    std::vector<char> out;

    std::string ini =
        "[header]\r\n"
        "auto_load = " + std::string(auto_load ? "true" : "false") + "\r\n"
        "creator = \"xrArchiver\"\r\n"
        "entry_point = " + entry_point + "\r\n";

    put_u32(out, DB_HEADER_CHUNK_ID);
    put_u32(out, static_cast<uint32_t>(ini.size()));
    put_blob(out, ini.data(), ini.size());

    const size_t dataChunkSizePos = out.size() + 4;
    put_u32(out, DB_DATA_CHUNK);
    put_u32(out, 0);
    const size_t dataChunkBegin = out.size();
    for (auto& e : entries) {
        e.ptr = static_cast<uint32_t>(out.size());
        put_blob(out, e.blob.data(), e.blob.size());
    }
    patch_u32(out, dataChunkSizePos, static_cast<uint32_t>(out.size() - dataChunkBegin));

    const size_t tableSizePos = out.size() + 4;
    put_u32(out, DB_FILE_TABLE_CHUNK);
    put_u32(out, 0);
    const size_t tableBegin = out.size();
    for (const auto& e : entries) {
        put_u16(out, static_cast<uint16_t>(e.name.size() + ARCHIVE_FILE_HEADER_FIXED_SIZE));
        put_u32(out, e.sizeReal);
        put_u32(out, e.sizeCompr);
        put_u32(out, e.crc);
        put_blob(out, e.name.data(), e.name.size());
        put_u32(out, e.ptr);
    }
    patch_u32(out, tableSizePos, static_cast<uint32_t>(out.size() - tableBegin));

    const std::string fullPath = fs().join_path(output_dir, archive_name);
    return fs().write_file(fullPath, out);
}

bool xrArchiver::pack_zdb(
    const std::string& output_dir,
    const std::string& archive_name,
    const std::vector<std::string>& file_paths,
    const std::string& base_dir,
    int compression_level)
{
    if (file_paths.empty() || file_paths.size() > UINT16_MAX) return false;

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
        if (raw.size() > UINT32_MAX) return false;

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
        uint32_t localSize = static_cast<uint32_t>(
            sizeof(ZipLocalHeader) + name.size() + compressed.size());

        entries.push_back({ name, static_cast<uint32_t>(compressed.size()), uncompSize, currentOffset, crc });
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

    const std::string fullPath = fs().join_path(output_dir, archive_name);
    return fs().write_file(fullPath, zipData);
}