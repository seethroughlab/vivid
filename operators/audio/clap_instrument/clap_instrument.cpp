#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include <clap/clap.h>
#include <clap/ext/state.h>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <cmath>
#include <sys/stat.h>

#ifdef __APPLE__
#include "clap_plugin_window.h"
#endif

// ---------------------------------------------------------------------------
// CLAPInstrument — hosts a CLAP instrument plugin as a Vivid audio operator.
//
// Parameters visible in the inspector:
//   plugin_path  — path to the .clap bundle
//   plugin_id    — CLAP plugin id within the bundle (blank = first plugin)
//   macro_0..7   — float 0-1, each mapped to a CLAP param by name via macro_0_id..7_id
//
// Plugin GUI is opened via Cmd+E / Open Editor button (Phase 3).
// Plugin state is not yet persisted across graph saves (Phase 2 follow-up).
//
// Threading model (per CLAP spec):
//   main thread : load, init, activate, deactivate, destroy
//   audio thread: start_processing, process, stop_processing
//
// The operator uses three atomic plugin slots:
//   pending_ — loaded+activated by main thread, awaiting audio thread swap
//   active_  — currently being processed by the audio thread
//   dying_   — stopped by audio thread, awaiting main-thread deactivate+destroy
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// Event list helpers
// ---------------------------------------------------------------------------

static constexpr int kMaxEvents   = 128;
static constexpr int kEventSlotSz = 64; // fits all CLAP event types

struct alignas(8) EventSlot { char data[kEventSlotSz]; };

struct EventList {
    EventSlot slots[kMaxEvents];
    int       count = 0;

    void clear() { count = 0; }

    // Append a CLAP event of any type (must fit in kEventSlotSz bytes).
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
static bool out_try_push(const clap_output_events_t*, const clap_event_header_t*) {
    return true;
}
static const clap_output_events_t kNullOutput = { nullptr, out_try_push };

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
    void*                      library  = nullptr;
    const clap_plugin_entry_t* entry    = nullptr;
    const clap_plugin_t*       plugin   = nullptr;
    bool                       started  = false; // start_processing called
    std::string                path;             // bundle path that was loaded

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

} // namespace

// ---------------------------------------------------------------------------
// Operator struct
// ---------------------------------------------------------------------------

