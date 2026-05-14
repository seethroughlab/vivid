#pragma once
// direct_param_queue.h — Lock-free SPSC ring buffer for MCP plugin param delivery.
//
// Producer: main thread (main_thread_update, parses _*_direct_params string param)
// Consumer: audio thread (build_macro_events / send_macro_params)
//
// Values are normalized [0, 1].  CLAP and AU operators rescale to native range at drain.
// Capacity 256 entries is sufficient for any plugin param set; overflows are dropped.

#include <atomic>
#include <array>
#include <cstdint>
#include <cstdlib>  // strtoul, strtod
#include <string>

struct DirectParamQueue {
    struct Entry { uint32_t id; double val; };
    static constexpr int kCap = 256;

    std::array<Entry, kCap> buf_{};
    std::atomic<int> write_{0};
    std::atomic<int> read_{0};

    // Called from main thread only.
    bool push(uint32_t id, double val) {
        int w    = write_.load(std::memory_order_relaxed);
        int next = (w + 1) % kCap;
        if (next == read_.load(std::memory_order_acquire)) return false; // full
        buf_[w] = {id, val};
        write_.store(next, std::memory_order_release);
        return true;
    }

    // Called from audio thread only.
    bool pop(Entry& e) {
        int r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false; // empty
        e = buf_[r];
        read_.store((r + 1) % kCap, std::memory_order_release);
        return true;
    }
};

// Parse "id:val id:val ..." into the queue.  Called from main thread.
// Returns the number of entries pushed.
static inline int direct_param_queue_parse_and_push(
        DirectParamQueue& q, const std::string& s) {
    int pushed = 0;
    const char* p = s.c_str();
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char* e1;
        unsigned long id = strtoul(p, &e1, 10);
        if (e1 == p || *e1 != ':') break;
        p = e1 + 1;
        char* e2;
        double val = strtod(p, &e2);
        if (e2 == p) break;
        q.push(static_cast<uint32_t>(id), val);
        ++pushed;
        p = e2;
    }
    return pushed;
}
