#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "shared/vst3_host/vst3_host_common.h"
#include "shared/vst3_host/vst3_scanner.h"
#include "shared/plugin_ui/plugin_picker.h"
#include "shared/plugin_common/direct_param_queue.h"
#ifdef __APPLE__
#include "shared/vst3_host/vst3_plugin_window.h"
#endif

#include <atomic>
#include <cmath>

// ---------------------------------------------------------------------------
// Vst3Instrument — hosts a VST3 instrument plugin as a Vivid audio operator.
//
// Parameters visible in the inspector:
//   plugin_name   — display key (plugin name from CFBundleName / bundle stem)
//   macro_0..7    — float 0-1, each mapped to a VST3 param by name via macro_N_id
//
// Plugin state is serialized via IComponent::getState/setState and stored in
// plugin_state (base64, hidden). Parameters are VST3-normalized [0,1].
//
// Threading model mirrors CLAPInstrument:
//   main thread : load, activate, deactivate, destroy
//   audio thread: setProcessing, process, per-block swap
//
// Triple-buffer slot pattern:
//   pending_ — loaded+activated by main thread, awaiting audio thread swap
//   active_  — currently being processed by the audio thread
//   dying_   — stopped by audio thread, awaiting main-thread cleanup
// ---------------------------------------------------------------------------

struct Vst3Instrument : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName        = "Vst3Instrument";
    static constexpr bool        kTimeDependent = true;

    // --- Params ---
    vivid::Param<vivid::TextValue> plugin_name {"plugin_name"};

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

    vivid::Param<vivid::TextValue> plugin_state       {"plugin_state"};
    vivid::Param<vivid::TextValue> vst3_params_       {"_vst3_params"};
    // Space-separated "id:normalized_val" pairs written by set_vst3_param MCP tool.
    // main_thread_update() pushes each entry into direct_q_ for audio-thread delivery.
    // Cleared on plugin change so stale IDs are never sent to a different plugin.
    vivid::Param<vivid::TextValue> direct_params_     {"_vst3_direct_params"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&plugin_name);
        out.push_back(&macro_0); out.push_back(&macro_0_id);
        out.push_back(&macro_1); out.push_back(&macro_1_id);
        out.push_back(&macro_2); out.push_back(&macro_2_id);
        out.push_back(&macro_3); out.push_back(&macro_3_id);
        out.push_back(&macro_4); out.push_back(&macro_4_id);
        out.push_back(&macro_5); out.push_back(&macro_5_id);
        out.push_back(&macro_6); out.push_back(&macro_6_id);
        out.push_back(&macro_7); out.push_back(&macro_7_id);
        out.push_back(&plugin_state);
        out.push_back(&vst3_params_);
        out.push_back(&direct_params_);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
    }

    // --- Plugin slot atomics ---
    std::atomic<Vst3Handle*> active_  {nullptr};
    std::atomic<Vst3Handle*> pending_ {nullptr};
    std::atomic<Vst3Handle*> dying_   {nullptr};
    std::atomic<Vst3Handle*> dying2_  {nullptr};

    // --- Per-macro resolved VST3 param ID cache ---
    // Both threads access macro_map_: main thread writes in update_macro_map(),
    // audio thread reads+writes last_sent in build_macro_events(). Use atomics.
    struct MacroEntry {
        std::atomic<Steinberg::Vst::ParamID> id        {static_cast<Steinberg::Vst::ParamID>(-1)};
        std::atomic<float>                   last_sent {-1.f};
    };
    static constexpr Steinberg::Vst::ParamID kInvalidParamID
        = static_cast<Steinberg::Vst::ParamID>(-1);
    MacroEntry macro_map_[8];

    vivid::Param<float>*            macro_float_[8];
    vivid::Param<vivid::TextValue>* macro_id_[8];

    // --- Plugin GUI window (main thread only) ---
#ifdef __APPLE__
    Vst3PluginWindow* gui_win_ = nullptr;
