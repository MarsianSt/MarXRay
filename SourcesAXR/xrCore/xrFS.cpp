#include "xrFS.h"
#include "xrArchiver.h"
#include "TaskManager.h"
#include "zstd/zstd.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <set>

#if defined(_WIN32)
#include <Windows.h>
#endif

xrFS& xrFS::instance() {
    static xrFS the_fs; // Meyers singleton: thread-safe lazy initialization.
    return the_fs;
}
// ---- helpers ---------------------------------------------------------------

static uint16_t rdU16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t xrFS::cache_shard_index(const std::string& key) {
    return std::hash<std::string>{}(key) % kCacheShards;
}

// Common normalization: unify separators to '/' and fold to lower case.
std::string xrFS::normalize_path(const std::string& path) {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Normalize a virtual path: separators -> '/', folded to lower case, leading
// "./" and trailing "/" stripped. Keys are these normalized strings.
std::string xrFS::vfs_key(const std::string& path) {
    std::string s = normalize_path(path);
    if (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.size() > 1 && s.front() == '/') s.erase(0, 1);
    return s;
}

// Resolve: strip the absolute virtual_root prefix (if present) then normalize.
std::string xrFS::vfs_resolve(const std::string& vpath, const std::string& vroot) {
    std::string s = normalize_path(vpath);
    if (!vroot.empty()) {
        std::string vr = vroot;
        if (!vr.empty()) {
            vr = normalize_path(vr);
            while (!vr.empty() && vr.back() == '/') vr.pop_back();
            if (s.size() > vr.size() && s.compare(0, vr.size(), vr) == 0 && s[vr.size()] == '/')
                s.erase(0, vr.size());
        }
    }
    if (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    while (!s.empty() && s.front() == '/') s.erase(0, 1);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// Join the real-disk root with a normalized (lowercase, '/'-separated) key
// using the platform's native separator via std::filesystem::path. On POSIX the
// key used to be joined with '\\' and collapsed into a single filename.
std::string xrFS::vfs_disk_path(const std::string& root, const std::string& key) {
    std::filesystem::path p(root);
    for (const auto& part : std::filesystem::path(key))
        p /= part;
    return p.string();
}

void xrFS::set_data_root(const std::string& real_dir) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_data_root = real_dir;
}
void xrFS::clear_data_root() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_data_root.clear();
}

// ---- mounted archive (zip with zstd method 93) -----------------------------

struct xrFS::MountedArchive {
    std::string path;
#if defined(_WIN32)
    void* hFile = nullptr;
    void* hMap = nullptr;
#endif
    const uint8_t* view = nullptr;
    uint64_t viewSize = 0;

    struct ZEntry {
        uint32_t localOff = 0;
        uint32_t compSize = 0;
        uint32_t uncompSize = 0;
        uint32_t crc = 0;
        uint16_t method = 93;
    };
    std::unordered_map<std::string, ZEntry> index;

    bool open(const std::string& p) {
#if !defined(_WIN32)
        (void)p; return false;
#else
        path = p;
        std::error_code ec;
        uint64_t size = std::filesystem::file_size(p, ec);
        if (ec || size < 22) return false;

        const std::wstring wpath = std::filesystem::path(p).wstring();
        hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!hFile || hFile == INVALID_HANDLE_VALUE) { hFile = nullptr; return false; }

        uint32_t hi = static_cast<uint32_t>(size >> 32);
        uint32_t lo = static_cast<uint32_t>(size & 0xFFFFFFFF);
        hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, hi, lo, nullptr);
        if (!hMap) {
            CloseHandle(hFile);
            hFile = nullptr;
            return false;
        }
        view = static_cast<const uint8_t*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
        if (!view) {
            CloseHandle(hMap);
            hMap = nullptr;
            CloseHandle(hFile);
            hFile = nullptr;
            return false;
        }
        viewSize = size;
        return true;
#endif
    }

    void close() {
#if defined(_WIN32)
        if (view) UnmapViewOfFile(view);
        view = nullptr;
        if (hMap) CloseHandle(hMap);
        hMap = nullptr;
        if (hFile) CloseHandle(hFile);
        hFile = nullptr;
#endif
    }
    ~MountedArchive() { close(); }

    bool parse_index() {
        if (!view || viewSize < 22) return false;
        // EOCD: scan backwards over the last 22 + 65535 bytes
        uint64_t start = viewSize - 22;
        uint64_t limit = viewSize > 22 + 65535 ? viewSize - (22 + 65535) : 0;
        uint64_t eocd = 0;
        for (uint64_t pos = start; pos >= limit && pos + 22 <= viewSize; --pos) {
            if (rdU32(view + pos) == 0x06054b50u) { eocd = pos; break; }
            if (pos == 0) break;
        }
        if (!eocd) return false;
        uint16_t total = rdU16(view + eocd + 10);
        uint32_t cdSize = rdU32(view + eocd + 12);
        uint32_t cdOffset = rdU32(view + eocd + 16);
        if ((uint64_t)cdOffset + cdSize > viewSize) return false;

        uint64_t p = cdOffset, end = cdOffset + cdSize;
        index.clear();
        while (p + 46 <= end) {
            if (rdU32(view + p) != 0x02014b50u) return false;
            uint16_t method = rdU16(view + p + 10);
            uint32_t crc = rdU32(view + p + 16);
            uint32_t compSize = rdU32(view + p + 20);
            uint32_t uncompSize = rdU32(view + p + 24);
            uint16_t nlen = rdU16(view + p + 28);
            uint16_t elen = rdU16(view + p + 30);
            uint16_t clen = rdU16(view + p + 32);
            uint32_t localOff = rdU32(view + p + 42);
            if (p + 46 + nlen > end) return false;
            std::string name((const char*)view + p + 46, nlen);
            if (!name.empty() && name.back() == '/') { p += 46 + nlen + elen + clen; continue; } // dir marker
            index.emplace(xrFS::vfs_key(name),
                          ZEntry{ localOff, compSize, uncompSize, crc, method });
            p += 46 + nlen + elen + clen;
        }
        return !index.empty();
    }

    const ZEntry* find(const std::string& key) const {
        auto it = index.find(key);
        return it == index.end() ? nullptr : &it->second;
    }

    // Decompress entry e into `out`; verifies CRC of the extracted content.
    bool extract(const ZEntry& e, std::vector<char>& out) const {
        if (!view) return false;
        if (e.localOff >= viewSize || e.localOff + 30 > viewSize) return false;
        if (rdU32(view + e.localOff) != 0x04034b50u) return false;
        uint16_t nlen = rdU16(view + e.localOff + 26);
        uint16_t elen = rdU16(view + e.localOff + 28);
        uint64_t off = (uint64_t)e.localOff + 30 + nlen + elen;
        if (off + e.compSize > viewSize) return false;

        static const size_t CSIZE_MAX = (size_t)0x7FFFFFFF;
        if (e.compSize > CSIZE_MAX || e.uncompSize > CSIZE_MAX) return false;

        if (e.method == 0) {
            out.assign((const char*)view + off, (const char*)view + off + e.compSize);
        } else if (e.method == 93) {
            out.resize(e.uncompSize);
            if (e.compSize == 0) {
                out.clear();
            } else {
                const char* comp = reinterpret_cast<const char*>(view + off);

                // Collect the zstd frames this entry consists of.
                std::vector<size_t> frameOffs, frameSizes;
                size_t pos = 0;
                while (pos < e.compSize) {
                    const size_t fsz = ZSTD_findFrameCompressedSize(comp + pos, e.compSize - pos);
                    if (ZSTD_isError(fsz)) { frameOffs.clear(); break; }
                    frameOffs.push_back(pos);
                    frameSizes.push_back(fsz);
                    pos += fsz;
                }

                if (frameOffs.size() == 1) {
                    const size_t got = ZSTD_decompress(out.data(), out.size(), comp, e.compSize);
                    if (ZSTD_isError(got) || got != e.uncompSize) { out.clear(); return false; }
                } else if (frameOffs.size() > 1) {
                    // Multi-frame entry (pack_zdb chunks large files): parallel decompress.
                    std::vector<size_t> outPos(frameOffs.size()), chunkSizes(frameOffs.size());
                    size_t total = 0;
                    bool known = true;
                    for (size_t i = 0; i < frameOffs.size(); ++i) {
                        const unsigned long long cs =
                            ZSTD_getFrameContentSize(comp + frameOffs[i], frameSizes[i]);
                        if (cs == ZSTD_CONTENTSIZE_UNKNOWN || cs == ZSTD_CONTENTSIZE_ERROR) {
                            known = false;
                            break;
                        }
                        outPos[i] = total;
                        chunkSizes[i] = static_cast<size_t>(cs);
                        total += chunkSizes[i];
                    }

                    bool okDec = false;
                    if (known && total == e.uncompSize && e.uncompSize <= out.size()) {
                        std::atomic<bool> allOk{true};
                        auto decompressRange = [&](size_t a, size_t b) {
                            for (size_t i = a; i < b; ++i) {
                                const size_t got = ZSTD_decompress(
                                    out.data() + outPos[i], chunkSizes[i],
                                    comp + frameOffs[i], frameSizes[i]);
                                if (ZSTD_isError(got) || got != chunkSizes[i])
                                    allOk.store(false, std::memory_order_relaxed);
                            }
                        };
                        if (CTaskManager::IsInsideTask()) {
                            // Nested call (inside a worker task): run the frames
                            // on this thread to avoid waiting on the busy pool.
                            decompressRange(0, outPos.size());
                        } else {
                            CTaskManager::AddTaskRange(
                                [&](uint32_t a, uint32_t b, uint32_t) {
                                    decompressRange(a, b);
                                },
                                static_cast<uint32_t>(frameOffs.size()), 4);
                            CTaskManager::WaitAll();
                        }
                        okDec = allOk.load(std::memory_order_relaxed);
                    }

                    if (!okDec) {
                        // Fallback: sequential assembly (foreign/legacy archives).
                        size_t o = 0;
                        for (size_t i = 0; i < frameOffs.size() && o <= e.uncompSize; ++i) {
                            const size_t got = ZSTD_decompress(
                                out.data() + o, e.uncompSize - o,
                                comp + frameOffs[i], frameSizes[i]);
                            if (ZSTD_isError(got) || o + got > e.uncompSize) {
                                o = e.uncompSize + 1;
                                break;
                            }
                            o += got;
                        }
                        okDec = (o == e.uncompSize);
                    }
                    if (!okDec) { out.clear(); return false; }
                } else {
                    out.clear();
                    return false;
                }
            }
        } else {
            out.clear();
            return false;
        }
        if (xrArchiver::crc32(out.data(), out.size()) != e.crc) { out.clear(); return false; }
        return true;
    }
};

