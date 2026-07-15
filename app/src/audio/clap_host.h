#pragma once
// Minimal CLAP 1.2 plugin host — the peer of vst3_host_common.h's Vst3Handle. Loads a
// `.clap` bundle, creates + activates one plugin, caches its params, and exposes an
// RT-safe process primitive (note + param events -> audio). Decoupled from the session
// Track: the Track-aware render wrappers live in vst3_host.cpp and feed clap_run().
//
// Scope today: load/activate, params, note+param events, audio in/out, state (save/load).
// The plugin GUI (clap.gui) is intentionally not wired here yet.
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include "audio/base64.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid::session {

// --- UI->audio parameter changes (SPSC ring; plain values, CLAP is by param id) ---
struct ClapParamMsg { clap_id id; double value; };
struct ClapParamQueue {
    static constexpr int N = 2048;
    ClapParamMsg buf[N];
    std::atomic<uint32_t> w{0}, r{0};
    void push(clap_id id, double v) {
        uint32_t wi = w.load(std::memory_order_relaxed);
        buf[wi % N] = { id, v };
        w.store(wi + 1, std::memory_order_release);
    }
    bool pop(ClapParamMsg& m) {
        uint32_t ri = r.load(std::memory_order_relaxed);
        if (ri == w.load(std::memory_order_acquire)) return false;
        m = buf[ri % N];
        r.store(ri + 1, std::memory_order_release);
        return true;
    }
};

// --- RT event scratch: builds a CLAP input-event list per block (no alloc on the audio
// thread). note + param_value are the only core events we emit; both start with the header
// at offset 0, so get() returns the union's address regardless of the stored type. ---
struct ClapEventScratch {
    static constexpr uint32_t N = 4096;
    union Evt { clap_event_note_t note; clap_event_param_value_t pv; };
    Evt evts[N];
    uint32_t count = 0;
    clap_input_events_t  in{};
    clap_output_events_t out{};

    ClapEventScratch() {
        in.ctx = this;  in.size = &ev_size;  in.get = &ev_get;
        out.ctx = this; out.try_push = &ev_push;
    }
    static uint32_t CLAP_ABI ev_size(const clap_input_events_t* l) {
        return static_cast<const ClapEventScratch*>(l->ctx)->count;
    }
    static const clap_event_header_t* CLAP_ABI ev_get(const clap_input_events_t* l, uint32_t i) {
        auto* s = static_cast<const ClapEventScratch*>(l->ctx);
        return &s->evts[i].note.header;
    }
    // ADR-0015 (M2): a plugin's OWN events. A note effect (arpeggiator / chord generator) writes
    // its notes here — the host used to discard them, which is why a CLAP note effect could not
    // work at all. Only note-on/off are kept: they are what a note edge carries. Fixed capacity;
    // the audio thread never allocates.
    static bool CLAP_ABI ev_push(const clap_output_events_t* l, const clap_event_header_t* h) {
        auto* s = static_cast<ClapEventScratch*>(l->ctx);
        if (!s || !h || s->out_count >= N) return true;
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
        if (h->type != CLAP_EVENT_NOTE_ON && h->type != CLAP_EVENT_NOTE_OFF) return true;
        s->out_notes[s->out_count++] = *reinterpret_cast<const clap_event_note_t*>(h);
        return true;
    }
    clap_event_note_t out_notes[N]{};   // notes the PLUGIN produced this block (not `out`: that is the CLAP sink)
    uint32_t          out_count = 0;
    void clear() { count = 0; out_count = 0; }
    void add_note(bool on, int key, double vel, int32_t note_id, uint32_t time) {
        if (count >= N) return;
        clap_event_note_t& e = evts[count++].note;
        e.header.size = sizeof(clap_event_note_t); e.header.time = time;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF; e.header.flags = 0;
        e.note_id = note_id; e.port_index = 0; e.channel = 0; e.key = static_cast<int16_t>(key);
        e.velocity = vel;
    }
    void add_param(clap_id id, double v) {
        if (count >= N) return;
        clap_event_param_value_t& e = evts[count++].pv;
        e.header.size = sizeof(clap_event_param_value_t); e.header.time = 0;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = 0;
        e.param_id = id; e.cookie = nullptr;
        e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = v;
    }
};