struct CLAPInstrument : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName        = "CLAPInstrument";
    static constexpr bool        kTimeDependent = true;

    // --- Params ---
    vivid::Param<vivid::FilePath>  plugin_path {"plugin_path"};
    vivid::Param<vivid::TextValue> plugin_id   {"plugin_id"};

    vivid::Param<float>            macro_0 {"macro_0", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_0_id {"macro_0_id"};
    vivid::Param<float>            macro_1 {"macro_1", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_1_id {"macro_1_id"};
    vivid::Param<float>            macro_2 {"macro_2", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_2_id {"macro_2_id"};
    vivid::Param<float>            macro_3 {"macro_3", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_3_id {"macro_3_id"};
    vivid::Param<float>            macro_4 {"macro_4", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_4_id {"macro_4_id"};
    vivid::Param<float>            macro_5 {"macro_5", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_5_id {"macro_5_id"};
    vivid::Param<float>            macro_6 {"macro_6", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_6_id {"macro_6_id"};
    vivid::Param<float>            macro_7 {"macro_7", 0.f, 0.f, 1.f};
    vivid::Param<vivid::TextValue> macro_7_id {"macro_7_id"};

    // Opaque plugin state, base64-encoded, persisted in graph JSON.
    // Not shown in the inspector; updated automatically by save_state().
    vivid::Param<vivid::TextValue> plugin_state {"plugin_state"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&plugin_path);
        out.push_back(&plugin_id);
        out.push_back(&macro_0); out.push_back(&macro_0_id);
        out.push_back(&macro_1); out.push_back(&macro_1_id);
        out.push_back(&macro_2); out.push_back(&macro_2_id);
        out.push_back(&macro_3); out.push_back(&macro_3_id);
        out.push_back(&macro_4); out.push_back(&macro_4_id);
        out.push_back(&macro_5); out.push_back(&macro_5_id);
        out.push_back(&macro_6); out.push_back(&macro_6_id);
        out.push_back(&macro_7); out.push_back(&macro_7_id);
        out.push_back(&plugin_state);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
    }

    // --- Plugin slot atomics ---
    std::atomic<PluginHandle*> active_  {nullptr};  // audio thread owns
    std::atomic<PluginHandle*> pending_ {nullptr};  // set by main, swapped by audio
    std::atomic<PluginHandle*> dying_   {nullptr};  // set by audio, cleaned by main

    // --- Plugin GUI window (main thread only) ---
#ifdef __APPLE__
    ClapPluginWindow* gui_win_ = nullptr;
#endif

    // --- State ---
    std::string last_path_;
    std::string last_plugin_id_;
    uint64_t    steady_sample_ = 0;
    uint32_t    sample_rate_   = 48000;
    bool        state_dirty_   = false;  // set by host mark_dirty; triggers save on main thread

    // --- Per-macro resolved CLAP param ID & range cache ---
    struct MacroEntry {
        clap_id id        = CLAP_INVALID_ID;
        double  min_val   = 0.0;
        double  max_val   = 1.0;
        float   last_sent = -1.f;  // detect changes to avoid spamming events
    };
    MacroEntry macro_map_[8];

    // Macro param accessors (indexed 0-7)
    vivid::Param<float>*            macro_float_[8];
    vivid::Param<vivid::TextValue>* macro_id_[8];

    // --- Per-frame event list (audio thread only) ---
    EventList in_events_;

    // --- CLAP host descriptor (per-instance, stable address) ---
    clap_host_t host_desc_;

    // -----------------------------------------------------------------------
    CLAPInstrument() {
        macro_float_[0] = &macro_0; macro_id_[0] = &macro_0_id;
        macro_float_[1] = &macro_1; macro_id_[1] = &macro_1_id;
        macro_float_[2] = &macro_2; macro_id_[2] = &macro_2_id;
        macro_float_[3] = &macro_3; macro_id_[3] = &macro_3_id;
        macro_float_[4] = &macro_4; macro_id_[4] = &macro_4_id;
        macro_float_[5] = &macro_5; macro_id_[5] = &macro_5_id;
        macro_float_[6] = &macro_6; macro_id_[6] = &macro_6_id;
        macro_float_[7] = &macro_7; macro_id_[7] = &macro_7_id;

        std::memset(&host_desc_, 0, sizeof(host_desc_));
        host_desc_.clap_version = CLAP_VERSION;
        host_desc_.host_data    = this;
        host_desc_.name         = "Vivid";
        host_desc_.vendor       = "See Through Lab";
        host_desc_.url          = "";
        host_desc_.version      = "1.0.0";
        host_desc_.get_extension   = &host_get_extension;
        host_desc_.request_restart = &host_request_restart;
        host_desc_.request_process = &host_request_process;
        host_desc_.request_callback = &host_request_callback;

        vivid::description(plugin_path, "Path to .clap plugin bundle");
        vivid::description(plugin_id,   "Plugin ID within bundle (blank = first plugin)");
        vivid::description(macro_0, "Macro 0 value (0-1), mapped to the CLAP param named in macro_0_id");
        vivid::description(macro_0_id, "CLAP parameter name for macro 0");
        vivid::description(macro_1, "Macro 1 value (0-1)");
        vivid::description(macro_1_id, "CLAP parameter name for macro 1");
        vivid::description(macro_2, "Macro 2 value (0-1)");
        vivid::description(macro_2_id, "CLAP parameter name for macro 2");
        vivid::description(macro_3, "Macro 3 value (0-1)");
        vivid::description(macro_3_id, "CLAP parameter name for macro 3");
        vivid::description(macro_4, "Macro 4 value (0-1)");
        vivid::description(macro_4_id, "CLAP parameter name for macro 4");
        vivid::description(macro_5, "Macro 5 value (0-1)");
        vivid::description(macro_5_id, "CLAP parameter name for macro 5");
        vivid::description(macro_6, "Macro 6 value (0-1)");
        vivid::description(macro_6_id, "CLAP parameter name for macro 6");
        vivid::description(macro_7, "Macro 7 value (0-1)");
        vivid::description(macro_7_id, "CLAP parameter name for macro 7");
        vivid::display_hint(plugin_state, VIVID_DISPLAY_HIDDEN);
    }

    ~CLAPInstrument() {
        // Close GUI before destroying plugins (CLAP spec: destroy GUI before deactivating)
#ifdef __APPLE__
        if (gui_win_) { clap_plugin_window_close(gui_win_); gui_win_ = nullptr; }
#endif
        // Best-effort cleanup. Audio thread should be stopped before operator destroy.
        auto* act = active_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_plugin(act, /*was_processing=*/act && act->started);
        auto* pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_plugin(pend, false);
        auto* dead = dying_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_plugin(dead, false);
    }

    // -----------------------------------------------------------------------
    // Main-thread: load a new plugin from disk
    // -----------------------------------------------------------------------

    void prepare_instance_assets() override {
        reload_if_changed();
    }

    void main_thread_update(double /*time*/) override {
        // Clean up any plugin stopped by the audio thread
        auto* dead = dying_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_plugin(dead, /*was_processing=*/false);

        // Reload if path changed
        reload_if_changed();

        // Refresh macro ID → CLAP param mappings when a new plugin is active
        update_macro_map();

        // Persist plugin state when dirty (plugin called mark_dirty, or first
        // save after the plugin becomes active so new graphs capture initial state)
        if (state_dirty_) save_state();

        // Check if the CLAP GUI window was closed by the user
#ifdef __APPLE__
        if (gui_win_ && !clap_plugin_window_is_open(gui_win_)) {
            clap_plugin_window_close(gui_win_);
            gui_win_ = nullptr;
        }
#endif
    }

    void reload_if_changed() {
        if (plugin_path.str_value == last_path_ && plugin_id.str_value == last_plugin_id_)
            return;
        last_path_      = plugin_path.str_value;
        last_plugin_id_ = plugin_id.str_value;

        // Close GUI when plugin changes
#ifdef __APPLE__
        if (gui_win_) { clap_plugin_window_close(gui_win_); gui_win_ = nullptr; }
#endif

        // Clear macro map — will be resolved after new plugin loads
        for (auto& m : macro_map_) { m.id = CLAP_INVALID_ID; m.last_sent = -1.f; }

        if (last_path_.empty()) {
            // Discard any pending plugin; active will be swapped out by audio thread
            auto* old_pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
            destroy_plugin(old_pend, false);
            // Signal audio thread to drop active plugin too
            pending_.store(nullptr, std::memory_order_release);
            // active_ will be stopped+moved to dying_ at next audio buffer
            // We use a sentinel: store a zero-initialized handle so audio thread
            // knows to stop the active plugin and install nothing.
            auto* sentinel = new PluginHandle();  // empty, no library loaded
            pending_.store(sentinel, std::memory_order_release);
            return;
        }

        PluginHandle* h = load_plugin(last_path_.c_str(), last_plugin_id_.c_str());
        if (!h) return;

        // Discard previously pending plugin (never reached the audio thread)
        auto* old_pend = pending_.exchange(h, std::memory_order_acq_rel);
        destroy_plugin(old_pend, false);

        // Trigger an initial state save once the plugin becomes active, so new
        // graphs capture the plugin's default state on the first graph save.
        state_dirty_ = true;
    }

    // On macOS .clap plugins are bundle directories; find the binary inside.
    // On Linux they are flat .so files; return path unchanged.
    static std::string resolve_clap_binary(const char* path) {
        struct stat st;
        if (stat(path, &st) != 0) return path;
        if (!S_ISDIR(st.st_mode)) return path;  // flat dylib (Linux)

        // Bundle: Contents/MacOS/<stem>  where stem = basename without ".clap"
        std::string p(path);
        // Strip trailing slash if any
        while (!p.empty() && p.back() == '/') p.pop_back();

        // Extract stem: filename without ".clap"
        size_t slash = p.rfind('/');
        std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
        std::string stem = name;
        const std::string ext = ".clap";
        if (stem.size() > ext.size() &&
            stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0)
            stem.resize(stem.size() - ext.size());

        return p + "/Contents/MacOS/" + stem;
    }

    // Load, init, and activate a CLAP plugin. Returns null on failure.
    PluginHandle* load_plugin(const char* path, const char* id_hint) {
        std::string binary = resolve_clap_binary(path);

        void* lib = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) { fprintf(stderr, "[CLAPInstrument] dlopen failed: %s\n", dlerror()); return nullptr; }

        auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
        if (!entry) { fprintf(stderr, "[CLAPInstrument] no clap_entry symbol\n"); dlclose(lib); return nullptr; }
        if (!clap_version_is_compatible(entry->clap_version)) {
            fprintf(stderr, "[CLAPInstrument] incompatible CLAP version %u.%u.%u\n",
                    entry->clap_version.major, entry->clap_version.minor, entry->clap_version.revision);
            dlclose(lib); return nullptr;
        }

        if (!entry->init(binary.c_str())) { fprintf(stderr, "[CLAPInstrument] entry->init failed\n"); dlclose(lib); return nullptr; }

        auto* factory = reinterpret_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (!factory) { fprintf(stderr, "[CLAPInstrument] no plugin factory\n"); entry->deinit(); dlclose(lib); return nullptr; }

        uint32_t count = factory->get_plugin_count(factory);
        const clap_plugin_descriptor_t* desc = nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            auto* d = factory->get_plugin_descriptor(factory, i);
            if (!d) continue;
            if (!id_hint || id_hint[0] == '\0' || std::strcmp(d->id, id_hint) == 0) {
                desc = d;
                break;
            }
        }
        if (!desc) { fprintf(stderr, "[CLAPInstrument] no matching plugin descriptor\n"); entry->deinit(); dlclose(lib); return nullptr; }

        const clap_plugin_t* plugin = factory->create_plugin(factory, &host_desc_, desc->id);
        if (!plugin) { fprintf(stderr, "[CLAPInstrument] create_plugin failed\n"); entry->deinit(); dlclose(lib); return nullptr; }

        if (!plugin->init(plugin)) {
            fprintf(stderr, "[CLAPInstrument] plugin->init failed\n");
            plugin->destroy(plugin); entry->deinit(); dlclose(lib); return nullptr;
        }

        // activate — main thread, before first process call
        if (!plugin->activate(plugin, sample_rate_, 32, 4096)) {
            fprintf(stderr, "[CLAPInstrument] plugin->activate failed (sample_rate=%u)\n", sample_rate_);
            plugin->destroy(plugin); entry->deinit(); dlclose(lib); return nullptr;
        }

        auto* h   = new PluginHandle();
        h->library = lib;
        h->entry   = entry;
        h->plugin  = plugin;
        h->path    = path;

        // Cache param list for macro resolution
        cache_params(h);

        // Restore saved state if we have one (e.g. reloading a saved graph)
        load_state(h);

        return h;
    }

    void cache_params(PluginHandle* h) {
        auto* params_ext = reinterpret_cast<const clap_plugin_params_t*>(
            h->plugin->get_extension(h->plugin, CLAP_EXT_PARAMS));
        if (!params_ext) return;
        uint32_t n = params_ext->count(h->plugin);
        h->params.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t info{};
            params_ext->get_info(h->plugin, i, &info);
            h->params[i] = { info.id, info.min_value, info.max_value, {} };
            std::strncpy(h->params[i].name, info.name, CLAP_NAME_SIZE - 1);
        }
    }

    // Resolve macro name strings → CLAP param IDs from the active plugin's param list
    void update_macro_map() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || act->params.empty()) return;

        for (int i = 0; i < 8; ++i) {
            const std::string& name = macro_id_[i]->str_value;
            if (name.empty()) { macro_map_[i].id = CLAP_INVALID_ID; continue; }
            if (macro_map_[i].id != CLAP_INVALID_ID) continue;  // already resolved

            for (auto& p : act->params) {
                if (std::strncmp(p.name, name.c_str(), CLAP_NAME_SIZE) == 0) {
                    macro_map_[i] = { p.id, p.min_val, p.max_val, -1.f };
                    break;
                }
            }
        }
    }

    // Safely deactivate and destroy a plugin handle (main thread only).
    void destroy_plugin(PluginHandle* h, bool was_processing) {
        if (!h) return;
        if (h->plugin) {
            if (was_processing) h->plugin->stop_processing(h->plugin);
            h->plugin->deactivate(h->plugin);
        }
        delete h;
    }

    // -----------------------------------------------------------------------
    // Audio thread
    // -----------------------------------------------------------------------

    void process_audio(const VividAudioContext* ctx) override {
        sample_rate_ = ctx->sample_rate;

        // Check for a pending plugin swap
        PluginHandle* pend = pending_.load(std::memory_order_acquire);
        if (pend) {
            pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
            if (pend) {
                PluginHandle* old = active_.exchange(pend, std::memory_order_acq_rel);
                if (old) {
                    if (old->started) {
                        old->plugin->stop_processing(old->plugin);
                        old->started = false;
                    }
                    dying_.store(old, std::memory_order_release);
                }
                // Empty sentinel means "unload, install nothing"
                if (!pend->plugin) {
                    active_.store(nullptr, std::memory_order_release);
                    // dying_ already holds `old` (if any) — do not overwrite it
                    delete pend;
                    zero_outputs(ctx);
                    return;
                }
                pend->plugin->start_processing(pend->plugin);
                pend->started = true;
            }
        }

        PluginHandle* act = active_.load(std::memory_order_acquire);
        if (!act || !act->started) { zero_outputs(ctx); return; }

        // Build CLAP input event list
        in_events_.clear();
        build_note_events(ctx);
        build_macro_events(ctx);
        auto clap_in = in_events_.as_clap_input();

        // Wire Vivid's output buffer directly as CLAP stereo output
        float* out_l = ctx->output_buffers[0];
        float* out_r = ctx->output_buffers[0] + ctx->buffer_size;
        float* channels[2] = { out_l, out_r };
        clap_audio_buffer_t clap_out{};
        clap_out.data32        = channels;
        clap_out.channel_count = 2;

        clap_process_t proc{};
        proc.steady_time         = static_cast<int64_t>(steady_sample_);
        proc.frames_count        = ctx->buffer_size;
        proc.transport           = nullptr;
        proc.audio_inputs        = nullptr;
        proc.audio_inputs_count  = 0;
        proc.audio_outputs       = &clap_out;
        proc.audio_outputs_count = 1;
        proc.in_events           = &clap_in;
        proc.out_events          = &kNullOutput;

        act->plugin->process(act->plugin, &proc);

        steady_sample_ += ctx->buffer_size;
    }

    void build_note_events(const VividAudioContext* ctx) {
        if (!ctx->custom_inputs || ctx->custom_input_count == 0 || !ctx->custom_inputs[0])
            return;
        auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        for (uint32_t i = 0; i < notes->count; ++i) {
            const auto& ev = notes->events[i];
            uint32_t t = static_cast<uint32_t>(
                std::min(static_cast<uint64_t>(ev.frame_offset_samples),
                         static_cast<uint64_t>(ctx->buffer_size - 1)));

            if (ev.type == VIVID_NOTE_ON) {
                clap_event_note_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 };
                e.note_id    = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index = 0;
                e.channel    = 0;
                e.key        = ev.note_number;
                e.velocity   = ev.value;
                in_events_.push(e);

            } else if (ev.type == VIVID_NOTE_OFF) {
                clap_event_note_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_OFF, 0 };
                e.note_id    = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index = 0;
                e.channel    = 0;
                // note_number==255 is the sentinel for "unknown"; use -1 wildcard in that case
                e.key        = (ev.note_number != 255) ? static_cast<int16_t>(ev.note_number) : -1;
                e.velocity   = ev.value;
                in_events_.push(e);

            } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                clap_event_note_expression_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0 };
                e.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
                e.note_id       = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index    = 0;
                e.channel       = 0;
                e.key           = ev.note_number;
                e.value         = ev.value; // semitones
                in_events_.push(e);

            } else if (ev.type == VIVID_NOTE_PRESSURE) {
                clap_event_note_expression_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0 };
                e.expression_id = CLAP_NOTE_EXPRESSION_PRESSURE;
                e.note_id       = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index    = 0;
                e.channel       = 0;
                e.key           = ev.note_number;
                e.value         = ev.value;
                in_events_.push(e);

            } else if (ev.type == VIVID_NOTE_TIMBRE) {
                clap_event_note_expression_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0 };
                e.expression_id = CLAP_NOTE_EXPRESSION_BRIGHTNESS;
                e.note_id       = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index    = 0;
                e.channel       = 0;
                e.key           = ev.note_number;
                e.value         = ev.value;
                in_events_.push(e);
            }
        }
    }

    void build_macro_events(const VividAudioContext* ctx) {
        // param values are synced into macro_N.value before process_audio is called
        const float vals[8] = {
            macro_0.value, macro_1.value, macro_2.value, macro_3.value,
            macro_4.value, macro_5.value, macro_6.value, macro_7.value,
        };
        for (int i = 0; i < 8; ++i) {
            if (macro_map_[i].id == CLAP_INVALID_ID) continue;
            if (std::fabs(vals[i] - macro_map_[i].last_sent) < 1e-6f) continue;
            macro_map_[i].last_sent = vals[i];

            double scaled = macro_map_[i].min_val +
                static_cast<double>(vals[i]) * (macro_map_[i].max_val - macro_map_[i].min_val);

            clap_event_param_value_t e{};
            e.header    = { sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
            e.param_id  = macro_map_[i].id;
            e.cookie    = nullptr;
            e.note_id   = -1;
            e.port_index = -1;
            e.channel   = -1;
            e.key       = -1;
            e.value     = scaled;
            in_events_.push(e);
        }
        (void)ctx;
    }

    static void zero_outputs(const VividAudioContext* ctx) {
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);
    }

    // -----------------------------------------------------------------------
    // State save / load  (main thread only)
    // -----------------------------------------------------------------------

    void save_state() {
        auto* act = active_.load(std::memory_order_acquire);
        // Guard: only save once the audio thread has swapped in the right plugin.
        // last_path_ is the intended path; act->path is what's actually running.
        if (!act || !act->plugin || act->path != last_path_) return;
        auto* st = reinterpret_cast<const clap_plugin_state_t*>(
            act->plugin->get_extension(act->plugin, CLAP_EXT_STATE));
        if (!st) return;
        VividOStream ostr;
        if (!st->save(act->plugin, &ostr.clap)) return;
        plugin_state.str_value = b64_encode(ostr.buf.data(), ostr.buf.size());
        state_dirty_ = false;
    }

    void load_state(PluginHandle* h) {
        if (plugin_state.str_value.empty() || !h || !h->plugin) return;
        auto* st = reinterpret_cast<const clap_plugin_state_t*>(
            h->plugin->get_extension(h->plugin, CLAP_EXT_STATE));
        if (!st) return;
        std::vector<uint8_t> buf = b64_decode(plugin_state.str_value);
        if (buf.empty()) return;
        VividIStream istr(buf.data(), buf.size());
        st->load(h->plugin, &istr.clap);
    }

    // -----------------------------------------------------------------------
    // CLAP host callbacks (static, use host_data to reach the instance)
    // -----------------------------------------------------------------------

    // Host state extension — returned by host_get_extension for CLAP_EXT_STATE.
    static const clap_host_state_t* host_state_ext() {
        static const clap_host_state_t kExt = {
            [](const clap_host_t* host) {
                static_cast<CLAPInstrument*>(host->host_data)->state_dirty_ = true;
            }
        };
        return &kExt;
    }

    static const void* host_get_extension(const clap_host_t*, const char* id) {
        if (std::strcmp(id, CLAP_EXT_STATE) == 0) return host_state_ext();
        return nullptr;
    }
    static void host_request_restart(const clap_host_t*)  {}
    static void host_request_process(const clap_host_t*)  {}
    static void host_request_callback(const clap_host_t*) {}
};