// Combined lookup index across all mounted archives. Shallow-immutable once
// published: readers grab the shared_ptr and can use it lock-free.
struct xrFS::FlatIndex {
    struct Rec {
        uint32_t owner;   // index into owners
        uint32_t mtime;   // archive file last_write_time (seconds), for locator
        MountedArchive::ZEntry e;
    };
    std::vector<std::shared_ptr<MountedArchive>> owners; // keep mapped views alive
    std::unordered_map<std::string, Rec> map;

    const Rec* find(const std::string& key) const {
        auto it = map.find(key);
        return it == map.end() ? nullptr : &it->second;
    }
    const MountedArchive* archive_of(const Rec& r) const {
        return owners[r.owner].get();
    }
};

void xrFS::rebuild_flat_locked()
{
    auto flat = std::make_shared<FlatIndex>();
    flat->owners.reserve(m_archives.size());
    size_t oi = 0;
    for (const auto& arc : m_archives)
    {
        uint32_t mt = 0;
        std::error_code ec;
        const auto fst = std::filesystem::last_write_time(arc->path, ec);
        if (!ec)
            mt = (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(
                fst.time_since_epoch()).count();
        const uint32_t owner = (uint32_t)oi;
        // First-wins: the archive mounted first owns the key, mirroring the old
        // per-archive scan order.
        for (const auto& kv : arc->index)
            flat->map.emplace(kv.first, FlatIndex::Rec{ owner, mt, kv.second });
        flat->owners.push_back(arc);
        ++oi;
    }
    m_flat = std::move(flat);
}

bool xrFS::mount_zdb(const std::string& archive_path, const std::string& virtual_root)
{
    return mount_zdb_many(std::vector<std::string>{ archive_path }, virtual_root) > 0;
}

size_t xrFS::mount_zdb_many(const std::vector<std::string>& archive_paths,
                            const std::string& virtual_root)
{
    const uint32_t N = (uint32_t)archive_paths.size();
    if (N == 0) return 0;

    // Step 1 (parallel): mmap + parse the central directory of every archive.
    // Each task owns its own MountedArchive, so nothing is shared yet; input
    // order is preserved by slotting results into `ready[i]`.
    std::vector<std::shared_ptr<MountedArchive>> ready(N);
    auto parseRange = [&](uint32_t a, uint32_t b)
    {
        for (uint32_t i = a; i < b; ++i)
        {
            auto arc = std::make_shared<MountedArchive>();
            if (arc->open(archive_paths[i]) && arc->parse_index())
                ready[i] = std::move(arc);
        }
    };
    if (CTaskManager::IsInsideTask()) {
        // Nested call (inside a worker task): run inline to avoid a scheduler
        // deadlock - the pool is already busy with the outer task.
        parseRange(0, N);
    } else {
        CTaskManager::AddTaskRange(
            [&](uint32_t a, uint32_t b, uint32_t) { parseRange(a, b); }, N, 1);
        CTaskManager::WaitAll();
    }

    // Step 2 (serial): commit the parsed archives and publish the flat index.
    size_t mounted = 0;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_virtual_root = virtual_root;
        for (auto& arc : ready)
        {
            if (!arc) continue;
            m_archives.push_back(std::move(arc));
            ++mounted;
        }
        if (mounted) rebuild_flat_locked();
    }
    return mounted;
}