// --- DSO refcount: a .clap bundle's entry->init()/deinit() must be called once per DSO
// even if we instantiate several plugins from it (mirrors g_vst3_bundle_refs). ---
struct ClapDso {
    const clap_plugin_entry_t* entry = nullptr;
#ifdef __APPLE__
    CFBundleRef bundle = nullptr;
#endif
    int refs = 0;
};
inline std::unordered_map<std::string, ClapDso>& clap_dsos() {
    static std::unordered_map<std::string, ClapDso> m;
    return m;
}
// Guards clap_dsos() — clap_load_plugin now runs on a background loader thread while
// ClapHandle::destroy() runs on the main thread, so the shared DSO map + refcounts must
// be serialized. (Pointers into an unordered_map stay valid across other insertions, so a
// ClapDso* held across the slow create_plugin() is safe as long as its ref is claimed.)
inline std::mutex& clap_dsos_mtx() { static std::mutex m; return m; }

struct ClapHandle {
    std::string bundle_path;
    const clap_plugin_entry_t* entry = nullptr;   // borrowed from the DSO cache
    const clap_plugin_t*       plugin = nullptr;
    clap_host_t                host{};

    const clap_plugin_params_t*      ext_params = nullptr;
    const clap_plugin_state_t*       ext_state = nullptr;
    const clap_plugin_audio_ports_t* ext_audio_ports = nullptr;
    const clap_plugin_note_ports_t*  ext_note_ports = nullptr;
    const clap_plugin_preset_load_t* ext_preset_load = nullptr;

    bool   activated = false, processing = false, has_note_in = false, has_note_out = false;
    uint32_t audio_in = 0, audio_out = 2, max_block = 0;
    double sample_rate = 48000.0;
    std::string name;
    std::vector<float> silence;   // max_block*2 zeros: silent input fed to instruments that
                                  // declare an audio-input port (CLAP wants the port present)

    struct ParamEntry { clap_id id; double min, max, def; std::string name, module; uint32_t flags = 0; };
    std::vector<ParamEntry> params;
    ClapParamQueue param_q;      // UI -> audio
    ClapEventScratch events;     // audio-thread scratch (built each block)

