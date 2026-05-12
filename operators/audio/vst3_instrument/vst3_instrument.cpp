#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/draw_ui_helpers.h"
#include "shared/vst3_host/vst3_host_common.h"
#include "shared/vst3_host/vst3_scanner.h"
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

    vivid::Param<vivid::TextValue> plugin_state  {"plugin_state"};
    vivid::Param<vivid::TextValue> vst3_params_  {"_vst3_params"};

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
    struct MacroEntry {
        Steinberg::Vst::ParamID id          = static_cast<Steinberg::Vst::ParamID>(-1);
        float                   last_sent   = -1.f;
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

    // --- State ---
    std::string   last_name_;
    uint64_t      steady_sample_  = 0;
    uint32_t      sample_rate_    = 48000;
    bool          state_dirty_    = false;

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
        vivid::description(macro_0, "Macro 0 value (0-1 normalized), mapped to the VST3 param named in macro_0_id");
        vivid::description(macro_0_id, "VST3 parameter name for macro 0");
        vivid::description(macro_1, "Macro 1 value (0-1)"); vivid::description(macro_1_id, "VST3 parameter name for macro 1");
        vivid::description(macro_2, "Macro 2 value (0-1)"); vivid::description(macro_2_id, "VST3 parameter name for macro 2");
        vivid::description(macro_3, "Macro 3 value (0-1)"); vivid::description(macro_3_id, "VST3 parameter name for macro 3");
        vivid::description(macro_4, "Macro 4 value (0-1)"); vivid::description(macro_4_id, "VST3 parameter name for macro 4");
        vivid::description(macro_5, "Macro 5 value (0-1)"); vivid::description(macro_5_id, "VST3 parameter name for macro 5");
        vivid::description(macro_6, "Macro 6 value (0-1)"); vivid::description(macro_6_id, "VST3 parameter name for macro 6");
        vivid::description(macro_7, "Macro 7 value (0-1)"); vivid::description(macro_7_id, "VST3 parameter name for macro 7");
        vivid::display_hint(plugin_state, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(vst3_params_, VIVID_DISPLAY_HIDDEN);
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

        reload_if_changed();
        update_macro_map();
        refresh_vst3_params_json();

        if (state_dirty_) save_state();

#ifdef __APPLE__
        if (gui_win_ && !vst3_plugin_window_is_open(gui_win_)) {
            vst3_plugin_window_close(gui_win_);
            gui_win_ = nullptr;
        }
#endif
    }

    void reload_if_changed() {
        if (plugin_name.str_value == last_name_) return;
        last_name_ = plugin_name.str_value;

#ifdef __APPLE__
        if (gui_win_) { vst3_plugin_window_close(gui_win_); gui_win_ = nullptr; }
#endif

        for (auto& m : macro_map_) { m.id = kInvalidParamID; m.last_sent = -1.f; }

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

        Vst3Handle* h = vst3_load_plugin(
            pi->path.c_str(), pi->uid_hex.c_str(),
            sample_rate_, plugin_state.str_value, &host_app_);
        if (!h) return;

        auto* old = pending_.exchange(h, std::memory_order_acq_rel);
        destroy_handle(old, false);
        state_dirty_ = true;
    }

    void update_macro_map() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || act->params.empty()) return;

        for (int i = 0; i < 8; ++i) {
            const std::string& name = macro_id_[i]->str_value;
            if (name.empty()) { macro_map_[i].id = kInvalidParamID; continue; }
            if (macro_map_[i].id != kInvalidParamID) continue;

            for (const auto& p : act->params) {
                if (p.name == name) {
                    macro_map_[i].id        = p.id;
                    macro_map_[i].last_sent = -1.f;
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

    void destroy_handle(Vst3Handle* h, bool was_processing) {
        if (!h) return;
        if (was_processing && h->processor && h->processing) {
            h->processor->setProcessing(false);
            h->processing = false;
        }
        delete h;  // ~Vst3Handle calls h->destroy()
    }

    void save_state() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || !act->component) return;
        std::string s = vst3_save_state(act);
        if (!s.empty()) {
            plugin_state.str_value = std::move(s);
            state_dirty_ = false;
        }
    }

    // -----------------------------------------------------------------------
    // Audio thread
    // -----------------------------------------------------------------------

    void process_audio(const VividAudioContext* ctx) override {
        using namespace Steinberg;
        using namespace Steinberg::Vst;

        sample_rate_ = ctx->sample_rate;

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
                    if (prev) dying2_.store(prev, std::memory_order_release);
                }
                if (!pend->component) {
                    active_.store(nullptr, std::memory_order_release);
                    delete pend;
                    zero_outputs(ctx);
                    return;
                }
                pend->processor->setProcessing(true);
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
            if (macro_map_[i].id == kInvalidParamID) continue;
            if (std::fabs(vals[i] - macro_map_[i].last_sent) < 1e-6f) continue;
            macro_map_[i].last_sent = vals[i];
            param_changes_.add(macro_map_[i].id,
                               static_cast<Steinberg::Vst::ParamValue>(vals[i]));
        }
    }

    static void zero_outputs(const VividAudioContext* ctx) {
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);
    }

    // -----------------------------------------------------------------------
    // Editor (VIVID_EDITOR) — opens the VST3 plugin GUI in a native NSWindow.
    // -----------------------------------------------------------------------

    static VividEditorMetadata editor_metadata() {
        VividEditorMetadata m{};
        m.default_width  = 400;
        m.default_height = 140;
        m.min_width      = 300;
        m.min_height     = 100;
        m.title_suffix   = nullptr;
        return m;
    }

    void draw_editor(VividEditorContext* ctx) {
#ifdef __APPLE__
        // Sync GUI window state
        if (gui_win_ && !vst3_plugin_window_is_open(gui_win_)) {
            vst3_plugin_window_close(gui_win_);
            gui_win_ = nullptr;
        }

        auto* act      = active_.load(std::memory_order_acquire);
        bool has_plugin = act && act->controller;

        VividDrawAPI& d  = ctx->draw;
        void*         o  = d.opaque;
        const auto&   th = ctx->theme;
        const auto&   m  = ctx->mouse;

        float pad = 16.f;
        float x   = pad;
        float y   = pad;
        float w   = ctx->surface_width - pad * 2.f;

        // Plugin name
        const char* pname = last_name_.empty() ? "(no plugin loaded)" : last_name_.c_str();
        if (d.draw_text) d.draw_text(o, x, y, pname, th.bright_text, 1.f);
        y += vivid::draw_ui::line_height_or(d, o, 14.f) + 10.f;

        // "Open Plugin UI" button
        float bh = 32.f;
        if (has_plugin) {
            bool gui_open  = gui_win_ != nullptr;
            VividColor fill        = gui_open ? VividColor{0.16f, 0.53f, 0.16f, 1.f}
                                              : VividColor{0.13f, 0.40f, 0.67f, 1.f};
            VividColor active_fill = gui_open ? VividColor{0.20f, 0.67f, 0.20f, 1.f}
                                              : VividColor{0.20f, 0.53f, 0.80f, 1.f};
            const char* label = gui_open ? "Plugin UI Open" : "Open Plugin UI";
            bool hovered = m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + bh;
            vivid::draw_ui::draw_button(d, o, x, y, w, bh, label, hovered,
                                        fill, active_fill, th.bright_text);

            if (m.left_clicked && hovered && !gui_open) {
                std::string title = last_name_.empty() ? "VST3 Plugin" : last_name_;
                gui_win_ = vst3_plugin_window_open(act->controller, title.c_str());
            }
        } else {
            VividColor dim = {0.2f, 0.2f, 0.2f, 1.f};
            vivid::draw_ui::draw_button(d, o, x, y, w, bh, "No plugin loaded",
                                        false, dim, dim, th.dim_text);
        }
#else
        ctx->request_close = 1;
#endif
    }
};

VIVID_DEFINE_OP(Vst3Instrument) {
    display_name = "VST3 Instrument";
    keywords     = {"vst3", "plugin", "synth", "instrument", "external"};
    summary      = "Hosts a VST3 instrument plugin; receives MIDI notes, outputs stereo audio.";
}

VIVID_EDITOR(Vst3Instrument)
