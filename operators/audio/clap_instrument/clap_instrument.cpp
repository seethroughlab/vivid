#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "shared/plugin_common/direct_param_queue.h"
#include "operator_api/type_id.h"
#include "shared/clap_host/clap_host_common.h"
#include "shared/clap_host/clap_scanner.h"
#include "shared/plugin_ui/plugin_picker.h"
#ifdef __APPLE__
#include "shared/clap_host/clap_plugin_window.h"
#endif

#include <atomic>
#include <cmath>

// ---------------------------------------------------------------------------
// CLAPInstrument — hosts a CLAP instrument plugin as a Vivid audio operator.
//
// Parameters visible in the inspector:
//   plugin_path  — path to the .clap bundle
//   plugin_id    — CLAP plugin id within the bundle (blank = first plugin)
//   macro_0..7   — float 0-1, each mapped to a CLAP param by name via macro_0_id..7_id
//
// Plugin GUI is opened via Cmd+E / Open Editor button.
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

    // JSON array of CLAP parameters from the loaded plugin (auto-populated).
    // Consumed by list_clap_params MCP tool; not shown in the inspector.
    vivid::Param<vivid::TextValue> clap_params_  {"_clap_params"};
    vivid::Param<vivid::TextValue> direct_params_ {"_clap_direct_params"};

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
        out.push_back(&clap_params_);
        out.push_back(&direct_params_);
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
    std::atomic<PluginHandle*> dying2_  {nullptr};  // overflow: if dying_ was already full

    // --- Plugin GUI window and inspector state (main thread only) ---
#ifdef __APPLE__
    ClapPluginWindow* gui_win_ = nullptr;