    void destroy() {
        if (plugin) {
            if (processing) plugin->stop_processing(plugin);
            if (activated)  plugin->deactivate(plugin);
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        std::lock_guard<std::mutex> lk(clap_dsos_mtx());
        auto& dsos = clap_dsos();
        auto it = dsos.find(bundle_path);
        if (it != dsos.end() && --it->second.refs <= 0) {
            if (it->second.entry) it->second.entry->deinit();
#ifdef __APPLE__
            if (it->second.bundle) CFRelease(it->second.bundle);
#endif
            dsos.erase(it);
        }
    }
    ~ClapHandle() { destroy(); }
};

// --- Minimal host callbacks (host_data == the ClapHandle) ---
inline const void* CLAP_ABI clap_host_get_extension(const clap_host_t*, const char*) { return nullptr; }
inline void CLAP_ABI clap_host_request_restart(const clap_host_t*) {}
inline void CLAP_ABI clap_host_request_process(const clap_host_t*) {}
inline void CLAP_ABI clap_host_request_callback(const clap_host_t*) {}

// Load `.clap` at `path`, create + activate its first plugin. Returns nullptr on any failure.
inline ClapHandle* clap_load_plugin(const std::string& path, double sample_rate, uint32_t max_frames) {
#ifdef __APPLE__
    // Acquire (or create) the DSO refcount entry and CLAIM a reference under the lock, so the
    // slow create_plugin() below (Surge's ctor scans its wavetable dir — seconds) runs UNLOCKED
    // on the loader thread without another thread erasing this DSO out from under `dso`.
    ClapDso* dso = nullptr;
    {
        std::lock_guard<std::mutex> lk(clap_dsos_mtx());
        auto& dsos = clap_dsos();
        auto it = dsos.find(path);
        if (it != dsos.end()) {
            dso = &it->second;
        } else {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(
                nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), true);
            if (!url) return nullptr;
            CFBundleRef bundle = CFBundleCreate(nullptr, url);
            CFRelease(url);
            if (!bundle) return nullptr;
            auto* entry = static_cast<const clap_plugin_entry_t*>(
                CFBundleGetDataPointerForName(bundle, CFSTR("clap_entry")));
            if (!entry || !entry->init(path.c_str())) { CFRelease(bundle); return nullptr; }
            dso = &dsos[path];
            dso->entry = entry; dso->bundle = bundle; dso->refs = 0;
        }
        dso->refs++;   // claimed; released by ClapHandle::destroy() or release_claim() on failure
    }
    auto release_claim = [&path]() {   // undo the claim on a failure path where no ClapHandle exists yet
        std::lock_guard<std::mutex> lk(clap_dsos_mtx());
        auto& dsos = clap_dsos();
        auto it = dsos.find(path);
        if (it != dsos.end() && --it->second.refs <= 0) {
            if (it->second.entry) it->second.entry->deinit();
            if (it->second.bundle) CFRelease(it->second.bundle);
            dsos.erase(it);
        }
    };

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        dso->entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) == 0) { release_claim(); return nullptr; }
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    if (!desc) { release_claim(); return nullptr; }

    auto* h = new ClapHandle();
    h->bundle_path = path;
    h->entry = dso->entry;
    h->sample_rate = sample_rate;
    h->name = desc->name ? desc->name : path;
    h->host.clap_version = CLAP_VERSION;
    h->host.host_data = h;
    h->host.name = "Vivid"; h->host.vendor = "Vivid"; h->host.url = "https://vivid.app"; h->host.version = "1.0";
    h->host.get_extension = &clap_host_get_extension;
    h->host.request_restart = &clap_host_request_restart;
    h->host.request_process = &clap_host_request_process;
    h->host.request_callback = &clap_host_request_callback;

    h->plugin = factory->create_plugin(factory, &h->host, desc->id);   // SLOW (unlocked, loader thread)
    if (!h->plugin || !h->plugin->init(h->plugin)) { delete h; return nullptr; }   // destroy() releases the claim
    // (the DSO reference was already claimed above, before create_plugin)

    h->ext_params      = static_cast<const clap_plugin_params_t*>(h->plugin->get_extension(h->plugin, CLAP_EXT_PARAMS));
    h->ext_state       = static_cast<const clap_plugin_state_t*>(h->plugin->get_extension(h->plugin, CLAP_EXT_STATE));
    h->ext_audio_ports = static_cast<const clap_plugin_audio_ports_t*>(h->plugin->get_extension(h->plugin, CLAP_EXT_AUDIO_PORTS));
    h->ext_note_ports  = static_cast<const clap_plugin_note_ports_t*>(h->plugin->get_extension(h->plugin, CLAP_EXT_NOTE_PORTS));
    h->ext_preset_load = static_cast<const clap_plugin_preset_load_t*>(h->plugin->get_extension(h->plugin, CLAP_EXT_PRESET_LOAD));
    if (!h->ext_preset_load)
        h->ext_preset_load = static_cast<const clap_plugin_preset_load_t*>(h->plugin->get_extension(h->plugin, CLAP_EXT_PRESET_LOAD_COMPAT));

    if (h->ext_note_ports) {
        h->has_note_in  = h->ext_note_ports->count(h->plugin, /*is_input*/ true) > 0;
        // ADR-0015 (M2): a note OUTPUT port means the plugin can generate notes (an arpeggiator,
        // a chord generator) — the host now drains them onto the node's note edge.
        h->has_note_out = h->ext_note_ports->count(h->plugin, /*is_input*/ false) > 0;
    }
    if (h->ext_audio_ports) {
        if (h->ext_audio_ports->count(h->plugin, /*is_input*/ true) > 0) {
            clap_audio_port_info_t info{};
            if (h->ext_audio_ports->get(h->plugin, 0, true, &info)) h->audio_in = info.channel_count;
        }
        if (h->ext_audio_ports->count(h->plugin, /*is_input*/ false) > 0) {
            clap_audio_port_info_t info{};
            if (h->ext_audio_ports->get(h->plugin, 0, false, &info)) h->audio_out = info.channel_count;
        }
    }

    if (h->ext_params) {
        uint32_t n = h->ext_params->count(h->plugin);
        h->params.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t pi{};
            if (!h->ext_params->get_info(h->plugin, i, &pi)) continue;
            h->params.push_back({ pi.id, pi.min_value, pi.max_value, pi.default_value, pi.name, pi.module, pi.flags });
        }
    }

    h->max_block = max_frames;
    h->silence.assign(static_cast<size_t>(max_frames) * 2, 0.f);
    if (!h->plugin->activate(h->plugin, sample_rate, 1, max_frames)) { delete h; return nullptr; }
    h->activated = true;
    h->processing = h->plugin->start_processing(h->plugin);
    return h;
