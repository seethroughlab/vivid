#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"

#ifdef __APPLE__
#include "shared/au_host/au_host_common.h"
#include "shared/au_host/au_scanner.h"
#include <atomic>
#include <cmath>

// ---------------------------------------------------------------------------
// AUInstrument — hosts an AU v2 MusicDevice plugin as a Vivid audio operator.
//
// Parameters visible in the inspector:
//   plugin_name  — display name from AudioComponentCopyName (e.g. "Surge XT")
//   macro_0..7   — float 0-1, each mapped to an AU param by name via macro_0_id..7_id
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

    // Opaque plugin state as base64-encoded CFPropertyList XML; hidden from inspector.
    vivid::Param<vivid::TextValue> plugin_state {"plugin_state"};

    // JSON array of AU parameters from the loaded plugin; hidden, consumed by list_au_params.
    vivid::Param<vivid::TextValue> au_params_ {"_au_params"};

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
        out.push_back(&au_params_);
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

    // --- State ---
    std::string last_name_;
    uint32_t    sample_rate_ = 48000;
    bool        state_dirty_ = false;
    uint64_t    steady_sample_ = 0;

    // --- Per-macro resolved AU param ID & range cache ---
    struct MacroEntry {
        AudioUnitParameterID id        = kAUInvalidParamID;
        float                min_val   = 0.f;
        float                max_val   = 1.f;
        float                last_sent = -1.f;
    };
    MacroEntry macro_map_[8];

    vivid::Param<float>*            macro_float_[8];
    vivid::Param<vivid::TextValue>* macro_id_[8];

    // -----------------------------------------------------------------------
    AUInstrument() {
        macro_float_[0] = &macro_0; macro_id_[0] = &macro_0_id;
        macro_float_[1] = &macro_1; macro_id_[1] = &macro_1_id;
        macro_float_[2] = &macro_2; macro_id_[2] = &macro_2_id;
        macro_float_[3] = &macro_3; macro_id_[3] = &macro_3_id;
        macro_float_[4] = &macro_4; macro_id_[4] = &macro_4_id;
        macro_float_[5] = &macro_5; macro_id_[5] = &macro_5_id;
        macro_float_[6] = &macro_6; macro_id_[6] = &macro_6_id;
        macro_float_[7] = &macro_7; macro_id_[7] = &macro_7_id;

        vivid::description(plugin_name, "AU instrument name (from list_au_plugins)");
        vivid::description(macro_0, "Macro 0 value (0-1), mapped to the AU param named in macro_0_id");
        vivid::description(macro_0_id, "AU parameter name for macro 0");
        vivid::description(macro_1, "Macro 1 value (0-1)");
        vivid::description(macro_1_id, "AU parameter name for macro 1");
        vivid::description(macro_2, "Macro 2 value (0-1)");
        vivid::description(macro_2_id, "AU parameter name for macro 2");
        vivid::description(macro_3, "Macro 3 value (0-1)");
        vivid::description(macro_3_id, "AU parameter name for macro 3");
        vivid::description(macro_4, "Macro 4 value (0-1)");
        vivid::description(macro_4_id, "AU parameter name for macro 4");
        vivid::description(macro_5, "Macro 5 value (0-1)");
        vivid::description(macro_5_id, "AU parameter name for macro 5");
        vivid::description(macro_6, "Macro 6 value (0-1)");
        vivid::description(macro_6_id, "AU parameter name for macro 6");
        vivid::description(macro_7, "Macro 7 value (0-1)");
        vivid::description(macro_7_id, "AU parameter name for macro 7");
        vivid::display_hint(plugin_state, VIVID_DISPLAY_HIDDEN);
        vivid::display_hint(au_params_,   VIVID_DISPLAY_HIDDEN);

        for (auto& m : macro_map_) { m.id = kAUInvalidParamID; m.last_sent = -1.f; }
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

        reload_if_changed();
        update_macro_map();
        refresh_au_params_json();

        if (state_dirty_) save_state();
    }

    void reload_if_changed() {
        if (plugin_name.str_value == last_name_) return;
        last_name_ = plugin_name.str_value;

        // Reset macro map — will be resolved after new plugin loads
        for (auto& m : macro_map_) { m.id = kAUInvalidParamID; m.last_sent = -1.f; }

        if (last_name_.empty()) {
            auto* old = pending_.exchange(nullptr, std::memory_order_acq_rel);
            delete old;
            pending_.store(new AUHandle(), std::memory_order_release); // sentinel: au == nullptr
            return;
        }

        AUHandle* h = au_load_plugin(last_name_, sample_rate_, &transport_state_,
                                      plugin_state.str_value);
        if (!h) return;

        auto* old = pending_.exchange(h, std::memory_order_acq_rel);
        delete old;
        state_dirty_ = true;
    }

    void update_macro_map() {
        auto* act = active_.load(std::memory_order_acquire);
        if (!act || !act->au || act->params.empty()) return;

        for (int i = 0; i < 8; ++i) {
            const std::string& name = macro_id_[i]->str_value;
            if (name.empty())                               { macro_map_[i].id = kAUInvalidParamID; continue; }
            if (macro_map_[i].id != kAUInvalidParamID)     continue; // already resolved

            for (const auto& p : act->params) {
                if (name == p.name) {
                    macro_map_[i] = { p.id, p.min_val, p.max_val, -1.f };
                    break;
                }
            }
        }
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
        const float vals[8] = {
            macro_0.value, macro_1.value, macro_2.value, macro_3.value,
            macro_4.value, macro_5.value, macro_6.value, macro_7.value,
        };
        for (int i = 0; i < 8; ++i) {
            if (macro_map_[i].id == kAUInvalidParamID) continue;
            if (std::fabs(vals[i] - macro_map_[i].last_sent) < 1e-6f) continue;
            macro_map_[i].last_sent = vals[i];

            float scaled = macro_map_[i].min_val +
                vals[i] * (macro_map_[i].max_val - macro_map_[i].min_val);
            AudioUnitSetParameter(act->au, macro_map_[i].id,
                                   kAudioUnitScope_Global, 0, scaled, 0);
        }
    }

    static void zero_outputs(const VividAudioContext* ctx) {
        std::memset(ctx->output_buffers[0], 0, sizeof(float) * ctx->buffer_size * 2);
    }
};

VIVID_DEFINE_OP(AUInstrument) {
    display_name = "AU Instrument";
    keywords     = {"plugin", "au", "audio unit", "synth", "instrument", "external"};
    summary      = "Hosts an AU (Audio Units) instrument plugin; receives MIDI notes, outputs audio.";
}

#endif // __APPLE__
