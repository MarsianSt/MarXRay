#include "xrFS.h"
#include "TaskManager.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <atomic>

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