#else
    (void)path; (void)sample_rate; (void)max_frames; return nullptr;
#endif
}

// Drain queued UI param changes into the block's event list (call before building notes).
inline void clap_flush_params(ClapHandle* h) {
    ClapParamMsg m;
    while (h->param_q.pop(m)) h->events.add_param(m.id, m.value);
}

// Process one block. `in` may be null (instruments). `out` holds `out_ch` channel pointers.
// The caller has already filled h->events (params + notes). RT-safe.
inline clap_process_status clap_run(ClapHandle* h, int64_t steady, uint32_t frames,
                                    float** in, uint32_t in_ch, float** out, uint32_t out_ch) {
    clap_audio_buffer_t ib{}; ib.data32 = in;  ib.channel_count = in_ch;
    clap_audio_buffer_t ob{}; ob.data32 = out; ob.channel_count = out_ch;
    clap_process_t p{};
    p.steady_time = steady; p.frames_count = frames; p.transport = nullptr;
    p.audio_inputs = in ? &ib : nullptr; p.audio_inputs_count = in ? 1u : 0u;
    p.audio_outputs = &ob; p.audio_outputs_count = 1;
    p.in_events = &h->events.in; p.out_events = &h->events.out;
    return h->plugin->process(h->plugin, &p);
}

// Read a param's current plain value (main thread).
inline double clap_param_value(ClapHandle* h, clap_id id) {
    double v = 0.0;
    if (h->ext_params) h->ext_params->get_value(h->plugin, id, &v);
    return v;
}

