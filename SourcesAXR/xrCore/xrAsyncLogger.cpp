#include "stdafx.h"
#include "xrAsyncLogger.h"
#include "xrStatusConsole.h"
#include "xrArchiver.h"
#include "xrFS.h"
#include <chrono>
#include <thread>

static constexpr size_t MSG_BUF_SIZE = 2048;

xrAsyncLogger& xrAsyncLogger::instance() {
    static xrAsyncLogger s;
    return s;
}

LogCallback xrAsyncLogger::set_log_callback(LogCallback cb) {
    return m_logCB.exchange(cb, std::memory_order_relaxed);
}

LogCallback xrAsyncLogger::get_log_callback() const {
    return m_logCB.load(std::memory_order_relaxed);
}

void xrAsyncLogger::start(const char* engineLogPath, const char* debugLogPath) {
    if (m_started) return;
    m_engineFile.open(engineLogPath, std::ios::out | std::ios::app);
    m_debugFile.open(debugLogPath, std::ios::out | std::ios::app);
    m_running = true;
    m_thread = std::thread(&xrAsyncLogger::writer_thread, this);
    m_started = true;

    // Сбрасываем pre-init буфер в очередь — ни одно сообщение не потеряно
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& msg : m_preInitBuffer) {
            m_queue.emplace(std::move(msg));
        }
        m_preInitBuffer.clear();
        m_preInitBuffer.shrink_to_fit();
        m_cv.notify_one();
    }
}

void xrAsyncLogger::stop() {
    if (!m_started) return;
    m_running = false;
    m_cv.notify_one();
    if (m_thread.joinable()) m_thread.join();

    flush();

    if (m_engineFile.is_open()) m_engineFile.close();
    if (m_debugFile.is_open()) m_debugFile.close();
    archive_current_logs();
    m_started = false;
}

void xrAsyncLogger::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_queue.empty()) {
        auto& msg = m_queue.front();
        write_message(msg);
        m_queue.pop();
    }
    if (m_engineFile.is_open()) m_engineFile.flush();
    if (m_debugFile.is_open()) m_debugFile.flush();
}

void xrAsyncLogger::write_message(const LogMessage& msg) {
    const char* lvl_str = "Info";
    switch (msg.level) {
        case LogLevel::Info:    lvl_str = "Info";    break;
        case LogLevel::Warning: lvl_str = "Warning"; break;
        case LogLevel::Error:   lvl_str = "Error";   break;
        case LogLevel::Debug:   lvl_str = "Debug";   break;
    }

    // Centralized formatted string: [HH:MM:SS.mmm][Level][Module][TID] Message
    string4096 formatted;
    xr_sprintf(formatted, "[%s][%s][%s][%lu] %s",
        msg.timestamp.c_str(), lvl_str, msg.module, msg.thread_id, msg.text.c_str());

    // Write to engine log (Info and Error only)
    if (m_engineFile.is_open() && (msg.level == LogLevel::Info || msg.level == LogLevel::Error))
        m_engineFile << formatted << std::endl;
    // Write to debug log (all levels)
    if (m_debugFile.is_open())
        m_debugFile << formatted << std::endl;

    // Feed the status console — fully formatted, same as log file
    CXRStatusConsole::instance().add_line(formatted);

    // Invoke registered callback — fully formatted, same as log file
    LogCallback cb = m_logCB.load(std::memory_order_relaxed);
    if (cb)
        cb(formatted);
}

void xrAsyncLogger::format_message(const LogMessage& msg, string4096& out) {
    const char* lvl_str = "Info";
    switch (msg.level) {
        case LogLevel::Info:    lvl_str = "Info";    break;
        case LogLevel::Warning: lvl_str = "Warning"; break;
        case LogLevel::Error:   lvl_str = "Error";   break;
        case LogLevel::Debug:   lvl_str = "Debug";   break;
    }
    xr_sprintf(out, "[%s][%s][%s][%lu] %s",
        msg.timestamp.c_str(), lvl_str, msg.module, msg.thread_id, msg.text.c_str());
}

void xrAsyncLogger::writer_thread() {
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });
        if (!m_running) break;
        auto msg = std::move(m_queue.front());
        m_queue.pop();
        lock.unlock();

        write_message(msg);
    }
    // Drain remaining messages on shutdown
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_queue.empty()) {
        auto& msg = m_queue.front();
        write_message(msg);
        m_queue.pop();
    }
}

