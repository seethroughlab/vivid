#include "app/log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace vivid {

const char* log_level_str(LogLevel l) {
    switch (l) {
        case LogLevel::Debug:   return "debug";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warn";
        case LogLevel::Error:   return "error";
    }
    return "info";
}

Logger::Logger() {
    start_ns_ = std::chrono::steady_clock::now().time_since_epoch().count();
}

double Logger::now() const {
    const int64_t ns = std::chrono::steady_clock::now().time_since_epoch().count();
    return static_cast<double>(ns - start_ns_) * 1e-9;
}

void Logger::append(LogLevel level, const char* msg, bool to_stderr) {
    if (to_stderr) std::fprintf(stderr, "[vivid] %s\n", msg);
    LogEntry e;
    e.id = next_id_++;
    e.t = now();
    e.level = level;
    std::snprintf(e.msg, sizeof(e.msg), "%s", msg);
    store_.push_back(e);
    while (store_.size() > kMaxStore) store_.pop_front();
}

uint64_t Logger::log(LogLevel level, const char* fmt, ...) {
    char buf[240];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const uint64_t id = next_id_;   // append() will consume this
    append(level, buf, /*to_stderr=*/true);
    return id;
}

void Logger::rt_log(LogLevel level, const char* msg) {
    const uint32_t h = head_.load(std::memory_order_relaxed);
    const uint32_t t = tail_.load(std::memory_order_acquire);
    if (h - t >= kRingCap) {                       // ring full: drop, account for it
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    RingSlot& s = ring_[h & (kRingCap - 1)];
    s.level = level;
    size_t n = 0;
    for (; msg && msg[n] && n < sizeof(s.msg) - 1; ++n) s.msg[n] = msg[n];
    s.msg[n] = '\0';
    head_.store(h + 1, std::memory_order_release);
}

void Logger::drain_rt() {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t h = head_.load(std::memory_order_acquire);
    for (; t != h; ++t) {
        const RingSlot& s = ring_[t & (kRingCap - 1)];
        append(s.level, s.msg, /*to_stderr=*/true);  // stderr write happens HERE (UI thread), not on audio
    }
    tail_.store(t, std::memory_order_release);

    const uint32_t dropped = dropped_.exchange(0, std::memory_order_relaxed);
    if (dropped) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "log ring overflow: %u audio-thread messages dropped", dropped);
        append(LogLevel::Warning, buf, /*to_stderr=*/true);
    }
}

}  // namespace vivid