void xrFS::unmount_zdb() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_archives.clear();
    m_virtual_root.clear();
    m_flat.reset();
    for (auto& shard : m_cacheShards) {
        std::lock_guard<std::mutex> cm(shard.mtx);
        shard.map.clear();
    }
    m_cacheBytes.store(0, std::memory_order_relaxed);
}

bool xrFS::is_mounted() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return !m_archives.empty();
}

size_t xrFS::mounted_count() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_archives.size();
}

bool xrFS::virtual_exists(const std::string& vpath) const
{
    std::string droot, vroot;
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        flat = m_flat;
    }
    const std::string key = vfs_resolve(vpath, vroot);
    if (!droot.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(vfs_disk_path(droot, key), ec) && !ec)
            return true;
    }
    return flat && flat->find(key) != nullptr;
}

uint64_t xrFS::virtual_file_size(const std::string& vpath) const
{
    std::string droot, vroot;
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        flat = m_flat;
    }
    const std::string key = vfs_resolve(vpath, vroot);
    if (!droot.empty()) {
        // Single stat: file_size already fails for non-files, no need for a
        // separate is_regular_file syscall on top.
        std::error_code ec;
        uint64_t sz = std::filesystem::file_size(vfs_disk_path(droot, key), ec);
        if (!ec) return sz;
    }
    const FlatIndex::Rec* r = flat ? flat->find(key) : nullptr;
    return r ? r->e.uncompSize : 0;
}

