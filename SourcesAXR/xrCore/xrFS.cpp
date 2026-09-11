#include "xrFS.h"
#include "xrArchiver.h"
#include "TaskManager.h"
#include "zstd/zstd.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <unordered_map>
#include <set>

#if defined(_WIN32)
#include <Windows.h>
#endif

std::unique_ptr<xrFS> xrFS::s_instance;

xrFS& xrFS::instance() {
    if (!s_instance) {
        s_instance = std::make_unique<xrFS>();
    }
    return *s_instance;
}

void xrFS::set_instance(std::unique_ptr<xrFS> new_fs) {
    s_instance = std::move(new_fs);
}

// ---- helpers ---------------------------------------------------------------

static uint16_t rdU16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Normalize a virtual path: separators -> '/', folded to lower case, leading
// "./" and trailing "/" stripped. Keys are these normalized strings.
std::string xrFS::vfs_key(const std::string& path) {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.size() > 1 && s.front() == '/') s.erase(0, 1);
    return s;
}

// Resolve: strip the absolute virtual_root prefix (if present) then normalize.
std::string xrFS::vfs_resolve(const std::string& vpath, const std::string& vroot) {
    std::string s = vpath;
    for (char& c : s) {
        if (c == '\\') c = '/';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!vroot.empty()) {
        std::string vr = vroot;
        if (!vr.empty() && vr.size() >= 1) {
            for (char& c : vr) {
                if (c == '\\') c = '/';
                else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
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

std::string xrFS::vfs_disk_path(const std::string& root, const std::string& key) {
    std::string p = root;
    if (!p.empty() && p.back() != '\\' && p.back() != '/') p += '\\';
    for (char c : key) p += (c == '/') ? '\\' : c;
    return p;
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
        if (!hMap) return false;
        view = static_cast<const uint8_t*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
        if (!view) return false;
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
                        if (!CTaskManager::IsRunning())
                            CTaskManager::Initialize();
                        CTaskManager::AddTaskRange(
                            [&](uint32_t a, uint32_t b, uint32_t) {
                                for (uint32_t i = a; i < b; ++i) {
                                    const size_t got = ZSTD_decompress(
                                        out.data() + outPos[i], chunkSizes[i],
                                        comp + frameOffs[i], frameSizes[i]);
                                    if (ZSTD_isError(got) || got != chunkSizes[i])
                                        allOk.store(false, std::memory_order_relaxed);
                                }
                            },
                            static_cast<uint32_t>(frameOffs.size()), 4);
                        CTaskManager::WaitAll();
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

bool xrFS::mount_zdb(const std::string& archive_path, const std::string& virtual_root)
{
    auto arc = std::make_shared<MountedArchive>();
    if (!arc->open(archive_path)) return false;
    if (!arc->parse_index()) return false;
    std::lock_guard<std::mutex> lock(m_mtx);
    m_archives.push_back(std::move(arc));
    m_virtual_root = virtual_root;
    return true;
}

void xrFS::unmount_zdb() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_archives.clear();
    m_virtual_root.clear();
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
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
    }
    const std::string key = vfs_resolve(vpath, vroot);
    if (!droot.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(vfs_disk_path(droot, key), ec) && !ec)
            return true;
    }
    for (const auto& a : arcs)
        if (a->find(key) != nullptr) return true;
    return false;
}

uint64_t xrFS::virtual_file_size(const std::string& vpath) const
{
    std::string droot, vroot;
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
    }
    const std::string key = vfs_resolve(vpath, vroot);
    if (!droot.empty()) {
        std::error_code ec;
        std::filesystem::path dp = vfs_disk_path(droot, key);
        if (std::filesystem::is_regular_file(dp, ec) && !ec)
            return std::filesystem::file_size(dp, ec);
    }
    for (const auto& a : arcs) {
        const auto e = a->find(key);
        if (e) return e->uncompSize;
    }
    return 0;
}

uint32_t xrFS::virtual_crc(const std::string& vpath) const
{
    std::string vroot;
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        vroot = m_virtual_root;
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
    }
    const std::string key = vfs_resolve(vpath, vroot);
    for (const auto& a : arcs) {
        const auto e = a->find(key);
        if (e) return e->crc;
    }
    return 0;
}

bool xrFS::read_virtual(const std::string& vpath, std::vector<char>& out) const
{
    std::string droot, vroot;
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        vroot = m_virtual_root;
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
    }
    const std::string key = vfs_resolve(vpath, vroot);
    if (!droot.empty()) {
        std::error_code ec;
        std::filesystem::path dp = vfs_disk_path(droot, key);
        if (std::filesystem::is_regular_file(dp, ec) && !ec)
            return read_file(dp.string(), out);
    }
    for (const auto& a : arcs) {
        const auto e = a->find(key);
        if (e) return a->extract(*e, out);
    }
    out.clear();
    return false;
}

std::vector<std::string> xrFS::list_virtual_files() const
{
    std::set<std::string> keys;

    std::string droot;
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        droot = m_data_root;
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
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
    for (const auto& a : arcs) {
        for (const auto& kv : a->index) keys.insert(kv.first);
    }
    return std::vector<std::string>(keys.begin(), keys.end());
}

std::vector<std::string> xrFS::list_archive_files() const
{
    std::set<std::string> keys;
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
    }
    for (const auto& a : arcs) {
        for (const auto& kv : a->index) keys.insert(kv.first);
    }
    return std::vector<std::string>(keys.begin(), keys.end());
}

std::vector<std::string> xrFS::list_last_archive_files() const
{
    std::vector<std::string> out;
    std::shared_ptr<MountedArchive> last;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (!m_archives.empty()) last = m_archives.back();
    }
    if (last) {
        out.reserve(last->index.size());
        for (const auto& kv : last->index) out.push_back(kv.first);
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

bool xrFS::archive_meta(const std::string& key, uint32_t& uncompSize, uint32_t& crc) const
{
    std::vector<std::shared_ptr<MountedArchive>> arcs;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        arcs.reserve(m_archives.size());
        for (const auto& a : m_archives) arcs.push_back(a);
    }
    for (const auto& a : arcs) {
        const auto e = a->find(key);
        if (!e) continue;
        uncompSize = e->uncompSize;
        crc = e->crc;
        return true;
    }
    return false;
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
    size_t size = f.tellg();
    out.resize(size);
    f.seekg(0);
    f.read(out.data(), size);
    return f.good();
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

    CTaskManager::AddTaskRange(
        [&](u32 start, u32 end, u32) {
            for (u32 i = start; i < end; ++i) {
                if (!read_file(paths[i], results[i])) {
                    allOk.store(false, std::memory_order_relaxed);
                }
            }
        },
        count, 1
    );
    CTaskManager::WaitAll();

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
        for (char& c : name) if (c == '\\') c = '/';
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
