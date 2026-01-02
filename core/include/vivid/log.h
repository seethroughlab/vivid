#pragma once

// Vivid - Unified Logging System
// Provides DEBUG/INFO/WARN/ERROR log levels with optional file output

#include <string>
#include <sstream>
#include <functional>

namespace vivid {

/**
 * @brief Log level enumeration
 */
enum class LogLevel {
    Debug = 0,   ///< Verbose debug information
    Info = 1,    ///< General information
    Warn = 2,    ///< Warnings (non-fatal issues)
    Error = 3,   ///< Errors (may affect functionality)
    None = 4     ///< Disable all logging
};

/**
 * @brief Global logger configuration and output
 *
 * Usage:
 *   vivid::Log::info() << "Processing file: " << filename;
 *   vivid::Log::error() << "Failed to load: " << path;
 *   vivid::Log::debug() << "Frame time: " << ms << "ms";
 *
 * Configure:
 *   vivid::Log::setLevel(vivid::LogLevel::Debug);  // Show all messages
 *   vivid::Log::setLevel(vivid::LogLevel::Warn);   // Show warnings and errors only
 */
class Log {
public:
    /**
     * @brief Stream-style log entry that writes on destruction
     */
    class Entry {
    public:
        Entry(LogLevel level, const char* file, int line);
        ~Entry();

        // Non-copyable, movable
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        Entry(Entry&& other) noexcept;
        Entry& operator=(Entry&& other) = delete;

        /// Stream operator for building log message
        template<typename T>
        Entry& operator<<(const T& value) {
            if (m_active) {
                m_stream << value;
            }
            return *this;
        }

    private:
        LogLevel m_level;
        const char* m_file;
        int m_line;
        std::ostringstream m_stream;
        bool m_active;
    };

    /// Create log entries at each level
    static Entry debug(const char* file = __builtin_FILE(), int line = __builtin_LINE());
    static Entry info(const char* file = __builtin_FILE(), int line = __builtin_LINE());
    static Entry warn(const char* file = __builtin_FILE(), int line = __builtin_LINE());
    static Entry error(const char* file = __builtin_FILE(), int line = __builtin_LINE());

    /// Set minimum log level (messages below this level are discarded)
    static void setLevel(LogLevel level);

    /// Get current log level
    static LogLevel getLevel();

    /// Set custom log callback (receives level, file, line, message)
    using Callback = std::function<void(LogLevel, const char*, int, const std::string&)>;
    static void setCallback(Callback cb);

    /// Clear custom callback (reverts to default stderr output)
    static void clearCallback();

    /// Convert level to string
    static const char* levelToString(LogLevel level);

private:
    friend class Entry;
    static void write(LogLevel level, const char* file, int line, const std::string& message);

    static LogLevel s_level;
    static Callback s_callback;
};

} // namespace vivid

// Convenience macros with automatic file/line capture
#define VIVID_LOG_DEBUG() vivid::Log::debug(__FILE__, __LINE__)
#define VIVID_LOG_INFO()  vivid::Log::info(__FILE__, __LINE__)
#define VIVID_LOG_WARN()  vivid::Log::warn(__FILE__, __LINE__)
#define VIVID_LOG_ERROR() vivid::Log::error(__FILE__, __LINE__)