VIVID_DEFINE_OP(CLAPInstrument) {
    display_name = "CLAP Instrument";
    keywords     = {"plugin", "vst", "synth", "instrument", "external"};
    summary      = "Hosts a CLAP instrument plugin; receives MIDI notes, outputs audio.";
}

// ---------------------------------------------------------------------------
// VIVID_EDITOR surface — opens the CLAP plugin's native GUI in its own
// Cocoa NSWindow. The Vivid editor window closes immediately (request_close=1);
// the CLAP GUI window lives independently until the user closes it.
// ---------------------------------------------------------------------------

extern "C" VividEditorMetadata vivid_editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 1;
    m.default_height = 1;
    m.min_width      = 1;
    m.min_height     = 1;
    m.title_suffix   = nullptr;
    return m;
}

extern "C" void vivid_draw_editor(void* instance, VividEditorContext* ctx) {
#ifdef __APPLE__
    auto* self = static_cast<CLAPInstrument*>(instance);

    // If window already open, close Vivid's editor immediately and return.
    if (self->gui_win_) {
        ctx->request_close = 1;
        return;
    }

    auto* act = self->active_.load(std::memory_order_acquire);
    if (!act || !act->plugin) { ctx->request_close = 1; return; }

    auto* gui_ext = reinterpret_cast<const clap_plugin_gui_t*>(
        act->plugin->get_extension(act->plugin, CLAP_EXT_GUI));
    if (!gui_ext) { ctx->request_close = 1; return; }

    // Build a window title from the plugin path stem.
    const char* title_str = "CLAP Plugin";
    const std::string& path = self->last_path_;
    std::string stem;
    if (!path.empty()) {
        size_t slash = path.rfind('/');
        stem = (slash == std::string::npos) ? path : path.substr(slash + 1);
        // Strip .clap extension
        const std::string ext = ".clap";
        if (stem.size() > ext.size() &&
            stem.compare(stem.size() - ext.size(), ext.size(), ext) == 0)
            stem.resize(stem.size() - ext.size());
        if (!stem.empty()) title_str = stem.c_str();
    }

    self->gui_win_ = clap_plugin_window_open(act->plugin, gui_ext, title_str);
    ctx->request_close = 1;
#else
    (void)instance;
    ctx->request_close = 1;
#endif
}