uint32_t xrFS::virtual_crc(const std::string& vpath) const
{
    std::string droot, vroot;
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        flat = m_flat;
    }
    const std::string key = vfs_resolve(vpath, vroot);
    // Same disk-override semantics as virtual_exists / virtual_file_size:
    // a real file under the data root wins, and its CRC is computed from disk.
    if (!droot.empty()) {
        std::error_code ec;
        std::filesystem::path dp = vfs_disk_path(droot, key);
        if (std::filesystem::is_regular_file(dp, ec) && !ec) {
            std::vector<char> buf;
            if (read_file(dp.string(), buf))
                return xrArchiver::crc32(buf.data(), buf.size());
            return 0;
        }
    }
    const FlatIndex::Rec* r = flat ? flat->find(key) : nullptr;
    return r ? r->e.crc : 0;
}

bool xrFS::read_virtual(const std::string& vpath, std::vector<char>& out) const
{
    std::string droot, vroot;
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        flat = m_flat;
    }
    const std::string key = vfs_resolve(vpath, vroot);
    if (!droot.empty()) {
        std::error_code ec;
        std::filesystem::path dp = vfs_disk_path(droot, key);
        if (std::filesystem::is_regular_file(dp, ec) && !ec)
            return read_file(dp.string(), out);
    }
    const FlatIndex::Rec* r = flat ? flat->find(key) : nullptr;
    if (!r) { out.clear(); return false; }
    {
        // Prefetched entry: serve straight from memory.
        auto& shard = m_cacheShards[cache_shard_index(key)];
        std::lock_guard<std::mutex> cm(shard.mtx);
        auto cit = shard.map.find(key);
        if (cit != shard.map.end()) { out = cit->second; return true; }
    }
    return flat->archive_of(*r)->extract(r->e, out);
}

