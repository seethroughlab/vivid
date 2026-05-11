#pragma once
// Shared CLAP host infrastructure used by CLAPInstrument and CLAPEffect.
// Everything that doesn't reference operator instance state lives here.

#include <clap/clap.h>
#include <clap/ext/state.h>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <cstring>
#include <sys/stat.h>

namespace {

// ---------------------------------------------------------------------------
// Event list — generic CLAP event queue (128 slots, 64 bytes each)
// ---------------------------------------------------------------------------

static constexpr int kMaxEvents   = 128;
static constexpr int kEventSlotSz = 64; // fits all CLAP event types

struct alignas(8) EventSlot { char data[kEventSlotSz]; };

struct EventList {
    EventSlot slots[kMaxEvents];
    int       count = 0;

    void clear() { count = 0; }

    template<class T>
    void push(const T& ev) {
        static_assert(sizeof(T) <= kEventSlotSz, "event too large for slot");
        if (count >= kMaxEvents) return;
        std::memcpy(slots[count++].data, &ev, sizeof(T));
    }

    static uint32_t clap_size(const clap_input_events_t* l) {
        return static_cast<uint32_t>(static_cast<EventList*>(l->ctx)->count);
    }
    static const clap_event_header_t* clap_get(const clap_input_events_t* l, uint32_t i) {
        return reinterpret_cast<const clap_event_header_t*>(
            static_cast<EventList*>(l->ctx)->slots[i].data);
    }

    clap_input_events_t as_clap_input() {
        return { this, clap_size, clap_get };
    }
};

// Output events: host must provide this but can ignore everything.
static bool clap_out_try_push(const clap_output_events_t*, const clap_event_header_t*) {
    return true;
}
static const clap_output_events_t kNullOutput = { nullptr, clap_out_try_push };

// ---------------------------------------------------------------------------
// Base64 encode/decode (RFC 4648, standard alphabet)
// ---------------------------------------------------------------------------

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kB64Table[(n >> 18) & 0x3F]);
        out.push_back(kB64Table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kB64Table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kB64Table[ n       & 0x3F] : '=');
    }
    return out;
}

static std::vector<uint8_t> b64_decode(const std::string& s) {
    static const int8_t kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<uint8_t> out;
    out.reserve((s.size() / 4) * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : s) {
        int v = kDec[c];
        if (v < 0) continue; // '=' and whitespace
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
            acc &= (1u << bits) - 1u;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// CLAP stream wrappers
// ---------------------------------------------------------------------------

struct VividOStream {
    std::vector<uint8_t> buf;
    clap_ostream_t clap = {};
    VividOStream() {
        clap.ctx = this;
        clap.write = [](const clap_ostream_t* s, const void* buf, uint64_t size) -> int64_t {
            auto* self = static_cast<VividOStream*>(s->ctx);
            const auto* p = static_cast<const uint8_t*>(buf);
            self->buf.insert(self->buf.end(), p, p + size);
            return static_cast<int64_t>(size);
        };
    }
};

struct VividIStream {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;
    clap_istream_t clap = {};
    VividIStream(const uint8_t* d, size_t n) : data(d), size(n) {
        clap.ctx = this;
        clap.read = [](const clap_istream_t* s, void* buf, uint64_t want) -> int64_t {
            auto* self = static_cast<VividIStream*>(s->ctx);
            size_t avail = self->size - self->pos;
            size_t n = std::min(static_cast<size_t>(want), avail);
            if (n == 0) return 0;
            std::memcpy(buf, self->data + self->pos, n);
            self->pos += n;
            return static_cast<int64_t>(n);
        };
    }
};

// ---------------------------------------------------------------------------
// PluginHandle — owns one loaded+initialized CLAP plugin instance
// ---------------------------------------------------------------------------

struct PluginHandle {
    void*                      library         = nullptr;
    const clap_plugin_entry_t* entry           = nullptr;
    const clap_plugin_t*       plugin          = nullptr;
    bool                       started         = false;  // start_processing called
    std::string                path;                     // bundle path that was loaded
    uint32_t                   latency_samples = 0;      // from CLAP_EXT_LATENCY
    uint32_t                   tail_samples    = 0;      // from CLAP_EXT_TAIL (0 = unknown/none)

    // Param info cache for macro mapping (populated after plugin init)
    struct ParamEntry { clap_id id; double min_val; double max_val; char name[CLAP_NAME_SIZE]; };
    std::vector<ParamEntry> params;

    ~PluginHandle() {
        // Caller is responsible for calling stop_processing (audio thread)
        // and deactivate (main thread) before destructing.
        if (plugin) {
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (entry) {
            entry->deinit();
            entry = nullptr;
        }
        if (library) {
            dlclose(library);
            library = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Bundle resolver — macOS .clap dirs → Contents/MacOS/<stem>
// On Linux, .clap files are flat .so; returns path unchanged.
// ---------------------------------------------------------------------------

static std::string clap_resolve_binary(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return path;
    if (!S_ISDIR(st.st_mode)) return path;

    std::string p(path);
    while (!p.empty() && p.back() == '/') p.pop_back();

    size_t slash = p.rfind('/');
    std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
    std::string stem = name;
    const std::string ext = ".clap";
    if (stem.size() > ext.size() &&
        stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0)
        stem.resize(stem.size() - ext.size());

    return p + "/Contents/MacOS/" + stem;
}

} // namespace
