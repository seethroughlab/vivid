#pragma once
// Shared CLAP host infrastructure used by CLAPInstrument and CLAPEffect.
// Everything that doesn't reference operator instance state lives here.

#include "operator_api/types.h"
#include "shared/plugin_common/base64.h"

#include <clap/clap.h>
#include <clap/ext/state.h>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>

namespace {

// Reference count per binary path — prevents entry->deinit() from tearing
// down a library's global state while a second handle (e.g. from
// reload_for_rate_change) still depends on it. Only call deinit() when
// count drops to 0. All accesses are main-thread-only.
static std::unordered_map<std::string, int> g_clap_entry_refs;

// ---------------------------------------------------------------------------
// Event list — generic CLAP event queue (128 slots, 64 bytes each)
// ---------------------------------------------------------------------------

static constexpr int kMaxEvents   = 256;
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

// Thin wrappers over the shared canonical base64 (operators/shared/plugin_common/
// base64.h) — keep the host-local names so call sites are unchanged. (audit 09-F1)
static std::string b64_encode(const uint8_t* data, size_t len) {
    return vivid::plugin_common::base64_encode(data, len);
}
static std::vector<uint8_t> b64_decode(const std::string& s) {
    return vivid::plugin_common::base64_decode(s);
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
    struct ParamEntry {
        clap_id id;
        double  min_val;
        double  max_val;
        double  default_val;
        int     step_count;  // 0=continuous, >0=discrete
        char    name[CLAP_NAME_SIZE];
    };
    std::vector<ParamEntry> params;

    ~PluginHandle() {
        // Caller is responsible for calling stop_processing (audio thread)
        // and deactivate (main thread) before destructing.
        if (plugin) {
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (entry) {
            if (!path.empty()) {
                auto it = g_clap_entry_refs.find(path);
                if (it != g_clap_entry_refs.end() && --it->second <= 0) {
                    g_clap_entry_refs.erase(it);
                    entry->deinit();
                }
            } else {
                entry->deinit();
            }
            entry = nullptr;
        }
        if (library) {
            dlclose(library);
            library = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Cache CLAP param info from the params extension into PluginHandle::params.
// Must be called on the main thread, after plugin init.
// ---------------------------------------------------------------------------

static void clap_cache_params(PluginHandle* h) {
    auto* params_ext = reinterpret_cast<const clap_plugin_params_t*>(
        h->plugin->get_extension(h->plugin, CLAP_EXT_PARAMS));
    if (!params_ext) return;
    uint32_t n = params_ext->count(h->plugin);
    h->params.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        clap_param_info_t info{};
        params_ext->get_info(h->plugin, i, &info);
        auto& e       = h->params[i];
        e.id          = info.id;
        e.min_val     = info.min_value;
        e.max_val     = info.max_value;
        e.default_val = info.default_value;
        e.step_count  = (info.flags & CLAP_PARAM_IS_STEPPED)
                        ? static_cast<int>(info.max_value - info.min_value) : 0;
        std::strncpy(e.name, info.name, CLAP_NAME_SIZE - 1);
        e.name[CLAP_NAME_SIZE - 1] = '\0';
    }
}

// ---------------------------------------------------------------------------
// CLAP param list serializer — builds JSON array from a PluginHandle's cached
// param info. Returns "[]" when no plugin is loaded or plugin has no params.
// ---------------------------------------------------------------------------

static std::string clap_params_to_json(const PluginHandle* h,
                                        const clap_plugin_params_t* params_ext = nullptr) {
    if (!h || h->params.empty()) return "[]";
    std::string json = "[";
    bool first = true;
    for (const auto& p : h->params) {
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"";
        for (const char* c = p.name; *c; ++c) {
            if (*c == '"')       json += "\\\"";
            else if (*c == '\\') json += "\\\\";
            else                 json += *c;
        }
        json += "\",\"id\":";
        json += std::to_string(p.id);
        json += ",\"min\":";
        json += std::to_string(p.min_val);
        json += ",\"max\":";
        json += std::to_string(p.max_val);
        json += ",\"default\":";
        json += std::to_string(p.default_val);
        json += ",\"step_count\":";
        json += std::to_string(p.step_count);
        if (params_ext && h->plugin) {
            double val = 0.0;
            if (params_ext->get_value(h->plugin, p.id, &val)) {
                json += ",\"value\":";
                json += std::to_string(val);
            }
        }
        json += "}";
    }
    json += "]";
    return json;
}

// ---------------------------------------------------------------------------
// Build a CLAP transport event from the Vivid audio context.
// Call once per process_audio() and point proc.transport at the result.
// Vivid's metronome always runs, so IS_PLAYING is always set.
// ---------------------------------------------------------------------------

static clap_event_transport_t clap_build_transport(const VividAudioContext* ctx) {
    clap_event_transport_t t{};
    t.header = { sizeof(t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };

    const double   bpm   = static_cast<double>(ctx->metronome_bpm > 0.f ? ctx->metronome_bpm : 120.f);
    const double   beats = ctx->metronome_beats_elapsed;
    const uint32_t bpb   = ctx->metronome_beats_per_bar > 0 ? ctx->metronome_beats_per_bar : 4;

    t.tempo     = bpm;
    t.tempo_inc = 0.0;

    t.song_pos_beats   = static_cast<clap_beattime>(beats * CLAP_BEATTIME_FACTOR);
    t.song_pos_seconds = static_cast<clap_sectime>(ctx->time * CLAP_SECTIME_FACTOR);

    const int32_t bar_number = static_cast<int32_t>(beats / static_cast<double>(bpb));
    t.bar_number = bar_number;
    t.bar_start  = static_cast<clap_beattime>(
        static_cast<int64_t>(bar_number) * static_cast<int64_t>(bpb) * CLAP_BEATTIME_FACTOR);

    t.tsig_num   = static_cast<uint16_t>(bpb);
    t.tsig_denom = 4;

    t.flags = CLAP_TRANSPORT_HAS_TEMPO
            | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
            | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE
            | CLAP_TRANSPORT_HAS_TIME_SIGNATURE
            | CLAP_TRANSPORT_IS_PLAYING;

    return t;
}

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