void xrFS::prefetch_virtual(const std::vector<std::string>& vpaths) const
{
    std::string droot, vroot;
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        flat = m_flat;
    }
    if (!flat || vpaths.empty()) return;

    // Overall cache quota: bytes already cached + this batch. On overflow the
    // whole cache is dropped (simplest eviction, no LRU). A batch that alone
    // exceeds the quota is ignored entirely (normal lazy path stays in effect).
    static const size_t kCacheCap = 1024ull * 1024 * 1024; // 1 GiB total cached bytes
    const size_t cachedBytes = m_cacheBytes.load(std::memory_order_relaxed);

    // Select cacheable archive entries (no disk override) and size the batch.
    std::vector<std::string> keys;
    keys.reserve(vpaths.size());
    size_t total = 0;
    for (const std::string& vp : vpaths)
    {
        const std::string key = vfs_resolve(vp, vroot);
        if (!droot.empty())
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file(vfs_disk_path(droot, key), ec) && !ec)
                continue; // real file overrides; the serial loop reads it from disk
        }
        const FlatIndex::Rec* r = flat->find(key);
        if (!r) continue;
        const size_t sz = r->e.uncompSize;
        if (cachedBytes + total + sz > kCacheCap)
        {
            // Would overflow the cache budget. Skip this file so read_virtual's
            // serial fallback reads it from disk. Never flush the whole cache
            // for one oversized file: the previous batch filled it in parallel.
            continue;
        }
        total += sz;
        keys.push_back(key);
    }
    const uint32_t n = (uint32_t)keys.size();
    if (n == 0) return;

    // Parallel decode (mmap views are read-only; per-slot buffers are private).
    std::vector<std::vector<char>> slots(n);
    std::atomic<bool> allOk{true};
    auto decodeRange = [&](uint32_t start, uint32_t end)
    {
        for (uint32_t i = start; i < end; ++i)
        {
            const FlatIndex::Rec* r = flat->find(keys[i]);
            if (r && !flat->archive_of(*r)->extract(r->e, slots[i]))
                allOk.store(false, std::memory_order_relaxed);
        }
    };
    if (CTaskManager::IsInsideTask()) {
        // Nested call (inside a worker task): run inline to avoid a scheduler
        // deadlock - the pool is already busy with the outer task.
        decodeRange(0, n);
    } else {
        CTaskManager::AddTaskRange(
            [&](uint32_t a, uint32_t b, uint32_t) { decodeRange(a, b); }, n, 1);
        CTaskManager::WaitAll();
    }
    if (!allOk.load(std::memory_order_relaxed)) return;

    for (uint32_t i = 0; i < n; ++i)
    {
        slots[i].shrink_to_fit();
        const size_t sz = slots[i].size();
        auto& shard = m_cacheShards[cache_shard_index(keys[i])];
        std::lock_guard<std::mutex> cm(shard.mtx);
        if (shard.map.emplace(keys[i], std::move(slots[i])).second)
            m_cacheBytes.fetch_add(sz, std::memory_order_relaxed);
    }
}

