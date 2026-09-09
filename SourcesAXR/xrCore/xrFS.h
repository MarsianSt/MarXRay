#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

class xrFS {
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

    static xrFS& instance();
    static void set_instance(std::unique_ptr<xrFS> new_fs);

    virtual ~xrFS() = default;

private:
    static std::unique_ptr<xrFS> s_instance;
};

inline xrFS& fs() { return xrFS::instance(); }