std::string xrAsyncLogger::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    int ms_int = static_cast<int>(ms.count());
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&tt);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
        tm->tm_hour, tm->tm_min, tm->tm_sec, ms_int);
    return buf;
}

void xrAsyncLogger::push_message(LogLevel level, const char* module, const std::string& text) {
    std::string ts = get_current_timestamp();
    unsigned long tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started) {
        // Буферизуем сообщения до инициализации логера
        if (m_preInitBuffer.size() < MAX_PREINIT_MSGS) {
            m_preInitBuffer.emplace_back(LogMessage{ level, module, text, ts, tid });
        }
        return;
    }
    m_queue.emplace(LogMessage{ level, module, text, ts, tid });
    m_cv.notify_one();
}

static std::string format_string(const char* format, va_list args) {
    char buf[MSG_BUF_SIZE];
    vsnprintf(buf, MSG_BUF_SIZE, format, args);
    buf[MSG_BUF_SIZE - 1] = 0;
    return std::string(buf);
}

void xrAsyncLogger::vininfo(const char* module, const char* format, va_list args) {
    push_message(LogLevel::Info, module, format_string(format, args));
}
void xrAsyncLogger::vinwarning(const char* module, const char* format, va_list args) {
    push_message(LogLevel::Warning, module, format_string(format, args));
}
void xrAsyncLogger::vinerror(const char* module, const char* format, va_list args) {
    push_message(LogLevel::Error, module, format_string(format, args));
}
void xrAsyncLogger::vindebug(const char* module, const char* format, va_list args) {
    push_message(LogLevel::Debug, module, format_string(format, args));
}

void xrAsyncLogger::info(const char* module, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vininfo(module, format, args);
    va_end(args);
}
void xrAsyncLogger::warning(const char* module, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vinwarning(module, format, args);
    va_end(args);
}
void xrAsyncLogger::error(const char* module, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vinerror(module, format, args);
    va_end(args);
}
void xrAsyncLogger::debug(const char* module, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vindebug(module, format, args);
    va_end(args);
}

void xrAsyncLogger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vininfo("Core", format, args);
    va_end(args);
}
void xrAsyncLogger::warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vinwarning("Core", format, args);
    va_end(args);
}
void xrAsyncLogger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vinerror("Core", format, args);
    va_end(args);
}
void xrAsyncLogger::debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vindebug("Core", format, args);
    va_end(args);
}

void xrAsyncLogger::initialize_logs(bool no_log) {
    if (m_started) return;

    string_path logsDir = "";
    if (FS.path_exist("$logs$"))
        FS.update_path(logsDir, "$logs$", "");

    string_path latestPath, debugPath;
    if (logsDir[0]) {
        strconcat(sizeof(latestPath), latestPath, logsDir, "latest.log");
        strconcat(sizeof(debugPath), debugPath, logsDir, "debug.log");
    }
    else {
        xr_strcpy(latestPath, "latest.log");
        xr_strcpy(debugPath, "debug.log");
    }

    m_logsDir = logsDir;
    m_engineLogPath = latestPath;
    m_debugLogPath = debugPath;

    if (logsDir[0] && !no_log) {
        string_path archiveDir;
        strconcat(sizeof(archiveDir), archiveDir, logsDir, "archives\\");
        fs().create_directory(archiveDir);
    }

    if (!no_log) {
        m_engineFile.open(latestPath, std::ios::out | std::ios::trunc);
        m_debugFile.open(debugPath, std::ios::out | std::ios::trunc);
        m_running = true;
        m_thread = std::thread(&xrAsyncLogger::writer_thread, this);
        m_started = true;

        // Сбрасываем pre-init буфер — сообщения, пришедшие до инициализации
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& msg : m_preInitBuffer) {
                m_queue.emplace(std::move(msg));
            }
            m_preInitBuffer.clear();
            m_preInitBuffer.shrink_to_fit();
            m_cv.notify_one();
        }
    }
}

void xrAsyncLogger::archive_current_logs() {
    if (m_logsDir.empty()) return;

    string_path archiveDir;
    strconcat(sizeof(archiveDir), archiveDir, m_logsDir.c_str(), "archives\\");
    fs().create_directory(archiveDir);

    string64 ts;   timestamp(ts);
    string64 fname; xr_sprintf(fname, "log_%s.zst", ts);

    std::vector<std::string> files = { m_engineLogPath, m_debugLogPath };
    xrArchiver::compress_files_sequential(archiveDir, fname, files, 3);
}
