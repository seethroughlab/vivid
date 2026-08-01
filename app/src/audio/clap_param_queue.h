#pragma once

#include <atomic>
#include <cstdint>

// UI -> audio parameter changes for a hosted CLAP plugin: a lock-free SPSC ring. The UI thread pushes
// (a knob drag or an MCP set_param); the audio thread pops at the top of process() and applies them.
//
// Ph2 audit P2-01: push FULL -> DROP, never overwrite an unread slot. The previous version advanced the
// write cursor unconditionally, so when the UI outran the audio consumer it LAPPED the ring and a pop
// could read a {id, value} slot mid-overwrite — a genuine torn read / data race on the RT thread (the
// VST3 ParamQueue already dropped-on-full; this brings CLAP to parity and matches the "full -> drop,
// never blocks audio" row in app/docs/thread-safety.md).
//
// Dependency-free (id is a CLAP param id, i.e. clap_id == uint32_t) so it is unit-testable on the
// portable ThreadSanitizer leg without the CLAP/CoreFoundation headers.
namespace vivid::session {

struct ClapParamMsg { uint32_t id; double value; };

struct ClapParamQueue {
    static constexpr int N = 2048;
    ClapParamMsg buf[N];
    std::atomic<uint32_t> w{0}, r{0};

    bool push(uint32_t id, double v) {   // UI thread (producer)
        const uint32_t wi = w.load(std::memory_order_relaxed);
        if (wi - r.load(std::memory_order_acquire) >= static_cast<uint32_t>(N))
            return false;                // full -> drop (never taps a slot the consumer hasn't read)
        buf[wi % N] = { id, v };
        w.store(wi + 1, std::memory_order_release);
        return true;
    }
    bool pop(ClapParamMsg& m) {           // audio thread (consumer)
        const uint32_t ri = r.load(std::memory_order_relaxed);
        if (ri == w.load(std::memory_order_acquire)) return false;   // empty
        m = buf[ri % N];
        r.store(ri + 1, std::memory_order_release);
        return true;
    }
};

}  // namespace vivid::session
