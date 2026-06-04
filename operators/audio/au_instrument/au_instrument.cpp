#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "shared/plugin_ui/plugin_picker.h"

#ifdef __APPLE__
#include "shared/au_host/au_host_common.h"
#include "shared/au_host/au_scanner.h"
#include "shared/plugin_common/direct_param_queue.h"
#include "shared/plugin_common/macro_bank.h"
#include <atomic>
#include <cmath>

// ---------------------------------------------------------------------------
// AUInstrument — hosts an AU v2 MusicDevice plugin as a Vivid audio operator.
//
// Parameters visible in the inspector:
//   plugin_name  — display name from AudioComponentCopyName (e.g. "Surge XT")
//   macro_<n>    — float 0-1, each mapped to an AU param by name via macro_<n>_id
//                  (variadic MacroBank, see shared/plugin_common/macro_bank.h)
//
// Threading model:
//   main thread  : load, init, deactivate, destroy (AudioUnit lifecycle)
//   audio thread : AudioUnitRender, MusicDeviceMIDIEvent, AudioUnitSetParameter
//
// AU v2 limitation: all voices share MIDI channel 1. Per-voice expressions
// (pitch bend, pressure) are sent as channel-wide messages and affect all
// simultaneous notes. This is a known constraint of AU v2 vs. CLAP.
// ---------------------------------------------------------------------------