bool xrFS::read_virtual_many(const std::vector<std::string>& vpaths,
                             std::vector<std::vector<char>>& results) const
{
    const uint32_t count = (uint32_t)vpaths.size();
    if (count == 0) { results.clear(); return true; }

    std::string droot, vroot;
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        flat = m_flat;
    }

    results.assign(count, {});
    // Pre-resolve keys once (single flat-index snapshot, lock-free afterwards).
    std::vector<std::string> keys(count);
    for (uint32_t i = 0; i < count; ++i)
        keys[i] = vfs_resolve(vpaths[i], vroot);

    std::atomic<bool> allOk{true};
    // Same per-element semantics as read_virtual: a real file under the data
    // root overrides the archive entry; otherwise the prefetch cache is used
    // first, then the archive. Multi-frame entries nested-extract sequentially
    // via the IsInsideTask() guard, so no scheduler deadlock.
    auto readRange = [&](uint32_t start, uint32_t end)
    {
        for (uint32_t i = start; i < end; ++i)
        {
            const std::string& key = keys[i];
            if (!droot.empty())
            {
                std::error_code ec;
                std::filesystem::path dp = vfs_disk_path(droot, key);
                if (std::filesystem::is_regular_file(dp, ec) && !ec)
                {
                    if (read_file(dp.string(), results[i])) continue;
                    allOk.store(false, std::memory_order_relaxed);
                    continue;
                }
            }
            {
                auto& shard = m_cacheShards[cache_shard_index(key)];
                std::lock_guard<std::mutex> cm(shard.mtx);
                auto cit = shard.map.find(key);
                if (cit != shard.map.end())
                {
                    results[i] = cit->second;
                    continue;
                }
            }
            const FlatIndex::Rec* r = flat ? flat->find(key) : nullptr;
            if (r)
            {
                if (!flat->archive_of(*r)->extract(r->e, results[i]))
                    allOk.store(false, std::memory_order_relaxed);
            }
            else
            {
                allOk.store(false, std::memory_order_relaxed);
            }
        }
    };
    if (CTaskManager::IsInsideTask()) {
        // Nested call (inside a worker task): run inline to avoid a scheduler
        // deadlock - the pool is already busy with the outer task.
        readRange(0, count);
    } else {
        CTaskManager::AddTaskRange(
            [&](uint32_t a, uint32_t b, uint32_t) { readRange(a, b); }, count, 1);
        CTaskManager::WaitAll();
    }
    return allOk.load(std::memory_order_relaxed);
}

std::vector<std::string> xrFS::list_virtual_files() const
{
    std::set<std::string> keys;

    std::string droot;
    std::vector<const MountedArchive*> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a.get());
    }
    if (!droot.empty()) {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(
            droot, std::filesystem::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) continue;
            if (ec) { ec.clear(); continue; }
            const std::filesystem::path rel = std::filesystem::relative(it->path(), droot, ec);
            if (ec || rel.is_absolute()) continue;
            keys.insert(vfs_key(rel.string()));
        }
    }
    for (const auto* a : arcs) {
        for (const auto& kv : a->index) keys.insert(kv.first);
    }
    return std::vector<std::string>(keys.begin(), keys.end());
}

std::vector<std::string> xrFS::list_archive_files() const
{
    std::set<std::string> keys;
    std::vector<const MountedArchive*> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a.get());
    }
    for (const auto* a : arcs) {
        for (const auto& kv : a->index) keys.insert(kv.first);
    }
    return std::vector<std::string>(keys.begin(), keys.end());
}

std::vector<std::string> xrFS::list_last_archive_files() const
{
    std::vector<std::string> out;
    const MountedArchive* last = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (!m_archives.empty()) last = m_archives.back().get();
    }
    if (last) {
        out.reserve(last->index.size());
        for (const auto& kv : last->index) out.push_back(kv.first);
        std::sort(out.begin(), out.end());
    }
    return out;
}

std::vector<std::string> xrFS::list_disk_files() const
{
    std::vector<std::string> keys;
    std::string droot;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
    }
    if (droot.empty()) return keys;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        droot, std::filesystem::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (ec) { ec.clear(); continue; }
        const std::filesystem::path rel = std::filesystem::relative(it->path(), droot, ec);
        if (ec || rel.is_absolute()) continue;
        keys.push_back(vfs_key(rel.string()));
    }
    return keys;
}