#endif
    vivid::plugin_ui::PluginPickerState picker_state_;
    vivid::plugin_ui::PluginPickerState add_picker_state_;
    int drag_macro_ = -1;
    vivid::draw_ui::InlineNumericField  text_field_;
    std::vector<std::string> param_names_cache_;

    // --- State ---
    std::string        last_path_;
    std::string        last_plugin_id_;
    uint64_t           steady_sample_         = 0;
    uint32_t           sample_rate_           = 48000;
    uint32_t           loaded_rate_           = 0;
    std::atomic<uint32_t> audio_rate_seen_    {0};
    bool               state_dirty_           = false;
    std::atomic<bool>  callback_requested_    {false};

    // --- Direct param queue (main thread → audio thread, unlimited params) ---
    DirectParamQueue direct_q_;
    std::string      last_direct_params_;

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
        host_desc_.clap_version  = CLAP_VERSION;
        host_desc_.host_data     = this;
        host_desc_.name          = "Vivid";
        host_desc_.vendor        = "See Through Lab";
        host_desc_.url           = "";
        host_desc_.version       = "1.0.0";
        host_desc_.get_extension  = &host_get_extension;
        host_desc_.request_restart  = &host_request_restart;
        host_desc_.request_process  = &host_request_process;
        host_desc_.request_callback = &host_request_callback;

        vivid::description(plugin_path, "Path to .clap plugin bundle");
        vivid::description(plugin_id,   "Plugin ID within bundle (blank = first plugin)");
        vivid::display_hint(plugin_path, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(plugin_id,   VIVID_DISPLAY_HIDDEN);
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
        vivid::display_hint(macro_0, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_1, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_2, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_3, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_4, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_5, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_6, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(macro_7, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(plugin_state,    VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(clap_params_,    VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(direct_params_,  VIVID_DISPLAY_HIDDEN);
    }

    ~CLAPInstrument() {
        // Close GUI before destroying plugins (CLAP spec: destroy GUI before deactivating)
#ifdef __APPLE__
        if (gui_win_) { clap_plugin_window_close(gui_win_); gui_win_ = nullptr; }
#endif
        // Best-effort cleanup. Audio thread should be stopped before operator destroy.
        auto* act = active_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_plugin(act, /*was_processing=*/act && act->started);
        auto* pend  = pending_.exchange(nullptr,  std::memory_order_acq_rel);
        destroy_plugin(pend, false);
        auto* dead  = dying_.exchange(nullptr,   std::memory_order_acq_rel);
        destroy_plugin(dead, false);
        auto* dead2 = dying2_.exchange(nullptr,  std::memory_order_acq_rel);
        destroy_plugin(dead2, false);
    }

    // -----------------------------------------------------------------------
    // Main-thread: load a new plugin from disk
    // -----------------------------------------------------------------------

    void prepare_instance_assets() override {
        reload_if_changed();
        clap_scan_plugins();
    }

    void main_thread_update(double /*time*/) override {
        // Clean up any plugins stopped by the audio thread
        auto* dead  = dying_.exchange(nullptr,  std::memory_order_acq_rel);
        auto* dead2 = dying2_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_plugin(dead,  /*was_processing=*/false);
        destroy_plugin(dead2, /*was_processing=*/false);

        // Dispatch any pending plugin main-thread callback
        if (callback_requested_.exchange(false, std::memory_order_acq_rel)) {
            auto* act = active_.load(std::memory_order_acquire);
            if (act && act->plugin) act->plugin->on_main_thread(act->plugin);
        }

        // If the audio thread reported a sample rate different from what we used to activate
        // the plugin, reload at the correct rate so activate() and process() are consistent.
        {
            uint32_t seen = audio_rate_seen_.load(std::memory_order_acquire);
            if (seen > 0 && seen != loaded_rate_ && !last_path_.empty()) {
                sample_rate_ = seen;
                reload_for_rate_change();
            }
        }

        // Reload if path changed
        reload_if_changed();

        // Refresh macro ID → CLAP param mappings when a new plugin is active
        update_macro_map();

        // Refresh _clap_params JSON when active plugin changes
        refresh_clap_params_json();

        // Deliver any direct param changes queued by MCP
        process_direct_params();

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

        // Clear macro map and direct params — stale IDs must not reach the new plugin
        for (auto& m : macro_map_) { m.id = CLAP_INVALID_ID; m.last_sent = -1.f; }
        direct_params_.str_value.clear();
        last_direct_params_.clear();

        if (last_path_.empty()) {
            auto* old_pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
            destroy_plugin(old_pend, false);
            auto* sentinel = new PluginHandle();  // empty, no library loaded
            pending_.store(sentinel, std::memory_order_release);
            return;
        }

        PluginHandle* h = load_plugin(last_path_.c_str(), last_plugin_id_.c_str());
        if (!h) return;

        loaded_rate_ = sample_rate_;
        auto* old_pend = pending_.exchange(h, std::memory_order_acq_rel);
        destroy_plugin(old_pend, false);

        // Trigger an initial state save once the plugin becomes active, so new
        // graphs capture the plugin's default state on the first graph save.
        state_dirty_ = true;
    }

    // Reload the current plugin at a new sample rate, preserving saved state.
    void reload_for_rate_change() {
        if (last_path_.empty()) return;
        PluginHandle* h = load_plugin(last_path_.c_str(), last_plugin_id_.c_str());
        if (!h) return;
        loaded_rate_ = sample_rate_;
        auto* old_pend = pending_.exchange(h, std::memory_order_acq_rel);
        destroy_plugin(old_pend, false);
        state_dirty_ = true;
    }

    // Load, init, and activate a CLAP plugin. Returns null on failure.
    PluginHandle* load_plugin(const char* path, const char* id_hint) {
        std::string binary = clap_resolve_binary(path);

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

        if (!plugin->activate(plugin, sample_rate_, 32, 4096)) {
            fprintf(stderr, "[CLAPInstrument] plugin->activate failed (sample_rate=%u)\n", sample_rate_);
            plugin->destroy(plugin); entry->deinit(); dlclose(lib); return nullptr;
        }

        auto* h    = new PluginHandle();
        h->library = lib;
        h->entry   = entry;
        h->plugin  = plugin;
        h->path    = path;

        cache_params(h);
        load_state(h);

        return h;
    }

    void cache_params(PluginHandle* h) { clap_cache_params(h); }

    void update_macro_map() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || act->params.empty()) return;

        for (int i = 0; i < 8; ++i) {
            const std::string& name = macro_id_[i]->str_value;
            if (name.empty()) { macro_map_[i].id = CLAP_INVALID_ID; continue; }
            if (macro_map_[i].id != CLAP_INVALID_ID) continue;

            for (auto& p : act->params) {
                if (std::strncmp(p.name, name.c_str(), CLAP_NAME_SIZE) == 0) {
                    macro_map_[i] = { p.id, p.min_val, p.max_val, -1.f };
                    break;
                }
            }
        }
    }

    void process_direct_params() {
        if (direct_params_.str_value == last_direct_params_) return;
        last_direct_params_ = direct_params_.str_value;
        direct_param_queue_parse_and_push(direct_q_, direct_params_.str_value);
    }

    void refresh_clap_params_json() {
        auto* act = active_.load(std::memory_order_acquire);
        const clap_plugin_params_t* params_ext = nullptr;
        if (act && act->plugin)
            params_ext = reinterpret_cast<const clap_plugin_params_t*>(
                act->plugin->get_extension(act->plugin, CLAP_EXT_PARAMS));
        std::string new_json = clap_params_to_json(act, params_ext);
        if (new_json != clap_params_.str_value)
            clap_params_.str_value = std::move(new_json);
    }

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
        audio_rate_seen_.store(ctx->sample_rate, std::memory_order_relaxed);

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
                    // Exchange so we can detect if main_thread_update() was too slow to
                    // drain dying_ before another swap arrived. The evicted plugin moves
                    // to dying2_ for the main thread to pick up next frame.
                    auto* prev = dying_.exchange(old, std::memory_order_acq_rel);
                    if (prev) dying2_.store(prev, std::memory_order_release);
                }
                if (!pend->plugin) {
                    active_.store(nullptr, std::memory_order_release);
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

        in_events_.clear();
        build_note_events(ctx);
        build_macro_events(ctx);
        auto clap_in = in_events_.as_clap_input();

        float* out_l = ctx->output_buffers[0];
        float* out_r = ctx->output_buffers[0] + ctx->buffer_size;
        float* channels[2] = { out_l, out_r };
        clap_audio_buffer_t clap_out{};
        clap_out.data32        = channels;
        clap_out.channel_count = 2;

        clap_event_transport_t transport = clap_build_transport(ctx);

        clap_process_t proc{};
        proc.steady_time         = static_cast<int64_t>(steady_sample_);
        proc.frames_count        = ctx->buffer_size;
        proc.transport           = &transport;
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
                e.key        = (ev.note_number != 255) ? static_cast<int16_t>(ev.note_number) : -1;
                e.velocity   = ev.value;
                in_events_.push(e);
            } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                clap_event_note_expression_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0 };
                e.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
                e.note_id       = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index    = 0;  e.channel = 0;
                e.key           = ev.note_number;
                e.value         = ev.value;
                in_events_.push(e);
            } else if (ev.type == VIVID_NOTE_PRESSURE) {
                clap_event_note_expression_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0 };
                e.expression_id = CLAP_NOTE_EXPRESSION_PRESSURE;
                e.note_id       = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index    = 0;  e.channel = 0;
                e.key           = ev.note_number;
                e.value         = ev.value;
                in_events_.push(e);
            } else if (ev.type == VIVID_NOTE_TIMBRE) {
                clap_event_note_expression_t e{};
                e.header = { sizeof(e), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0 };
                e.expression_id = CLAP_NOTE_EXPRESSION_BRIGHTNESS;
                e.note_id       = static_cast<int32_t>(ev.note_id & 0x7FFFFFFF);
                e.port_index    = 0;  e.channel = 0;
                e.key           = ev.note_number;
                e.value         = ev.value;
                in_events_.push(e);
            }
        }
    }

    void build_macro_events(const VividAudioContext* /*ctx*/) {
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
            e.header     = { sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
            e.param_id   = macro_map_[i].id;
            e.cookie     = nullptr;
            e.note_id    = -1;  e.port_index = -1;  e.channel = -1;  e.key = -1;
            e.value      = scaled;
            in_events_.push(e);
        }

        // Drain direct-param queue (MCP-sourced, normalized → native)
        {
            auto* act = active_.load(std::memory_order_acquire);
            DirectParamQueue::Entry entry;
            while (direct_q_.pop(entry)) {
                double native = entry.val;
                if (act) {
                    for (const auto& p : act->params)
                        if (p.id == entry.id) {
                            native = p.min_val + entry.val * (p.max_val - p.min_val);
                            break;
                        }
                }
                clap_event_param_value_t e{};
                e.header     = { sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id   = entry.id;
                e.cookie     = nullptr;
                e.note_id    = -1;  e.port_index = -1;  e.channel = -1;  e.key = -1;
                e.value      = native;
                in_events_.push(e);
            }
        }
    }

    static void zero_outputs(const VividAudioContext* ctx) {
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);
    }

    // -----------------------------------------------------------------------
    // State save / load  (main thread only)
    // -----------------------------------------------------------------------

    void save_state() {
        auto* act = active_.load(std::memory_order_acquire);
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
    // CLAP host callbacks
    // -----------------------------------------------------------------------

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
    static void host_request_callback(const clap_host_t* h) {
        static_cast<CLAPInstrument*>(h->host_data)->callback_requested_.store(true, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    // Inspector (VIVID_INSPECTOR) — plugin picker + native GUI button.
    // -----------------------------------------------------------------------

    void draw_inspector(VividInspectorContext* ctx) override {
        using namespace vivid::plugin_ui;
        using namespace vivid::draw_ui;

        const auto& plugins = clap_get_plugins();

        // Build param name list from active handle (fallback to pending)
        {
            auto* h = active_.load(std::memory_order_acquire);
            if (!h || h->params.empty()) h = pending_.load(std::memory_order_acquire);
            param_names_cache_.clear();
            if (h) {
                param_names_cache_.reserve(h->params.size());
                for (const auto& p : h->params)
                    param_names_cache_.emplace_back(p.name);
            }
        }

        // Build "Name [Vendor]" display list
        std::vector<std::string> names;
        names.reserve(plugins.size());
        for (const auto& p : plugins) {
            std::string disp = p.name;
            if (!p.vendor.empty()) { disp += " ["; disp += p.vendor; disp += "]"; }
            names.push_back(std::move(disp));
        }

        // Find current selection by matching path + plugin_id
        int cur = -1;
        for (int i = 0; i < (int)plugins.size(); ++i) {
            if (plugins[i].path == last_path_ && plugins[i].plugin_id == last_plugin_id_)
                { cur = i; break; }
        }

        const float x  = ctx->content_x;
        const float w  = ctx->content_width;
        const auto& m  = ctx->mouse;
        auto& d        = ctx->draw;
        void* o        = d.opaque;
        const auto& th = ctx->theme;

        float y = ctx->content_y + 4.f;

        bool was_picker_open = picker_state_.open;
        int sel = draw_plugin_picker(ctx, y, names, cur, picker_state_);
        if (!was_picker_open && picker_state_.open) add_picker_state_.open = false;
        if (sel >= 0) {
            ctx->commands.set_string_param(ctx->commands.opaque,
                                           "plugin_path", plugins[sel].path.c_str());
            ctx->commands.set_string_param(ctx->commands.opaque,
                                           "plugin_id", plugins[sel].plugin_id.c_str());
        }

        y += 4.f;

#ifdef __APPLE__
        // Sync GUI window closed state
        if (gui_win_ && !clap_plugin_window_is_open(gui_win_)) {
            clap_plugin_window_close(gui_win_);
            gui_win_ = nullptr;
        }
        auto* act = active_.load(std::memory_order_acquire);
        bool has_gui = false;
        if (act && act->plugin) {
            auto* gui_ext = reinterpret_cast<const clap_plugin_gui_t*>(
                act->plugin->get_extension(act->plugin, CLAP_EXT_GUI));
            has_gui = gui_ext &&
                gui_ext->is_api_supported(act->plugin, CLAP_WINDOW_API_COCOA, false);
        }
        bool opened = draw_open_gui_button(ctx, y, has_gui, gui_win_ != nullptr);
        if (opened && act && act->plugin) {
            auto* gui_ext = reinterpret_cast<const clap_plugin_gui_t*>(
                act->plugin->get_extension(act->plugin, CLAP_EXT_GUI));
            if (gui_ext) {
                std::string title = "CLAP Plugin";
                for (const auto& p : clap_get_plugins()) {
                    if (p.path == last_path_ && p.plugin_id == last_plugin_id_) {
                        title = p.name; break;
                    }
                }
                gui_win_ = clap_plugin_window_open(act->plugin, gui_ext, title.c_str());
            }
        }
#endif

        // --- Mapped param rows (only non-empty slots) + "+ Add param…" ---
        static constexpr float kRowH = 20.f;
        static constexpr float kGap  =  4.f;
        static constexpr float kXW   = 16.f;
        static constexpr float kValW = 74.f;

        const float sli_w = w * 0.27f;
        const float x_btn = x + w - kXW;
        const float val_x = x_btn - kValW - kGap;
        const float sli_x = val_x - sli_w - kGap;
        const float nam_w = sli_x - x - kGap;
        const float lh    = line_height_or(d, o, 12.f);

        if (m.left_released) drag_macro_ = -1;

        // Process keyboard/char events while a text field is active.
        bool text_confirmed = false;
        if (text_field_.active) {
            ctx->wants_keyboard = 1;
            for (uint32_t ei = 0; ei < ctx->char_event_count; ++ei)
                text_field_.handle_char(ctx->char_events[ei]);
            for (uint32_t ei = 0; ei < ctx->key_event_count; ++ei)
                if (text_field_.handle_key(ctx->key_events[ei].key, ctx->key_events[ei].action))
                    text_confirmed = true;
        }

        bool any_empty = false;
        for (int i = 0; i < 8; ++i) {
            if (macro_id_[i]->str_value.empty()) { any_empty = true; continue; }

            const float row_y = y;
            float val = macro_float_[i]->value;

            // Native value (CLAP: linear rescale via cached range; no units)
            double native = static_cast<double>(val);
            if (macro_map_[i].id != CLAP_INVALID_ID)
                native = macro_map_[i].min_val + val * (macro_map_[i].max_val - macro_map_[i].min_val);
            char val_buf[32];
            std::snprintf(val_buf, sizeof(val_buf), "%.3g", native);

            // Param name — clipped
            if (d.push_clip_rect) d.push_clip_rect(o, x, row_y, nam_w, kRowH);
            if (d.draw_text)      d.draw_text(o, x, row_y + (kRowH - lh) * 0.5f,
                                              macro_id_[i]->str_value.c_str(), th.bright_text, 0.85f);
            if (d.pop_clip_rect)  d.pop_clip_rect(o);

            // Slider
            bool over_sli = m.x >= sli_x && m.x <= sli_x + sli_w
                         && m.y >= row_y  && m.y <= row_y + kRowH;
            if (m.left_clicked && over_sli) drag_macro_ = i;
            if (drag_macro_ == i && m.left_down) {
                val = std::clamp((m.x - sli_x) / sli_w, 0.f, 1.f);
                ctx->commands.set_param(ctx->commands.opaque, macro_float_[i]->name, val);
            }
            draw_meter(d, o, sli_x, row_y + (kRowH - 8.f) * 0.5f, sli_w, 8.f,
                       val, th.slider_fill, th.slider_track, MeterOrientation::Horizontal, 2.f);

            // Native value label / inline text field
            bool over_val    = m.x >= val_x && m.x <= val_x + kValW
                            && m.y >= row_y  && m.y <= row_y + kRowH;
            bool editing_this = text_field_.active && text_field_.slot == i;

            if (m.left_clicked && over_val && !text_field_.active) {
                char init[32];
                std::snprintf(init, sizeof(init), "%.6g", native);
                text_field_.open(init, i);
            }

            if (editing_this) {
                if (text_confirmed) {
                    float typed = text_field_.parsed_value(static_cast<float>(native));
                    double range = macro_map_[i].max_val - macro_map_[i].min_val;
                    float norm   = range > 1e-9
                                 ? static_cast<float>((typed - macro_map_[i].min_val) / range)
                                 : 0.f;
                    norm = std::clamp(norm, 0.f, 1.f);
                    ctx->commands.set_param(ctx->commands.opaque, macro_float_[i]->name, norm);
                    text_field_.close();
                }
                if (m.left_clicked && !over_val) text_field_.close();
                draw_inline_numeric_field(d, o, val_x, row_y, kValW, kRowH, text_field_,
                                          with_alpha(th.slider_track, 1.f), th.accent,
                                          th.bright_text, ctx->time);
            } else {
                if (d.push_clip_rect) d.push_clip_rect(o, val_x, row_y, kValW, kRowH);
                if (d.draw_text)      d.draw_text(o, val_x, row_y + (kRowH - lh) * 0.5f,
                                                  val_buf, th.dim_text, 0.75f);
                if (d.pop_clip_rect)  d.pop_clip_rect(o);
            }

            // × clear button
            bool over_x = m.x >= x_btn && m.x <= x_btn + kXW
                       && m.y >= row_y  && m.y <= row_y + kRowH;
            VividColor xcol = over_x ? th.bright_text : th.dim_text;
            if (d.draw_text)
                d.draw_text(o, x_btn + (kXW - lh) * 0.5f, row_y + (kRowH - lh) * 0.5f,
                            "x", xcol, 0.85f);
            if (m.left_clicked && over_x) {
                ctx->commands.set_string_param(ctx->commands.opaque, macro_id_[i]->name, "");
                ctx->commands.set_param(ctx->commands.opaque, macro_float_[i]->name, 0.f);
            }

            y += kRowH + 2.f;
        }

        // "+ Add param…" — only when at least one slot is available
        if (any_empty) {
            bool was_add_open = add_picker_state_.open;
            int picked = draw_compact_picker(ctx, y, x, w, param_names_cache_, -1,
                                             add_picker_state_, "+ Add param\xe2\x80\xa6");
            if (!was_add_open && add_picker_state_.open)
                picker_state_.open = false;
            if (picked >= 0) {
                for (int j = 0; j < 8; ++j) {
                    if (macro_id_[j]->str_value.empty()) {
                        ctx->commands.set_string_param(ctx->commands.opaque,
                                                       macro_id_[j]->name,
                                                       param_names_cache_[picked].c_str());
                        break;
                    }
                }
            }
        }

        ctx->consumed_height = y - ctx->content_y;
    }
};

VIVID_DEFINE_OP(CLAPInstrument) {
    display_name = "CLAP Instrument";
    keywords     = {"plugin", "vst", "synth", "instrument", "external"};
    summary      = "Hosts a CLAP instrument plugin; receives MIDI notes, outputs audio.";
}

VIVID_INSPECTOR_FULL_MODE(CLAPInstrument)
