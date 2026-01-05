// Vivid - Unified Logging System Implementation

#include <vivid/log.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstring>

namespace vivid {

// Static member initialization
LogLevel Log::s_level = LogLevel::Info;
Log::Callback Log::s_callback = nullptr;

// Entry implementation
Log::Entry::Entry(LogLevel level, const char* file, int line)
    : m_level(level)
    , m_file(file)
    , m_line(line)
    , m_active(level >= Log::s_level)
{
}

Log::Entry::~Entry() {
    if (m_active && !m_stream.str().empty()) {
        Log::write(m_level, m_file, m_line, m_stream.str());
    }
}

Log::Entry::Entry(Entry&& other) noexcept
    : m_level(other.m_level)
    , m_file(other.m_file)
    , m_line(other.m_line)
    , m_stream(std::move(other.m_stream))
    , m_active(other.m_active)
{
    other.m_active = false;  // Prevent double-write
}

// Log factory methods
Log::Entry Log::debug(const char* file, int line) {
    return Entry(LogLevel::Debug, file, line);
}

Log::Entry Log::info(const char* file, int line) {
    return Entry(LogLevel::Info, file, line);
}

Log::Entry Log::warn(const char* file, int line) {
    return Entry(LogLevel::Warn, file, line);
}

Log::Entry Log::error(const char* file, int line) {
    return Entry(LogLevel::Error, file, line);
}

// Configuration
void Log::setLevel(LogLevel level) {
    s_level = level;
}

LogLevel Log::getLevel() {
    return s_level;
}

void Log::setCallback(Callback cb) {
    s_callback = std::move(cb);
}

void Log::clearCallback() {
    s_callback = nullptr;
}

const char* Log::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None:  return "NONE";
    }
    return "?";
}

void Log::write(LogLevel level, const char* file, int line, const std::string& message) {
    // Use custom callback if set
    if (s_callback) {
        s_callback(level, file, line, message);
        return;
    }

    // Default output to stderr with timestamp and level
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    // Extract just the filename from path
    const char* filename = file;
    if (const char* slash = strrchr(file, '/')) {
        filename = slash + 1;
    }
    if (const char* backslash = strrchr(filename, '\\')) {
        filename = backslash + 1;
    }

    // Format: [HH:MM:SS.mmm] [LEVEL] message (file:line)
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostream& out = (level >= LogLevel::Warn) ? std::cerr : std::cout;

    out << "["
        << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
        << std::setfill('0') << std::setw(2) << tm_buf.tm_min << ":"
        << std::setfill('0') << std::setw(2) << tm_buf.tm_sec << "."
        << std::setfill('0') << std::setw(3) << ms.count()
        << "] [" << levelToString(level) << "] "
        << message;

    // Only show file:line for debug/warn/error
    if (level != LogLevel::Info) {
        out << " (" << filename << ":" << line << ")";
    }

    out << std::endl;
}

} // namespace vivid
