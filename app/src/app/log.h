#pragma once
#include <atomic>
#include <cstdint>
#include <deque>

// ADR-0019 (E4): a minimal leveled logger. The trunk logged everything via
// `fprintf(stderr, "[vivid] …")` — no levels, no file, no in-app view. This is the
// thin replacement: a level + a message + a capped ring the UI can render, and a
// pass-through to stderr so nothing that works today stops working.
//
// Discipline (per the ADR): the logger is a passthrough + a record of *events*, never a
// second source of truth for health. Health stays HealthSnapshot; node badges read the
// node's own error state; the status dot reads severity(). The log only records.
//
// Threads: log()/entries()/drain_rt() are UI/main-thread only (a plain deque, no lock).
// The audio thread NEVER calls log() (it would format + allocate + touch the deque).
// It calls rt_log(level, "static literal") which memcpy's into a lock-free SPSC ring;
// drain_rt() moves those into the store on the UI thread once per frame — exactly the
// discipline platform/midi_input.h uses for CoreMIDI events.
namespace vivid {

enum class LogLevel { Debug, Info, Warning, Error };
const char* log_level_str(LogLevel);

struct LogEntry {
    uint64_t id = 0;         // monotonic; lets the frame loop find "new since last frame" (toasts)
    double   t  = 0.0;       // seconds since logger construction
    LogLevel level = LogLevel::Info;
    char     msg[240] = {};
};

class Logger {
public:
    Logger();

    // UI/main thread. Formats, writes through to stderr ("[vivid] …") exactly as before,
    // and appends to the capped store. Returns the new entry's id.
    uint64_t log(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

    // AUDIO thread. Copies a caller-owned string (static literal or stack buffer) into a
    // pre-allocated ring slot. No malloc, no lock, no stderr. Dropped if the ring is full.
    void rt_log(LogLevel level, const char* msg);

    // UI thread, once per frame: move ring entries into the store (stderr pass-through here,
    // NOT on the audio thread) and account for any drops.
    void drain_rt();

    const std::deque<LogEntry>& entries() const { return store_; }
    uint64_t next_id() const { return next_id_; }

private:
    void append(LogLevel level, const char* msg, bool to_stderr);
    double now() const;

    static constexpr size_t kMaxStore = 512;   // capped history for the in-app log view
    static constexpr uint32_t kRingCap = 256;  // power of two; audio-thread → UI-thread ring

    std::deque<LogEntry> store_;
    uint64_t next_id_ = 1;
    int64_t  start_ns_ = 0;

    struct RingSlot { LogLevel level; char msg[240]; };
    RingSlot ring_[kRingCap];
    std::atomic<uint32_t> head_{0};   // producer (audio thread)
    std::atomic<uint32_t> tail_{0};   // consumer (UI thread)
    std::atomic<uint32_t> dropped_{0};
};

}  // namespace vivid

// Thin call-site sugar so migrated sites read like the old fprintf. `app` is a vivid::App&.
#define VLOG_ERR(app, ...)  ((app).log.log(::vivid::LogLevel::Error,   __VA_ARGS__))
#define VLOG_WARN(app, ...) ((app).log.log(::vivid::LogLevel::Warning, __VA_ARGS__))
#define VLOG_INFO(app, ...) ((app).log.log(::vivid::LogLevel::Info,    __VA_ARGS__))
