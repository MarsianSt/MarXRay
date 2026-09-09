#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <ostream>

class xrArchiver {
public:
    // Сжать файлы в один архив.
    // output_dir – путь к папке (например, "D:/logs/archives")
    // archive_name – имя файла с расширением (например, "log_2025-06-15_14-30-00.zst")
    // file_paths – полные пути к исходным файлам
    // compression_level – уровень сжатия (1…22)
    // Возвращает true при успехе.
    static bool compress_files(
        const std::string& output_dir,
        const std::string& archive_name,
        const std::vector<std::string>& file_paths,
        int compression_level
    );

    static bool compress_files_sequential(
        const std::string& output_dir,
        const std::string& archive_name,
        const std::vector<std::string>& file_paths,
        int compression_level);

private:
    static void fill_tar_header(char header[512], const std::string& filename, uint64_t filesize);

    static void write_u32(std::ostream&, uint32_t);
    static void write_u64(std::ostream&, uint64_t);
    static constexpr uint32_t MAGIC = 0x5A53544C; // 'LZST' LE
    static constexpr uint32_t VERSION = 1;
};