#endif
    vivid::plugin_ui::PluginPickerState picker_state_;
    vivid::plugin_ui::PluginPickerState add_picker_state_;
    int drag_macro_ = -1;
    vivid::draw_ui::InlineNumericField  text_field_;

    // Param name list for inspector dropdowns, rebuilt every frame from active/pending.
    std::vector<std::string> param_names_cache_;

    // --- State ---
    std::string   last_name_;
    std::string   last_applied_state_;   // plugin_state.str_value at last vst3_load_plugin call
    uint64_t      steady_sample_  = 0;
    uint32_t      sample_rate_    = 48000;
    uint32_t      loaded_rate_    = 0;    // sample rate used at last plugin load
    std::atomic<uint32_t> audio_rate_seen_{0};  // written by audio thread, read by main thread
    bool          state_dirty_    = false;
    uint32_t      save_state_frame_ = 0; // throttle: only call getState() ~1/sec

    // --- Direct param queue (main thread → audio thread, unlimited params) ---
    DirectParamQueue direct_q_;
    std::string      last_direct_params_;  // tracks last-pushed value to avoid re-push

    // --- Per-frame event buffers (audio thread only) ---
    Vst3EventList   in_events_;
    Vst3ParamChanges param_changes_;

    // --- Stable host application context ---
    Vst3HostApp host_app_;

    // -----------------------------------------------------------------------

    Vst3Instrument() {
        macro_float_[0] = &macro_0; macro_id_[0] = &macro_0_id;
        macro_float_[1] = &macro_1; macro_id_[1] = &macro_1_id;
        macro_float_[2] = &macro_2; macro_id_[2] = &macro_2_id;
        macro_float_[3] = &macro_3; macro_id_[3] = &macro_3_id;
        macro_float_[4] = &macro_4; macro_id_[4] = &macro_4_id;
        macro_float_[5] = &macro_5; macro_id_[5] = &macro_5_id;
        macro_float_[6] = &macro_6; macro_id_[6] = &macro_6_id;
        macro_float_[7] = &macro_7; macro_id_[7] = &macro_7_id;

        vivid::description(plugin_name,  "VST3 plugin to load (\"Name [Vendor]\" from list_vst3_plugins)");
        vivid::display_hint(plugin_name, VIVID_DISPLAY_HIDDEN);
        vivid::description(macro_0, "Macro 0 value (0-1 normalized), mapped to the VST3 param named in macro_0_id");
        vivid::description(macro_0_id, "VST3 parameter name for macro 0");
        vivid::description(macro_1, "Macro 1 value (0-1)");
        vivid::description(macro_1_id, "VST3 parameter name for macro 1");
        vivid::description(macro_2, "Macro 2 value (0-1)");
        vivid::description(macro_2_id, "VST3 parameter name for macro 2");
        vivid::description(macro_3, "Macro 3 value (0-1)");
        vivid::description(macro_3_id, "VST3 parameter name for macro 3");
        vivid::description(macro_4, "Macro 4 value (0-1)");
        vivid::description(macro_4_id, "VST3 parameter name for macro 4");
        vivid::description(macro_5, "Macro 5 value (0-1)");
        vivid::description(macro_5_id, "VST3 parameter name for macro 5");
        vivid::description(macro_6, "Macro 6 value (0-1)");
        vivid::description(macro_6_id, "VST3 parameter name for macro 6");
        vivid::description(macro_7, "Macro 7 value (0-1)");
        vivid::description(macro_7_id, "VST3 parameter name for macro 7");
        // Float sliders drawn manually; id strings must NOT be hidden so the
        // snapshot includes them (hidden params are stripped from file_param_values,
        // causing _vivid_sync_params to clobber str_value back to "" each frame).
        // VIVID_INSPECTOR_FULL_MODE suppresses the auto-rendered controls section.
        for (int i = 0; i < 8; ++i)
            vivid::display_hint(*macro_float_[i], VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(plugin_state,   VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(vst3_params_,   VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(direct_params_, VIVID_DISPLAY_HIDDEN);
    }

    ~Vst3Instrument() {
#ifdef __APPLE__
        if (gui_win_) { vst3_plugin_window_close(gui_win_); gui_win_ = nullptr; }
#endif
        auto* act  = active_.exchange(nullptr,  std::memory_order_acq_rel);
        auto* pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
        auto* dead = dying_.exchange(nullptr,   std::memory_order_acq_rel);
        auto* dead2 = dying2_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_handle(act,  true);
        destroy_handle(pend, false);
        destroy_handle(dead, false);
        destroy_handle(dead2, false);
    }

    // -----------------------------------------------------------------------
    // Main-thread lifecycle
    // -----------------------------------------------------------------------

    void prepare_instance_assets() override {
        vst3_scan_plugins();
        reload_if_changed();
    }

    void main_thread_update(double /*time*/) override {
        // Clean up handles stopped by the audio thread
        auto* dead  = dying_.exchange(nullptr,  std::memory_order_acq_rel);
        auto* dead2 = dying2_.exchange(nullptr, std::memory_order_acq_rel);
        destroy_handle(dead,  false);
        destroy_handle(dead2, false);

        // Apply state changes to the already-active handle without a full reload.
        // Loading the plugin with saved state during vst3_load_plugin causes some plugins
        // (e.g. Pigments) to crash: setState triggers a CFRunLoop source0 that fires
        // before the plugin's DSP is initialized (before setProcessing(true)). Applying
        // state here — after the first audio buffer — avoids that ordering hazard.
        {
            auto* act = active_.load(std::memory_order_acquire);
            if (act && act->component &&
                plugin_name.str_value == last_name_ &&
                !plugin_state.str_value.empty() &&
                plugin_state.str_value != last_applied_state_) {
                vst3_load_state(act, plugin_state.str_value);
                last_applied_state_ = plugin_state.str_value;
            }
        }

        // If the audio thread reported a sample rate different from what we used to load
        // the plugin, reload with the correct rate so setupProcessing and ProcessContext
        // are consistent. State is preserved: plugin_state is not cleared.
        {
            uint32_t seen = audio_rate_seen_.load(std::memory_order_acquire);
            if (seen > 0 && seen != loaded_rate_ && !last_name_.empty()) {
                sample_rate_ = seen;
                reload_for_rate_change();
            }
        }

        reload_if_changed();
        update_macro_map();
        refresh_vst3_params_json();
        process_direct_params();

        save_state();

#ifdef __APPLE__
        if (gui_win_ && !vst3_plugin_window_is_open(gui_win_)) {
            vst3_plugin_window_close(gui_win_);
            gui_win_ = nullptr;
        }
#endif
    }

    void reload_if_changed() {
        if (plugin_name.str_value == last_name_) return;
        const bool switching_plugin = !last_name_.empty();
        last_name_ = plugin_name.str_value;

#ifdef __APPLE__
        if (gui_win_) { vst3_plugin_window_close(gui_win_); gui_win_ = nullptr; }
#endif

        for (auto& m : macro_map_) { m.id = kInvalidParamID; m.last_sent = -1.f; }

        // Clear direct params so stale IDs aren't applied to the incoming plugin.
        direct_params_.str_value.clear();
        last_direct_params_.clear();

        // Clear stale state only when switching from one plugin to another.
        // On a fresh instance after a topology change, last_name_ was "" so
        // switching_plugin is false — plugin_state holds the snapshot-restored
        // state and must survive to be applied via the deferred setState path.
        // When actually changing plugins, clear it to prevent cross-plugin bleed.
        if (switching_plugin) {
            plugin_state.str_value.clear();
        }

        if (last_name_.empty()) {
            auto* old = pending_.exchange(new Vst3Handle(), std::memory_order_acq_rel);
            destroy_handle(old, false);
            return;
        }

        const Vst3PluginInfo* pi = vst3_find_by_key(last_name_);
        if (!pi) {
            fprintf(stderr, "[Vst3Instrument] unknown plugin '%s'\n", last_name_.c_str());
            return;
        }

        // Load with empty state; saved state is applied to the active handle
        // in main_thread_update after setProcessing(true) runs on the audio thread.
        Vst3Handle* h = vst3_load_plugin(
            pi->path.c_str(), pi->uid_hex.c_str(),
            sample_rate_, "", &host_app_);
        if (!h) return;

        h->component_handler.state_dirty = &state_dirty_;
        loaded_rate_ = sample_rate_;
        auto* old = pending_.exchange(h, std::memory_order_acq_rel);
        destroy_handle(old, false);
        last_applied_state_ = "";  // will be set once state is applied via main_thread_update
        state_dirty_ = true;
    }

    // Reload the already-named plugin at a new sample rate, preserving saved state.
    void reload_for_rate_change() {
        const Vst3PluginInfo* pi = vst3_find_by_key(last_name_);
        if (!pi) return;
        Vst3Handle* h = vst3_load_plugin(
            pi->path.c_str(), pi->uid_hex.c_str(),
            sample_rate_, "", &host_app_);
        if (!h) return;

        h->component_handler.state_dirty = &state_dirty_;
        loaded_rate_ = sample_rate_;
        auto* old = pending_.exchange(h, std::memory_order_acq_rel);
        destroy_handle(old, false);
        last_applied_state_ = "";  // state will be re-applied via the post-swap path
        state_dirty_ = true;
    }

    void update_macro_map() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || act->params.empty()) return;

        for (int i = 0; i < 8; ++i) {
            const std::string& name = macro_id_[i]->str_value;
            if (name.empty()) { macro_map_[i].id.store(kInvalidParamID, std::memory_order_relaxed); continue; }
            if (macro_map_[i].id.load(std::memory_order_relaxed) != kInvalidParamID) continue;

            for (const auto& p : act->params) {
                if (p.name == name) {
                    macro_map_[i].last_sent.store(-1.f, std::memory_order_relaxed);
                    macro_map_[i].id.store(p.id, std::memory_order_release);
                    break;
                }
            }
        }
    }

    void refresh_vst3_params_json() {
        auto* act = active_.load(std::memory_order_acquire);
        std::string new_json = vst3_params_to_json(act);
        if (new_json != vst3_params_.str_value)
            vst3_params_.str_value = std::move(new_json);
    }

    void process_direct_params() {
        if (direct_params_.str_value == last_direct_params_) return;
        last_direct_params_ = direct_params_.str_value;
        direct_param_queue_parse_and_push(direct_q_, direct_params_.str_value);
    }

    void destroy_handle(Vst3Handle* h, bool was_processing) {
        if (!h) return;
        if (was_processing && h->processor && h->processing) {
            h->processor->setProcessing(false);
            h->processing = false;
        }
        delete h;  // ~Vst3Handle calls h->destroy()
    }

    void save_state() {
        // Skip while a new plugin is pending: active_ still holds the old handle,
        // so getState() would capture the old plugin's state and overwrite plugin_state,
        // causing it to be applied to the new plugin after the next topology change.
        if (pending_.load(std::memory_order_acquire)) return;
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || !act->component) return;

        // getState() called while process() runs concurrently is not guaranteed thread-safe.
        // Many plugins (including Serum2) internally serialize these, causing the audio thread
        // to stall for one full callback → audible silent dropout. Rely on performEdit()/setDirty()
        // callbacks (wired via component_handler.state_dirty) to signal when state actually changes.
        // The 3600-frame fallback (~1 min) covers non-conformant plugins that skip the callbacks.
        if (!state_dirty_ && ++save_state_frame_ < 3600) return;
        save_state_frame_ = 0;

        std::string s = vst3_save_state(act);
        if (!s.empty() && s != plugin_state.str_value) {
            plugin_state.str_value = s;
            last_applied_state_ = std::move(s);
        }
        state_dirty_ = false;
    }

    // -----------------------------------------------------------------------
    // Audio thread
    // -----------------------------------------------------------------------

    void process_audio(const VividAudioContext* ctx) override {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        sample_rate_ = ctx->sample_rate;
        audio_rate_seen_.store(ctx->sample_rate, std::memory_order_relaxed);

        // Check for a pending plugin swap
        Vst3Handle* pend = pending_.load(std::memory_order_acquire);
        if (pend) {
            pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
            if (pend) {
                Vst3Handle* old = active_.exchange(pend, std::memory_order_acq_rel);
                if (old) {
                    if (old->processing) {
                        old->processor->setProcessing(false);
                        old->processing = false;
                    }
                    auto* prev = dying_.exchange(old, std::memory_order_acq_rel);
                    if (prev) {
                        auto* prev2 = dying2_.exchange(prev, std::memory_order_acq_rel);
                        if (prev2) {
                            // Main thread didn't drain dying2_ in time — three consecutive
                            // plugin swaps without a frame tick. Accept the leak rather than
                            // blocking the audio thread with a delete.
                            fprintf(stderr, "[Vst3Instrument] dying2_ overflow; handle leaked\n");
                        }
                    }
                }
                if (!pend->component) {
                    active_.store(nullptr, std::memory_order_release);
                    delete pend;
                    zero_outputs(ctx);
                    return;
                }
                if (pend->processor->setProcessing(true) != kResultOk)
                    fprintf(stderr, "[Vst3Instrument] setProcessing(true) failed\n");
                pend->processing = true;
            }
        }

        Vst3Handle* act = active_.load(std::memory_order_acquire);
        if (!act || !act->processing) { zero_outputs(ctx); return; }

        in_events_.clear();
        param_changes_.clear();
        build_note_events(ctx);
        build_macro_events();

        float* out_l = ctx->output_buffers[0];
        float* out_r = ctx->output_buffers[0] + ctx->buffer_size;
        float* channels[2] = { out_l, out_r };

        AudioBusBuffers output_bus{};
        output_bus.channelBuffers32 = channels;
        output_bus.numChannels      = 2;
        output_bus.silenceFlags     = 0;

        ProcessContext pctx = vst3_build_process_context(ctx, steady_sample_);

        ProcessData data{};
        data.processMode             = kRealtime;
        data.symbolicSampleSize      = kSample32;
        data.numSamples              = static_cast<int32>(ctx->buffer_size);
        data.numInputs               = 0;
        data.numOutputs              = 1;
        data.inputs                  = nullptr;
        data.outputs                 = &output_bus;
        data.inputEvents             = &in_events_;
        data.outputEvents            = nullptr;
        data.inputParameterChanges   = &param_changes_;
        data.outputParameterChanges  = nullptr;
        data.processContext          = &pctx;

        // VST3 runs in-process; a misbehaving plugin that segfaults or throws here
        // will crash Vivid. There is no signal handler or crash isolation.
        act->processor->process(data);
        steady_sample_ += ctx->buffer_size;
    }

    void build_note_events(const VividAudioContext* ctx) {
        using namespace Steinberg::Vst;
        if (!ctx->custom_inputs || ctx->custom_input_count == 0 || !ctx->custom_inputs[0])
            return;
        auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        for (uint32_t i = 0; i < notes->count; ++i) {
            const auto& ev = notes->events[i];
            int32 t = static_cast<int32>(
                std::min(static_cast<uint64_t>(ev.frame_offset_samples),
                         static_cast<uint64_t>(ctx->buffer_size - 1)));

            Event e{};
            if (ev.type == VIVID_NOTE_ON) {
                e.type              = Event::kNoteOnEvent;
                e.sampleOffset      = t;
                e.noteOn.pitch      = static_cast<int16>(ev.note_number);
                e.noteOn.velocity   = ev.value;
                e.noteOn.noteId     = static_cast<int32>(ev.note_id & 0x7FFFFFFF);
                e.noteOn.channel    = 0;
                e.noteOn.tuning     = 0.f;
                in_events_.addEvent(e);
            } else if (ev.type == VIVID_NOTE_OFF) {
                e.type              = Event::kNoteOffEvent;
                e.sampleOffset      = t;
                e.noteOff.pitch     = (ev.note_number != 255)
                                        ? static_cast<int16>(ev.note_number) : -1;
                e.noteOff.velocity  = ev.value;
                e.noteOff.noteId    = static_cast<int32>(ev.note_id & 0x7FFFFFFF);
                e.noteOff.channel   = 0;
                e.noteOff.tuning    = 0.f;
                in_events_.addEvent(e);
            } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                // VST3 has no dedicated per-note pitch-bend event; kPolyPressureEvent
                // is the closest per-note continuous event supported by the spec.
                e.type              = Event::kPolyPressureEvent;
                e.sampleOffset      = t;
                e.polyPressure.pitch    = ev.note_number;
                e.polyPressure.pressure = ev.value;
                e.polyPressure.noteId   = static_cast<int32>(ev.note_id & 0x7FFFFFFF);
                e.polyPressure.channel  = 0;
                in_events_.addEvent(e);
            } else if (ev.type == VIVID_NOTE_PRESSURE) {
                e.type              = Event::kPolyPressureEvent;
                e.sampleOffset      = t;
                e.polyPressure.pitch    = ev.note_number;
                e.polyPressure.pressure = ev.value;
                e.polyPressure.noteId   = static_cast<int32>(ev.note_id & 0x7FFFFFFF);
                e.polyPressure.channel  = 0;
                in_events_.addEvent(e);
            }
        }
    }

    void build_macro_events() {
        const float vals[8] = {
            macro_0.value, macro_1.value, macro_2.value, macro_3.value,
            macro_4.value, macro_5.value, macro_6.value, macro_7.value,
        };
        for (int i = 0; i < 8; ++i) {
            const auto id = macro_map_[i].id.load(std::memory_order_acquire);
            if (id == kInvalidParamID) continue;
            const float last = macro_map_[i].last_sent.load(std::memory_order_relaxed);
            if (std::fabs(vals[i] - last) < 1e-6f) continue;
            macro_map_[i].last_sent.store(vals[i], std::memory_order_relaxed);
            param_changes_.add(id, static_cast<Steinberg::Vst::ParamValue>(vals[i]));
        }
        // Drain unlimited direct-param queue (written by set_vst3_param MCP tool)
        DirectParamQueue::Entry e;
        while (direct_q_.pop(e))
            param_changes_.add(e.id, static_cast<Steinberg::Vst::ParamValue>(e.val));
    }

    static void zero_outputs(const VividAudioContext* ctx) {
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);
    }

    // -----------------------------------------------------------------------
    // Inspector (VIVID_INSPECTOR) — plugin picker, GUI button, macro rows.
    // -----------------------------------------------------------------------

    void draw_inspector(VividInspectorContext* ctx) override {
        using namespace vivid::plugin_ui;
        using namespace vivid::draw_ui;

        vst3_scan_plugins();
        const auto& plugins = vst3_get_plugins();

        // Rebuild param name list from active handle, falling back to pending if audio
        // thread hasn't yet processed the swap (e.g., audio device not yet running).
        {
            auto* h = active_.load(std::memory_order_acquire);
            if (!h || h->params.empty())
                h = pending_.load(std::memory_order_acquire);
            param_names_cache_.clear();
            if (h) {
                param_names_cache_.reserve(h->params.size());
                for (const auto& p : h->params)
                    param_names_cache_.push_back(p.name);
            }
        }

        // Build plugin display name list and find current selection.
        std::vector<std::string> plugin_names;
        plugin_names.reserve(plugins.size());
        for (const auto& p : plugins) plugin_names.push_back(p.name);
        int cur_plugin = -1;
        for (int i = 0; i < (int)plugins.size(); ++i) {
            if (plugins[i].name == last_name_) { cur_plugin = i; break; }
        }

        const float x  = ctx->content_x;
        const float w  = ctx->content_width;
        const auto& m  = ctx->mouse;
        auto& d        = ctx->draw;
        void* o        = d.opaque;
        const auto& th = ctx->theme;

        float y = ctx->content_y + 4.f;

        // Plugin picker
        bool was_picker_open = picker_state_.open;
        int sel = draw_plugin_picker(ctx, y, plugin_names, cur_plugin, picker_state_);
        if (!was_picker_open && picker_state_.open)
            add_picker_state_.open = false;
        if (sel >= 0)
            ctx->commands.set_string_param(ctx->commands.opaque,
                                           "plugin_name", plugins[sel].name.c_str());

        y += 4.f;

#ifdef __APPLE__
        if (gui_win_ && !vst3_plugin_window_is_open(gui_win_)) {
            vst3_plugin_window_close(gui_win_);
            gui_win_ = nullptr;
        }
        // Snapshot controller before draw_open_gui_button — audio thread can swap
        // active_ at any time, making act->controller a dangling pointer by the
        // time we use it if we load act here and dereference it below.
        auto* act = active_.load(std::memory_order_acquire);
        auto* ctl = act ? act->controller : nullptr;
        bool has_gui = ctl != nullptr;
        bool opened = draw_open_gui_button(ctx, y, has_gui, gui_win_ != nullptr);
        if (opened && ctl) {
            std::string title = last_name_.empty() ? "VST3 Plugin" : last_name_;
            gui_win_ = vst3_plugin_window_open(ctl, title.c_str());
        }
        y += 4.f;
#endif

        // --- Mapped param rows (only non-empty slots) + "+ Add param…" ---
        static constexpr float kRowH = 20.f;
        static constexpr float kGap  =  4.f;
        static constexpr float kXW   = 16.f;   // × button width
        static constexpr float kValW = 74.f;   // native value + units label

        const float sli_w = w * 0.27f;
        const float x_btn = x + w - kXW;
        const float val_x = x_btn - kValW - kGap;
        const float sli_x = val_x - sli_w - kGap;
        const float nam_w = sli_x - x - kGap;
        const float lh    = line_height_or(d, o, 12.f);

        // Snapshot controller for native value lookup (main-thread safe)
        auto* ch = active_.load(std::memory_order_acquire);
        if (!ch || !ch->controller) ch = pending_.load(std::memory_order_acquire);
        auto* vctl = (ch && ch->controller) ? ch->controller : nullptr;

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
            const std::string& name = macro_id_[i]->str_value;
            float val = macro_float_[i]->value;

            // Native value + units via VST3 controller
            double native = static_cast<double>(val);
            std::string units_str;
            auto pid = kInvalidParamID;
            if (vctl) {
                pid = macro_map_[i].id.load(std::memory_order_acquire);
                if (pid != kInvalidParamID) {
                    native = vctl->normalizedParamToPlain(pid,
                                 static_cast<Steinberg::Vst::ParamValue>(val));
                    if (ch) {
                        for (const auto& p : ch->params)
                            if (p.id == pid) { units_str = p.units; break; }
                    }
                }
            }
            char val_buf[40];
            if (!units_str.empty())
                std::snprintf(val_buf, sizeof(val_buf), "%.3g %s", native, units_str.c_str());
            else
                std::snprintf(val_buf, sizeof(val_buf), "%.3g", native);

            // Param name — clipped
            if (d.push_clip_rect) d.push_clip_rect(o, x, row_y, nam_w, kRowH);
            if (d.draw_text)      d.draw_text(o, x, row_y + (kRowH - lh) * 0.5f,
                                              name.c_str(), th.bright_text, 0.85f);
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
                char init[40];
                std::snprintf(init, sizeof(init), "%.6g", native);
                text_field_.open(init, i);
            }

            if (editing_this) {
                if (text_confirmed) {
                    float typed  = text_field_.parsed_value(static_cast<float>(native));
                    float norm   = vctl && pid != kInvalidParamID
                                 ? static_cast<float>(vctl->plainParamToNormalized(pid, typed))
                                 : typed;
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

VIVID_DEFINE_OP(Vst3Instrument) {
    display_name = "VST3 Instrument";
    keywords     = {"vst3", "plugin", "synth", "instrument", "external"};
    summary      = "Hosts a VST3 instrument plugin; receives MIDI notes, outputs stereo audio.";
}

VIVID_INSPECTOR_FULL_MODE(Vst3Instrument)