// --- State (save/load) as "b:<base64>" via the plugin's clap.state extension (base64.h is
// shared across hosts). Empty string on no-state / failure. Main-thread only. ---
inline std::string clap_save_state(ClapHandle* h) {
    if (!h->ext_state) return "";
    std::string buf;
    clap_ostream_t os{};
    os.ctx = &buf;
    os.write = [](const clap_ostream_t* s, const void* d, uint64_t n) -> int64_t {
        static_cast<std::string*>(s->ctx)->append(static_cast<const char*>(d), static_cast<size_t>(n));
        return static_cast<int64_t>(n);
    };
    if (!h->ext_state->save(h->plugin, &os)) return "";
    return "b:" + vivid::plugin_common::base64_encode(
        reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
}

struct ClapReadStream { const uint8_t* p; size_t n, off; };
inline bool clap_load_state(ClapHandle* h, const std::string& b64) {
    if (!h->ext_state || b64.empty()) return false;
    const std::string body = (b64.rfind("b:", 0) == 0) ? b64.substr(2) : b64;
    std::vector<uint8_t> bytes = vivid::plugin_common::base64_decode(body);
    ClapReadStream rs{ bytes.data(), bytes.size(), 0 };
    clap_istream_t is{};
    is.ctx = &rs;
    is.read = [](const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t {
        auto* r = static_cast<ClapReadStream*>(s->ctx);
        uint64_t avail = r->n - r->off;
        uint64_t take = size < avail ? size : avail;
        std::memcpy(buffer, r->p + r->off, static_cast<size_t>(take));
        r->off += static_cast<size_t>(take);
        return static_cast<int64_t>(take);
    };
    return h->ext_state->load(h->plugin, &is);
}

// --- Preset browse (clap.preset-discovery factory) + load (clap.preset-load ext) ---
// A single discovered preset. `location_kind` + `location` + `load_key` are what preset-load
// wants; `id` is a flat token the host round-trips through MCP (a file path, or "clap:<key>").
struct ClapPresetInfo { std::string name, id; };

// Load a preset by the flat id produced by clap_list_presets. Main-thread only.
inline bool clap_load_preset(ClapHandle* h, const std::string& id) {
    if (!h || !h->ext_preset_load || id.empty()) return false;
    if (id.rfind("clap:", 0) == 0)   // plugin-internal preset: location null, load_key follows
        return h->ext_preset_load->from_location(h->plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                                 nullptr, id.c_str() + 5);
    return h->ext_preset_load->from_location(h->plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                             id.c_str(), nullptr);   // a file path
}

// Collector threaded through the discovery indexer + metadata receiver callbacks.
struct ClapDiscovery {
    std::vector<std::string> exts;                              // declared file extensions (no dot)
    std::vector<std::pair<uint32_t, std::string>> locations;   // (kind, path)
    uint32_t cur_kind = 0; std::string cur_loc;                // set before each get_metadata
    std::vector<ClapPresetInfo>* out = nullptr;
    size_t cap = 4000;                                         // safety cap on the list size
};
inline bool CLAP_ABI clap_idx_declare_filetype(const clap_preset_discovery_indexer_t* ix,
                                               const clap_preset_discovery_filetype_t* ft) {
    auto* d = static_cast<ClapDiscovery*>(ix->indexer_data);
    if (ft && ft->file_extension && *ft->file_extension) {
        std::string e = ft->file_extension; if (!e.empty() && e[0] == '.') e.erase(0, 1);
        d->exts.push_back(e);
    }
    return true;
}
inline bool CLAP_ABI clap_idx_declare_location(const clap_preset_discovery_indexer_t* ix,
                                               const clap_preset_discovery_location_t* loc) {
    auto* d = static_cast<ClapDiscovery*>(ix->indexer_data);
    if (loc) d->locations.push_back({ loc->kind, loc->location ? loc->location : "" });
    return true;
}
inline bool CLAP_ABI clap_idx_declare_soundpack(const clap_preset_discovery_indexer_t*,
                                                const clap_preset_discovery_soundpack_t*) { return true; }
inline const void* CLAP_ABI clap_idx_get_extension(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }
inline bool CLAP_ABI clap_rx_begin_preset(const clap_preset_discovery_metadata_receiver_t* rx,
                                          const char* name, const char* load_key) {
    auto* d = static_cast<ClapDiscovery*>(rx->receiver_data);
    if (d->out->size() >= d->cap) return false;
    ClapPresetInfo pi;
    pi.name = name && *name ? name : (load_key ? load_key : "");
    if (d->cur_kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
        pi.id = std::string("clap:") + (load_key ? load_key : "");
    else
        pi.id = d->cur_loc;   // a file path; load_key on file presets is usually null
    if (!pi.name.empty() && !pi.id.empty()) d->out->push_back(std::move(pi));
    return true;
}

// Case-insensitive substring test (for the browse filter).
inline bool clap_ci_contains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    return lower(hay).find(lower(needle)) != std::string::npos;
}

// Enumerate a loaded CLAP plugin's presets via its DSO's preset-discovery factory. Main-thread
// (filesystem crawl); safe to call off the audio path. Appends to `out`. For FILE presets the
// filename stem IS the name (fast: no per-file get_metadata read — Surge alone ships ~5k patches);
// `filter` narrows by a case-insensitive substring so the list stays usable + quick.
inline void clap_list_presets(ClapHandle* h, std::vector<ClapPresetInfo>& out, const std::string& filter = "") {
    if (!h || !h->entry) return;
    auto* factory = static_cast<const clap_preset_discovery_factory_t*>(
        h->entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (!factory)
        factory = static_cast<const clap_preset_discovery_factory_t*>(
            h->entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));
    if (!factory) return;

    ClapDiscovery d; d.out = &out;
    clap_preset_discovery_indexer_t indexer{};
    indexer.clap_version = CLAP_VERSION;
    indexer.name = "Vivid"; indexer.vendor = "Vivid"; indexer.url = "https://vivid.app"; indexer.version = "1.0";
    indexer.indexer_data = &d;
    indexer.declare_filetype  = &clap_idx_declare_filetype;
    indexer.declare_location  = &clap_idx_declare_location;
    indexer.declare_soundpack = &clap_idx_declare_soundpack;
    indexer.get_extension     = &clap_idx_get_extension;

    clap_preset_discovery_metadata_receiver_t rx{};
    rx.receiver_data = &d;
    rx.begin_preset = &clap_rx_begin_preset;
    // The remaining receiver callbacks are metadata we ignore — the provider may call any of them.
    rx.on_error         = [](const clap_preset_discovery_metadata_receiver_t*, int32_t, const char*) {};
    rx.add_plugin_id    = [](const clap_preset_discovery_metadata_receiver_t*, const clap_universal_plugin_id_t*) {};
    rx.set_soundpack_id = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
    rx.set_flags        = [](const clap_preset_discovery_metadata_receiver_t*, uint32_t) {};
    rx.add_creator      = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
    rx.set_description   = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
    rx.set_timestamps   = [](const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {};
    rx.add_feature      = [](const clap_preset_discovery_metadata_receiver_t*, const char*) {};
    rx.add_extra_info   = [](const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {};

    namespace fs = std::filesystem;
    const uint32_t nprov = factory->count(factory);
    for (uint32_t p = 0; p < nprov && out.size() < d.cap; ++p) {
        const auto* pd = factory->get_descriptor(factory, p);
        if (!pd || !pd->id) continue;
        const auto* prov = factory->create(factory, &indexer, pd->id);
        if (!prov) continue;
        d.exts.clear(); d.locations.clear();
        if (prov->init(prov)) {
            for (const auto& [kind, path] : d.locations) {
                if (out.size() >= d.cap) break;
                if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
                    d.cur_kind = kind; d.cur_loc.clear();
                    prov->get_metadata(prov, kind, nullptr, &rx);
                    continue;
                }
                std::error_code ec;
                // FILE presets: the filename stem is the name; no per-file get_metadata (5k reads).
                auto push_file = [&](const fs::path& fp) {
                    std::string ext = fp.extension().string();
                    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
                    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    bool match_ext = d.exts.empty();
                    for (const auto& e : d.exts) { if (clap_ci_contains(ext, e) && ext.size() == e.size()) { match_ext = true; break; } }
                    if (!match_ext) return;
                    const std::string name = fp.stem().string();
                    if (!clap_ci_contains(name, filter)) return;
                    out.push_back({ name, fp.string() });
                };
                if (fs::is_directory(path, ec)) {
                    for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
                         it != end && out.size() < d.cap; it.increment(ec)) {
                        if (ec) break;
                        if (it->is_regular_file(ec)) push_file(it->path());
                    }
                } else if (fs::is_regular_file(path, ec)) {
                    push_file(path);
                }
            }
        }
        prov->destroy(prov);
    }
}

}  // namespace vivid::session