struct AUInstrument : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName         = "AUInstrument";
    static constexpr bool        kTimeDependent = true;

    // --- Params ---
    vivid::Param<vivid::TextValue> plugin_name {"plugin_name"};

    // Variadic macro bank — see shared/plugin_common/macro_bank.h.
    MacroBank<AudioUnitParameterID, kAUInvalidParamID> bank_;

    // Opaque plugin state as base64-encoded CFPropertyList XML; hidden from inspector.
    vivid::Param<vivid::TextValue> plugin_state {"plugin_state"};

    // JSON array of AU parameters from the loaded plugin; hidden, consumed by list_au_params.
    vivid::Param<vivid::TextValue> au_params_     {"_au_params"};
    vivid::Param<vivid::TextValue> direct_params_ {"_au_direct_params"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&plugin_name);
        bank_.collect(out);
        out.push_back(&plugin_state);
        out.push_back(&au_params_);
        out.push_back(&direct_params_);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
    }

    // --- Plugin slot atomics (triple-buffer, same pattern as CLAPInstrument) ---
    std::atomic<AUHandle*> active_  {nullptr};
    std::atomic<AUHandle*> pending_ {nullptr};
    std::atomic<AUHandle*> dying_   {nullptr};
    std::atomic<AUHandle*> dying2_  {nullptr};

    // --- Transport state (written on audio thread before each render) ---
    AUTransportState transport_state_;

    // --- Inspector state (main thread only) ---
    vivid::plugin_ui::PluginPickerState picker_state_;
    vivid::plugin_ui::PluginPickerState add_picker_state_;
    int drag_macro_ = -1;
    vivid::draw_ui::InlineNumericField  text_field_;
    std::vector<std::string> param_names_cache_;

    // --- Direct param queue (main thread → audio thread, unlimited params) ---
    DirectParamQueue direct_q_;
    std::string      last_direct_params_;

    // --- State ---
    std::string last_name_;
    uint32_t    sample_rate_       = 48000;
    uint32_t    loaded_rate_       = 0;
    std::atomic<uint32_t> audio_rate_seen_ {0};
    bool        state_dirty_       = false;
    uint32_t    save_state_frame_  = 0;
    uint64_t    steady_sample_     = 0;

    // -----------------------------------------------------------------------
    AUInstrument() {
        bank_.init_params();

        vivid::description(plugin_name, "AU instrument name (from list_au_plugins)");
        vivid::display_hint(plugin_name, VIVID_DISPLAY_HIDDEN);
        // Macro float sliders are drawn manually (VIVID_INSPECTOR_FULL_MODE suppresses the
        // auto-rendered controls); MacroBank::init_params hides the knobs and keeps the id
        // strings visible so the snapshot includes them.
        vivid::display_hint(plugin_state,   VIVID_DISPLAY_HIDDEN);  // authored state — persisted
        // Runtime-computed list / scratch input — recomputed live, not saved to disk.
        vivid::transient_param(au_params_);
        vivid::transient_param(direct_params_);
    }

    ~AUInstrument() {
        // Best-effort cleanup. Audio thread must be stopped before operator destroy.
        auto* act  = active_.exchange(nullptr,  std::memory_order_acq_rel);
        auto* pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
        auto* dead = dying_.exchange(nullptr,   std::memory_order_acq_rel);
        auto* dead2= dying2_.exchange(nullptr,  std::memory_order_acq_rel);
        delete act; delete pend; delete dead; delete dead2;
    }

    // -----------------------------------------------------------------------
    // Main-thread lifecycle
    // -----------------------------------------------------------------------

    void prepare_instance_assets() override {
        au_scan_plugins();
        reload_if_changed();
    }

    void main_thread_update(double /*time*/) override {
        // Dispose plugins evicted by the audio thread
        auto* dead  = dying_.exchange(nullptr,  std::memory_order_acq_rel);
        auto* dead2 = dying2_.exchange(nullptr, std::memory_order_acq_rel);
        delete dead;
        delete dead2;

        // Reload at the correct sample rate if the audio thread observed a mismatch.
        // AU has no performEdit/mark_dirty callbacks, so we also use a periodic
        // save_state() fallback (~1 min) to capture GUI-driven parameter changes.
        {
            uint32_t seen = audio_rate_seen_.load(std::memory_order_acquire);
            if (seen > 0 && seen != loaded_rate_ && !last_name_.empty()) {
                sample_rate_ = seen;
                reload_for_rate_change();
            }
        }

        reload_if_changed();
        update_macro_map();
        refresh_au_params_json();
        process_direct_params();

        if (!state_dirty_ && ++save_state_frame_ < 3600) {
            // skip
        } else {
            save_state_frame_ = 0;
            save_state();
        }
    }

    void reload_if_changed() {
        if (plugin_name.str_value == last_name_) return;
        last_name_ = plugin_name.str_value;

        // Reset macro map — will be resolved after new plugin loads
        bank_.reset();
        direct_params_.str_value.clear();
        last_direct_params_.clear();

        if (last_name_.empty()) {
            auto* old = pending_.exchange(nullptr, std::memory_order_acq_rel);
            delete old;
            pending_.store(new AUHandle(), std::memory_order_release); // sentinel: au == nullptr
            return;
        }

        AUHandle* h = au_load_plugin(last_name_, sample_rate_, &transport_state_,
                                      plugin_state.str_value);
        if (!h) {
            fprintf(stderr, "[AUInstrument] failed to load plugin '%s'\n", last_name_.c_str());
            return;
        }

        loaded_rate_ = sample_rate_;
        auto* old = pending_.exchange(h, std::memory_order_acq_rel);
        delete old;
        state_dirty_ = true;
    }

    void reload_for_rate_change() {
        if (last_name_.empty()) return;
        AUHandle* h = au_load_plugin(last_name_, sample_rate_, &transport_state_,
                                      plugin_state.str_value);
        if (!h) return;
        loaded_rate_ = sample_rate_;
        auto* old = pending_.exchange(h, std::memory_order_acq_rel);
        delete old;
        state_dirty_ = true;
    }

    void update_macro_map() {
        auto* act = active_.load(std::memory_order_acquire);
        const bool have_params = act && act->au && !act->params.empty();
        bank_.update([&](const std::string& name, AudioUnitParameterID& out_id, auto& e) {
            if (!have_params) return false;
            for (const auto& p : act->params) {
                if (name == p.name) {
                    // Write min/max/units before id so the audio thread's acquire-load
                    // on id also acquires visibility of min/max/units.
                    e.min_val = p.min_val;
                    e.max_val = p.max_val;
                    std::strncpy(e.units, p.units, 31);
                    e.units[31] = '\0';
                    out_id = p.id;
                    return true;
                }
            }
            return false;
        });
    }

    void process_direct_params() {
        if (direct_params_.str_value == last_direct_params_) return;
        last_direct_params_ = direct_params_.str_value;
        direct_param_queue_parse_and_push(direct_q_, direct_params_.str_value);
    }

    void refresh_au_params_json() {
        auto* act = active_.load(std::memory_order_acquire);
        std::string json = au_params_to_json(act);
        if (json != au_params_.str_value)
            au_params_.str_value = std::move(json);
    }

    // -----------------------------------------------------------------------
    // State save / load (main thread only)
    // -----------------------------------------------------------------------

    void save_state() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || !act->au || act->comp_name != last_name_) return;
        std::string b64 = au_save_state(act);
        if (!b64.empty()) {
            plugin_state.str_value = std::move(b64);
            state_dirty_ = false;
        }
    }

    // -----------------------------------------------------------------------
    // Audio thread
    // -----------------------------------------------------------------------

    void process_audio(const VividAudioContext* ctx) override {
        sample_rate_ = ctx->sample_rate;
        audio_rate_seen_.store(ctx->sample_rate, std::memory_order_relaxed);

        // Update transport state before render (callbacks read these during AudioUnitRender)
        const double bpm   = ctx->metronome_bpm > 0.f ? ctx->metronome_bpm : 120.0;
        const double beats = ctx->metronome_beats_elapsed;
        const uint32_t bpb = ctx->metronome_beats_per_bar > 0 ? ctx->metronome_beats_per_bar : 4;
        transport_state_.bpm  = bpm;
        transport_state_.beat = beats;
        transport_state_.bpb  = bpb;

        // Check for pending plugin swap
        AUHandle* pend = pending_.load(std::memory_order_acquire);
        if (pend) {
            pend = pending_.exchange(nullptr, std::memory_order_acq_rel);
            if (pend) {
                AUHandle* old = active_.exchange(pend, std::memory_order_acq_rel);
                if (old) {
                    auto* prev = dying_.exchange(old, std::memory_order_acq_rel);
                    if (prev) dying2_.store(prev, std::memory_order_release);
                }
                if (!pend->au) {
                    // Sentinel: no plugin loaded
                    active_.store(nullptr, std::memory_order_release);
                    delete pend;
                    zero_outputs(ctx);
                    return;
                }
            }
        }

        AUHandle* act = active_.load(std::memory_order_acquire);
        if (!act || !act->au) { zero_outputs(ctx); return; }

        dispatch_notes(ctx, act);
        send_macro_params(act);

        // Build AudioBufferList for stereo non-interleaved output
        // Stack-allocate with room for 2 AudioBuffer entries
        char abl_mem[sizeof(AudioBufferList) + sizeof(AudioBuffer)];
        AudioBufferList* abl = reinterpret_cast<AudioBufferList*>(abl_mem);
        abl->mNumberBuffers            = 2;
        abl->mBuffers[0].mNumberChannels = 1;
        abl->mBuffers[0].mDataByteSize   = ctx->buffer_size * sizeof(float);
        abl->mBuffers[0].mData           = ctx->output_buffers[0];
        abl->mBuffers[1].mNumberChannels = 1;
        abl->mBuffers[1].mDataByteSize   = ctx->buffer_size * sizeof(float);
        abl->mBuffers[1].mData           = ctx->output_buffers[0] + ctx->buffer_size;

        // Zero output before render — some plugins write partial buffers
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);

        AudioTimeStamp ts = {};
        ts.mFlags      = kAudioTimeStampSampleTimeValid;
        ts.mSampleTime = static_cast<Float64>(steady_sample_);

        AudioUnitRenderActionFlags action_flags = 0;
        AudioUnitRender(act->au, &action_flags, &ts, 0,
                        static_cast<UInt32>(ctx->buffer_size), abl);

        steady_sample_ += ctx->buffer_size;
    }

    void dispatch_notes(const VividAudioContext* ctx, AUHandle* act) {
        if (!ctx->custom_inputs || ctx->custom_input_count == 0 || !ctx->custom_inputs[0])
            return;
        const auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        for (uint32_t i = 0; i < notes->count; ++i) {
            const auto& ev = notes->events[i];
            UInt32 offset = static_cast<UInt32>(
                std::min(static_cast<uint64_t>(ev.frame_offset_samples),
                         static_cast<uint64_t>(ctx->buffer_size - 1)));

            if (ev.type == VIVID_NOTE_ON) {
                UInt32 vel = static_cast<UInt32>(std::clamp(ev.value * 127.f, 0.f, 127.f));
                MusicDeviceMIDIEvent(act->au, 0x90, ev.note_number, vel, offset);

            } else if (ev.type == VIVID_NOTE_OFF) {
                UInt32 note = (ev.note_number != 255) ? ev.note_number : 0;
                MusicDeviceMIDIEvent(act->au, 0x80, note, 0, offset);

            } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                // ev.value is in semitones. Map ±12 semitones to 14-bit pitch bend (0..16383).
                // Center = 8192. Actual range depends on plugin's pitch bend range setting.
                float norm = std::clamp(ev.value / 12.0f, -1.0f, 1.0f);
                int bend = static_cast<int>(norm * 8191.f) + 8192;
                bend = std::clamp(bend, 0, 16383);
                MusicDeviceMIDIEvent(act->au, 0xE0,
                                     static_cast<UInt32>(bend & 0x7F),
                                     static_cast<UInt32>((bend >> 7) & 0x7F), offset);

            } else if (ev.type == VIVID_NOTE_PRESSURE) {
                UInt32 pressure = static_cast<UInt32>(std::clamp(ev.value * 127.f, 0.f, 127.f));
                // Channel aftertouch — affects all simultaneous notes (AU v2 limitation)
                MusicDeviceMIDIEvent(act->au, 0xD0, pressure, 0, offset);
            }
            // VIVID_NOTE_TIMBRE: no direct AU v2 mapping
        }
    }

    void send_macro_params(AUHandle* act) {
        bank_.emit_changes([&](AudioUnitParameterID id, float v, const auto& e) {
            float scaled = static_cast<float>(
                e.min_val + static_cast<double>(v) * (e.max_val - e.min_val));
            AudioUnitSetParameter(act->au, id, kAudioUnitScope_Global, 0, scaled, 0);
        });

        // Drain direct-param queue (MCP-sourced, normalized → native)
        {
            DirectParamQueue::Entry entry;
            while (direct_q_.pop(entry)) {
                float native = static_cast<float>(entry.val);
                for (const auto& p : act->params)
                    if (p.id == entry.id) {
                        native = p.min_val + static_cast<float>(entry.val) * (p.max_val - p.min_val);
                        break;
                    }
                AudioUnitSetParameter(act->au, entry.id,
                                       kAudioUnitScope_Global, 0, native, 0);
            }
        }
    }

    static void zero_outputs(const VividAudioContext* ctx) {
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);
    }

    // -----------------------------------------------------------------------
    // Inspector (VIVID_INSPECTOR) — plugin picker dropdown.
    // AU v2 GUI is not supported; no "Open Plugin UI" button.
    // -----------------------------------------------------------------------

    void draw_inspector(VividInspectorContext* ctx) override {
        using namespace vivid::plugin_ui;
        using namespace vivid::draw_ui;

        const auto& plugins = au_get_plugins();

        // Build param name list from active handle (fallback to pending)
        {
            auto* h = active_.load(std::memory_order_acquire);
            if (!h || !h->au || h->params.empty()) h = pending_.load(std::memory_order_acquire);
            param_names_cache_.clear();
            if (h && h->au) {
                param_names_cache_.reserve(h->params.size());
                for (const auto& p : h->params)
                    param_names_cache_.emplace_back(p.name);
            }
        }

        std::vector<std::string> names;
        names.reserve(plugins.size());
        for (const auto& p : plugins) names.push_back(p.name);

        int cur = -1;
        for (int i = 0; i < (int)plugins.size(); ++i) {
            if (plugins[i].name == last_name_) { cur = i; break; }
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
        if (sel >= 0)
            ctx->commands.set_string_param(ctx->commands.opaque,
                                           "plugin_name", plugins[sel].name.c_str());

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
        for (int i = 0; i < kMaxMacros; ++i) {
            if (bank_.slots_[i].id.str_value.empty()) { any_empty = true; continue; }

            const float row_y = y;
            float val = bank_.slots_[i].knob.value;
            const auto& entry = bank_.entries_[i];

            // Native value + units (AU: linear rescale; units from cached entry)
            float native = val;
            if (entry.id.load(std::memory_order_acquire) != kAUInvalidParamID)
                native = static_cast<float>(entry.min_val + val * (entry.max_val - entry.min_val));
            char val_buf[40];
            if (entry.units[0])
                std::snprintf(val_buf, sizeof(val_buf), "%.3g %s", native, entry.units);
            else
                std::snprintf(val_buf, sizeof(val_buf), "%.3g", native);

            // Param name — clipped
            if (d.push_clip_rect) d.push_clip_rect(o, x, row_y, nam_w, kRowH);
            if (d.draw_text)      d.draw_text(o, x, row_y + (kRowH - lh) * 0.5f,
                                              bank_.slots_[i].id.str_value.c_str(), th.bright_text, 0.85f);
            if (d.pop_clip_rect)  d.pop_clip_rect(o);

            // Slider
            bool over_sli = m.x >= sli_x && m.x <= sli_x + sli_w
                         && m.y >= row_y  && m.y <= row_y + kRowH;
            if (m.left_clicked && over_sli) drag_macro_ = i;
            if (drag_macro_ == i && m.left_down) {
                val = std::clamp((m.x - sli_x) / sli_w, 0.f, 1.f);
                ctx->commands.set_param(ctx->commands.opaque, bank_.slots_[i].knob.name, val);
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
                    float typed = text_field_.parsed_value(native);
                    float range = static_cast<float>(entry.max_val - entry.min_val);
                    float norm  = range > 1e-6f
                                ? (typed - static_cast<float>(entry.min_val)) / range
                                : 0.f;
                    norm = std::clamp(norm, 0.f, 1.f);
                    ctx->commands.set_param(ctx->commands.opaque, bank_.slots_[i].knob.name, norm);
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
                ctx->commands.set_string_param(ctx->commands.opaque, bank_.slots_[i].id.name, "");
                ctx->commands.set_param(ctx->commands.opaque, bank_.slots_[i].knob.name, 0.f);
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
                int slot = bank_.first_empty();
                if (slot >= 0)
                    ctx->commands.set_string_param(ctx->commands.opaque,
                                                   bank_.slots_[slot].id.name,
                                                   param_names_cache_[picked].c_str());
            }
        }

        ctx->consumed_height = y + 4.f - ctx->content_y;
    }
};

VIVID_DEFINE_OP(AUInstrument) {
    display_name = "AU Instrument";
    keywords     = {"plugin", "au", "audio unit", "synth", "instrument", "external"};
    summary      = "Hosts an AU (Audio Units) instrument plugin; receives MIDI notes, outputs audio.";
}

VIVID_INSPECTOR_FULL_MODE(AUInstrument)

#endif // __APPLE__