bool xrFS::archive_meta(const std::string& key, uint32_t& uncompSize, uint32_t& crc,
                        uint32_t* mtime) const
{
    std::shared_ptr<const FlatIndex> flat;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        flat = m_flat;
    }
    const FlatIndex::Rec* r = flat ? flat->find(key) : nullptr;
    if (!r) return false;
    uncompSize = r->e.uncompSize;
    crc = r->e.crc;
    if (mtime) *mtime = r->mtime;
    return true;
}

xrFS::~xrFS() = default;

bool xrFS::exists(const std::string& path) const {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

uint64_t xrFS::file_size(const std::string& path) const {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return ec ? 0 : sz;
}

bool xrFS::read_file(const std::string& path, std::vector<char>& out) const {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff off = f.tellg();
    if (off < 0) return false; // stream not at a valid position
    const size_t size = static_cast<size_t>(off);
    out.resize(size);
    f.seekg(0);
    if (size > 0)
        f.read(out.data(), static_cast<std::streamsize>(size));
    // Verify by bytes actually transferred (gcount), not f.good(): a short read
    // (dfs eof) otherwise reports success with stale vector contents.
    return static_cast<size_t>(f.gcount()) == size;
}

bool xrFS::write_file(const std::string& path, const std::vector<char>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), data.size());
    f.close();
    return f.good();
}

bool xrFS::remove(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

bool xrFS::create_directory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

bool xrFS::rename(const std::string& from, const std::string& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}

std::string xrFS::get_filename(const std::string& path) const {
    return std::filesystem::path(path).filename().string();
}

std::string xrFS::join_path(const std::string& a, const std::string& b) const {
    return (std::filesystem::path(a) / b).string();
}

std::vector<std::string> xrFS::list_files(const std::string& dir, const std::string& ext) const {
    std::vector<std::string> res;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file() && (ext.empty() || e.path().extension() == ext))
            res.push_back(e.path().string());
    }
    return res;
}

// Parallel batch read: read multiple files concurrently via CTaskManager
bool xrFS::read_files_parallel(
    const std::vector<std::string>& paths,
    std::vector<std::vector<char>>& results) const
{
    const u32 count = static_cast<u32>(paths.size());
    if (count == 0) return true;
    if (count == 1) {
        results.resize(1);
        return read_file(paths[0], results[0]);
    }

    results.resize(count);
    std::atomic<bool> allOk{true};

    auto readRange = [&](u32 start, u32 end) {
        for (u32 i = start; i < end; ++i) {
            if (!read_file(paths[i], results[i])) {
                allOk.store(false, std::memory_order_relaxed);
            }
        }
    };
    if (CTaskManager::IsInsideTask()) {
        // Nested call (inside a worker task): run inline to avoid a scheduler
        // deadlock - the pool is already busy with the outer task.
        readRange(0, count);
    } else {
        CTaskManager::AddTaskRange(
            [&](u32 a, u32 b, u32) { readRange(a, b); },
            count, 1
        );
        CTaskManager::WaitAll();
    }

    return allOk.load(std::memory_order_relaxed);
}

// Recursively collect regular files under base_dir into `files`
static void collect_tree(const std::string& base_dir, std::vector<std::string>& files)
{
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        base_dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec))
    {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (ec) { ec.clear(); continue; }

        const std::filesystem::path full = it->path();
        const std::filesystem::path rel = std::filesystem::relative(full, base_dir, ec);
        if (ec) { ec.clear(); continue; }
        if (rel.is_absolute()) continue;

        std::string name = rel.string();
#if defined(_WIN32)
        // std::filesystem::relative yields backslashes on Windows; normalize to
        // the key separator. On POSIX the native separator is already '/'.
        for (char& c : name) if (c == '\\') c = '/';
#endif
        if (name.size() > UINT16_MAX) continue; // zip central dir name limit

        files.push_back(full.string());
    }
}

bool xrFS::pack_zdb_tree(
    const std::string& output_dir,
    const std::string& archive_name,
    const std::string& base_dir,
    int compression_level) const
{
    std::vector<std::string> files;
    collect_tree(base_dir, files);
    if (files.empty()) return false;
    return xrArchiver::pack_zdb(output_dir, archive_name, files, base_dir, compression_level);
}