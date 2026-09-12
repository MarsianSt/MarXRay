// xrAsyncLogger.h
#pragma once

#include <string>
#include <vector>
#include <queue>
#include <fstream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

typedef void (*LogCallback)(LPCSTR string);

enum class LogLevel {
    Info,
    Warning,
    Error,
    Debug
};

struct LogMessage {
    LogLevel level;
    const char* module;   // статический литерал, не удаляем
    std::string text;
    std::string timestamp;
    unsigned long thread_id;  // ID потока-отправителя
};

class XRCORE_API xrAsyncLogger {
public:
    static xrAsyncLogger& instance();

    void start(const char* engineLogPath, const char* debugLogPath);
    void stop();
    void flush();

    void initialize_logs(bool no_log = false);

    void info(const char* module, const char* format, ...);
    void warning(const char* module, const char* format, ...);
    void error(const char* module, const char* format, ...);
    void debug(const char* module, const char* format, ...);

    void info(const char* format, ...);
    void warning(const char* format, ...);
    void error(const char* format, ...);
    void debug(const char* format, ...);

    void vininfo(const char* module, const char* format, va_list args);
    void vinwarning(const char* module, const char* format, va_list args);
    void vinerror(const char* module, const char* format, va_list args);
    void vindebug(const char* module, const char* format, va_list args);

    // LogCB support — callback is invoked from writer thread after formatting
    LogCallback set_log_callback(LogCallback cb);
    LogCallback get_log_callback() const;

    const char* get_engine_log_path() const { return m_engineLogPath.c_str(); }
    const char* get_debug_log_path() const { return m_debugLogPath.c_str(); }

    // Centralized formatting: [HH:MM:SS.mmm][Level][Module] Message
    static void format_message(const LogMessage& msg, string4096& out);

private:
    xrAsyncLogger() = default;
    ~xrAsyncLogger() { stop(); }
    xrAsyncLogger(const xrAsyncLogger&) = delete;
    xrAsyncLogger& operator=(const xrAsyncLogger&) = delete;
    void archive_current_logs();
    std::string get_current_timestamp();

    void writer_thread();
    void push_message(LogLevel level, const char* module, const std::string& text);
    void write_message(const LogMessage& msg);

    std::string m_engineLogPath;
    std::string m_debugLogPath;
    std::string m_logsDir;
    std::ofstream m_engineFile;
    std::ofstream m_debugFile;
    std::queue<LogMessage> m_queue;
    std::vector<LogMessage> m_preInitBuffer;  // хранит сообщения до вызова start()
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<LogCallback> m_logCB{ nullptr };
    bool m_started = false;
    static constexpr size_t MAX_PREINIT_MSGS = 4096;  // лимит pre-init буфера
};

#define VPUSH(a)	((a).x), ((a).y), ((a).z)

// Every TU that uses the Log* macros must have LOG_MODULE declared. Provide a
// sensible default here; a TU that wants its own module label defines LOG_MODULE
// BEFORE including this header (e.g. '#define LOG_MODULE "xrCore"').
#ifndef LOG_MODULE
#define LOG_MODULE "engine"
#endif

#ifndef LogInfo
#define LogInfo(fmt, ...)    xrAsyncLogger::instance().info(LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogWarning(fmt, ...) xrAsyncLogger::instance().warning(LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogError(fmt, ...)   xrAsyncLogger::instance().error(LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogDebug(fmt, ...)   xrAsyncLogger::instance().debug(LOG_MODULE, fmt, ##__VA_ARGS__)
#endif

// Backward-compatible global LogFile pointer (used by XR_IOConsole, Text_Console, console_commands)
extern XRCORE_API xr_vector<shared_str>* LogFile;