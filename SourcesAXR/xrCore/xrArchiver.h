#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <ostream>

#if !defined(XRCORE_API)
#	if defined(XRCORE_EXPORTS)
#		define XRCORE_API __declspec(dllexport)
#	else
#		define XRCORE_API __declspec(dllimport)
#	endif
#endif

class XRCORE_API xrArchiver {
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

    static uint32_t crc32(const void* data, size_t size);

    static bool pack_db(
        const std::string& output_dir,
        const std::string& archive_name,
        const std::vector<std::string>& file_paths,
        const std::string& base_dir,
        const std::string& entry_point,
        bool auto_load);

    static bool pack_zdb(
        const std::string& output_dir,
        const std::string& archive_name,
        const std::vector<std::string>& file_paths,
        const std::string& base_dir,
        int compression_level);

    // "Умная" упаковка всей дерева данных (адаптировано из test_pack/smartpack):
    //   levels/<lv>/** где <lv> не начинается с "mp_" -> levels/<lv>.zdb
    //   levels/mp_*/**                             -> mp/<lv>.zdb
    //   configs/** или scripts/**                  -> configs_scripts.zdb
    //   всё остальное                              -> resource_bins сбалансированных
    //                                                 по суммарному размеру resources.N.zdb
    // Дерево собирается рекурсивно из base_dir, имена членов в архивах — относительно
    // base_dir. Каждый архив пакуется через многопоточный pack_zdb, сами архивы
    // строятся параллельно через CTaskManager. Результаты (если results != nullptr)
    // заполняются для каждого построенного архива в порядке его обработки.
    // Возвращает true только если все архивы собраны успешно.
    struct ZdbResult {
        std::string folder;
        std::string archive;
        uint64_t uncompressed = 0;  // сумма исходных размеров файлов
        uint64_t compressed = 0;    // размер получившегося архива (0 если файл отсутствует)
    };
    static bool pack_zdb_smart(
        const std::string& base_dir,
        const std::string& output_root,
        std::vector<ZdbResult>* results = nullptr,
        unsigned int resource_bins = 4,
        int compression_level = 3);

private:
    static void fill_tar_header(char header[512], const std::string& filename, uint64_t filesize);

    static void write_u32(std::ostream&, uint32_t);
    static void write_u64(std::ostream&, uint64_t);
    static constexpr uint32_t MAGIC = 0x5A53544C; // 'LZST' LE
    static constexpr uint32_t VERSION = 1;
};