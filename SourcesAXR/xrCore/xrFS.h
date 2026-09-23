#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <array>
#include <atomic>

#if !defined(XRCORE_API)
#	if defined(XRCORE_EXPORTS)
#		define XRCORE_API __declspec(dllexport)
#	else
#		define XRCORE_API __declspec(dllimport)
#	endif
#endif

class XRCORE_API xrFS {
public:
    virtual bool exists(const std::string& path) const;
    virtual uint64_t file_size(const std::string& path) const;
    virtual bool read_file(const std::string& path, std::vector<char>& out) const;
    virtual bool write_file(const std::string& path, const std::vector<char>& data);
    virtual bool remove(const std::string& path);
    virtual bool create_directory(const std::string& path);
    virtual bool rename(const std::string& from, const std::string& to);
    virtual std::string get_filename(const std::string& path) const;
    virtual std::string join_path(const std::string& a, const std::string& b) const;
    virtual std::vector<std::string> list_files(const std::string& dir, const std::string& ext = "") const;

    // Parallel batch read via CTaskManager — reads multiple files concurrently
    bool read_files_parallel(
        const std::vector<std::string>& paths,
        std::vector<std::vector<char>>& results) const;

    // Recursively pack an entire directory tree into a zdb archive
    // (ZIP-контейнер + zstd, читается 7-Zip 21+). Uses xrArchiver::pack_zdb.
    // Archive member names are relative to base_dir ('/' separators).
    bool pack_zdb_tree(
        const std::string& output_dir,
        const std::string& archive_name,
        const std::string& base_dir,
        int compression_level = 3) const;

    // ---- Virtual file system ----
    //
    // A mounted zdb archive (pack_zdb_tree output) makes its entries addressable
    // by relative names ('configs/system.ltx', case-insensitive, '/' or '\').
    // Files physically present under the real data tree (set_data_root) take
    // precedence and "virtually override" archive entries on every lookup.
    //
    // Resolve order in virtual_exists / virtual_file_size / read_virtual:
    //   1. real data tree file  -> read from disk
    //   2. mounted archive      -> decompressed (zstd) on the fly
    //   3. otherwise not found

    // Real (unpacked) data tree whose files override the archive.
    void set_data_root(const std::string& real_dir);
    void clear_data_root();

    // Mount a zdb archive, appending it to the set of mounted archives.
    // Entry lookups and reads are thread-safe (read-only after mount;
    // mmap-backed, lazy decompression). The same virtual key may resolve in
    // any mounted archive; the first archive whose index contains it wins.
    // virtual_root (e.g. "E:\\...\\gamedata") lets absolute paths be passed to
    // virtual_exists/read_virtual: anything under that prefix is stripped to the
    // archive's relative name before lookup.
    bool mount_zdb(const std::string& archive_path, const std::string& virtual_root = "");
    // Mount many archives at once: every archive is mmap'd and its central
    // directory parsed in parallel across the worker pool, then all indexes are
    // merged into a single flat O(1) lookup table. Returns how many mounted.
    size_t mount_zdb_many(const std::vector<std::string>& archive_paths,
                          const std::string& virtual_root = "");
    // Detach and reset everything back to a not-mounted, no-data-root state.
    void unmount_zdb();
    bool is_mounted() const;
    size_t mounted_count() const;

    bool virtual_exists(const std::string& vpath) const;
    uint64_t virtual_file_size(const std::string& vpath) const;
    uint32_t virtual_crc(const std::string& vpath) const;
    bool read_virtual(const std::string& vpath, std::vector<char>& out) const;
    // Parallel batch read: resolves the whole list against one flat-index
    // snapshot, then decompresses/reads the entries across the worker pool.
    // results[i] gets the entry bytes; returns false if any single read failed
    // (failed slots are left empty). Same disk-override semantics as
    // read_virtual per element.
    bool read_virtual_many(const std::vector<std::string>& vpaths,
                           std::vector<std::vector<char>>& results) const;
    // Asynchronous warm-up: decodes the listed archive entries in PARALLEL and
    // stores them in an internal cache so subsequent read_virtual() calls for
    // those keys become memcpy instead of zstd decompression. Only entries that
    // live in mounted archives and are NOT overridden by a real file are cached;
    // oversized batches are ignored. Safe to call at any time; never blocks the
    // caller beyond the parallel decode itself.
    void prefetch_virtual(const std::vector<std::string>& vpaths) const;

    // Index-only accessors (no disk stat). Used by the locator for fast,
    // parallel registration of mounted archive entries.
    std::vector<std::string> list_archive_files() const;
    // Only the entries of the most recently mounted archive (empty if none).
    std::vector<std::string> list_last_archive_files() const;
    std::vector<std::string> list_disk_files() const;
    bool archive_meta(const std::string& key, uint32_t& uncompSize, uint32_t& crc,
                      uint32_t* mtime = nullptr) const;

    // Combined listing: real data tree files (normalized) + archive entry names.
    std::vector<std::string> list_virtual_files() const;

    static xrFS& instance();

    virtual ~xrFS();

private:
    struct MountedArchive;
    // Flat, merged lookup table: one lookup per key instead of scanning every
    // mounted archive. Rebuilt on mount/unmount; published via shared_ptr so
    // concurrent readers keep a consistent, alive snapshot.
    struct FlatIndex;
    std::string m_data_root;
    std::string m_virtual_root;
    std::vector<std::shared_ptr<MountedArchive>> m_archives;
    std::shared_ptr<const FlatIndex> m_flat;
    mutable std::mutex m_mtx;
    static constexpr size_t kCacheShards = 16;
    struct CacheShard {
        std::mutex mtx;
        std::unordered_map<std::string, std::vector<char>> map;
    };
    mutable std::array<CacheShard, kCacheShards> m_cacheShards;
    mutable std::atomic<size_t> m_cacheBytes{ 0 };
    static size_t cache_shard_index(const std::string& key);
    void rebuild_flat_locked();

    static std::string normalize_path(const std::string& path); // unify separators + lowercase
    static std::string vfs_key(const std::string& path);   // normalize + lowercase, '/'
    static std::string vfs_disk_path(const std::string& root, const std::string& key);
    static std::string vfs_resolve(const std::string& vpath, const std::string& vroot);
};

inline xrFS& fs() { return xrFS::instance(); }
