#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "operator_api/value_view.h"
#include "sample_bank.h"
#include "voice.h"
#include "voice_breakouts.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace vivid_sampler;

struct SlicerData {
    std::shared_ptr<SampleData> sample;
};

/**
 * @brief Sample slicer dividing audio into N equal segments triggered by note.
 *
 * Loads an audio file and divides it into equal slices. Each slice maps
 * to a MIDI note starting from note 36 (C2). Supports one-shot, loop,
 * and gate play modes with per-voice ADSR.
 *
 * @param slices Number of equal divisions of the sample.
 * @param mode one_shot plays once, loop repeats the slice, gate sustains while held.
 * @see Sampler, SP404, DrumSequencer
 */
struct Slicer : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "Slicer";
    static constexpr bool kTimeDependent = false;
    static constexpr int kMaxVoices = 16;
    static constexpr int kBaseNote = 36;

    vivid::Param<vivid::FilePath> file    {"file"};
    vivid::Param<int>   slices  {"slices",  16, 2, 64};
    vivid::Param<int>   mode    {"mode",    0, {"one_shot", "loop", "gate"}};
    vivid::Param<float> attack  {"attack",  0.001f, 0.001f, 2.0f};
    vivid::Param<float> decay   {"decay",   0.1f,   0.01f,  2.0f};
    vivid::Param<float> sustain {"sustain", 1.0f,   0.0f,   1.0f};
    vivid::Param<float> release {"release", 0.05f,  0.001f, 10.0f};
    vivid::Param<float> volume  {"volume",  1.0f,   0.0f,   2.0f};
    // Per-note expression depth (Phase 5).
    vivid::Param<float> pressure_to_amp {"pressure_to_amp", 0.5f,  0.0f, 1.0f};
    vivid::Param<float> timbre_to_pitch {"timbre_to_pitch", 12.0f, -24.0f, 24.0f};

    Voice voices_[kMaxVoices];
    uint32_t voice_slice_start_[kMaxVoices] = {};
    uint32_t voice_slice_end_[kMaxVoices] = {};
    std::atomic<SlicerData*> data_{nullptr};
    SlicerData* deferred_delete_ = nullptr;
    std::string last_path_;
    uint64_t frame_counter_ = 0;
    int last_slices_ = -1;

    Slicer() {
        vivid::description(file, "Audio file to slice");
        vivid::description(slices, "Number of equal slices to divide the sample into");
        vivid::description(mode, "Playback mode: one_shot plays once, loop repeats, gate sustains while held");
        vivid::description(attack, "Envelope attack time in seconds");
        vivid::description(decay, "Envelope decay time in seconds");
        vivid::description(sustain, "Envelope sustain level (0-1)");
        vivid::description(release, "Envelope release time in seconds");
        vivid::description(volume, "Master output volume, can boost up to 2x");
    }

    ~Slicer() {
        delete data_.load(std::memory_order_relaxed);
        delete deferred_delete_;
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&slices);
        out.push_back(&mode);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&volume);
        out.push_back(&pressure_to_amp);
        out.push_back(&timbre_to_pitch);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        // Per-voice advanced breakouts (kMaxVoices channels mono each).
        out.push_back({"voices_out",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr,
                       16, 0.0f}); // kMaxVoices channels
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_ids",        .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_gates",      .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_velocities", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_freqs",      .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        vivid::append_analysis_ports(out);
    }

    void prepare_instance_assets() override {
        refresh_sample_data();
    }

    void main_thread_update(double /*time*/) override {
        refresh_sample_data();
    }

    void process_audio(const VividAudioContext* ctx) override {
        SlicerData* d = data_.load(std::memory_order_acquire);
        if (!d || !d->sample || d->sample->samples_L.empty()) {
            for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
                ctx->output_buffers[0][i] = 0.0f;
                ctx->output_buffers[0][ctx->buffer_size + i] = 0.0f;
            }
            return;
        }

        const auto& sample = *d->sample;
        uint32_t total_frames = static_cast<uint32_t>(sample.samples_L.size());

        // Read params
        int   p_slices  = std::clamp(slices.int_value(), 2, 64);
        int   p_mode    = mode.int_value();
        float p_attack  = attack.value;
        float p_decay   = decay.value;
        float p_sustain = sustain.value;
        float p_release = release.value;
        float p_volume  = volume.value;
        const float p_amp_depth   = pressure_to_amp.value;
        const float t_pitch_semis = timbre_to_pitch.value;
        float dt        = 1.0f / static_cast<float>(ctx->sample_rate);

        // Slice boundary calculation
        uint32_t slice_len = total_frames / static_cast<uint32_t>(p_slices);

        // If slices param changed, deactivate all voices (boundaries shifted)
        if (p_slices != last_slices_) {
            if (last_slices_ > 0) {
                for (int v = 0; v < kMaxVoices; ++v)
                    voices_[v].active = false;
            }
            last_slices_ = p_slices;
        }

        // Playback rate: sample_sr / runtime_sr
        double playback_rate = static_cast<double>(sample.sample_rate) /
                               static_cast<double>(ctx->sample_rate);

        // Process native note input. Voice slot lookup is by note_id.
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
            for (uint32_t m = 0; m < notes->count; ++m) {
                const auto& ev = notes->events[m];
                if (ev.note_id == 0) continue;

                if (ev.type == VIVID_NOTE_ON) {
                    int note = ev.note_number;
                    float vel = ev.value;

                    int slice_index = std::clamp(note - kBaseNote, 0, p_slices - 1);
                    uint32_t s_start = static_cast<uint32_t>(slice_index) * slice_len;
                    uint32_t s_end = s_start + slice_len;
                    if (s_end > total_frames) s_end = total_frames;

                    int vi = -1;
                    for (int j = 0; j < kMaxVoices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            vi = j; break;
                        }
                    }
                    if (vi < 0) vi = find_free_voice(voices_, kMaxVoices);
                    if (vi < 0) vi = steal_oldest_voice(voices_, kMaxVoices);

                    voices_[vi].active = true;
                    voices_[vi].note = note;
                    voices_[vi].note_id = ev.note_id;
                    voices_[vi].velocity = vel;
                    voices_[vi].region = nullptr;
                    voices_[vi].playback_rate = playback_rate;
                    voices_[vi].playback_pos = static_cast<double>(s_start);
                    voices_[vi].one_shot = (p_mode == 0);
                    voices_[vi].start_frame = frame_counter_;
                    voices_[vi].pitch_bend_semis = 0.0f;
                    voices_[vi].pressure         = 0.0f;
                    voices_[vi].timbre           = 0.0f;
                    vivid::adsr::gate_on(voices_[vi].envelope);

                    voice_slice_start_[vi] = s_start;
                    voice_slice_end_[vi] = s_end;
                } else if (ev.type == VIVID_NOTE_OFF) {
                    for (int j = 0; j < kMaxVoices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voice_note_off(voices_[j]);
                            break;
                        }
                    }
                } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                    for (int j = 0; j < kMaxVoices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voices_[j].pitch_bend_semis = ev.value;
                            break;
                        }
                    }
                }
                // Slicer doesn't currently route pressure or timbre — ignore.
            }
        }

        // Build active-voice ordering for the breakout surface.
        int slot_to_pos[kMaxVoices];
        int sorted[kMaxVoices];
        int active_count = 0;
        for (int v = 0; v < kMaxVoices; ++v) {
            slot_to_pos[v] = -1;
            if (voices_[v].active) sorted[active_count++] = v;
        }
        std::sort(sorted, sorted + active_count,
                  [this](int a, int b) {
                      return voices_[a].note_id < voices_[b].note_id;
                  });
        for (int i = 0; i < active_count; ++i) slot_to_pos[sorted[i]] = i;

        const uint32_t frames = ctx->buffer_size;
        float* voices_out_buf = (ctx->output_buffers && ctx->output_buffers[1])
                                ? ctx->output_buffers[1] : nullptr;
        if (voices_out_buf) {
            std::memset(voices_out_buf, 0,
                        static_cast<size_t>(kMaxVoices) * frames * sizeof(float));
        }

        // Render audio
        for (uint32_t s = 0; s < frames; ++s) {
            float out_L = 0.0f;
            float out_R = 0.0f;

            for (int v = 0; v < kMaxVoices; ++v) {
                if (!voices_[v].active) continue;

                uint32_t s_start = voice_slice_start_[v];
                uint32_t s_end   = voice_slice_end_[v];

                // Bounds check — past end of slice?
                if (voices_[v].playback_pos >= static_cast<double>(s_end)) {
                    if (p_mode == 1) {
                        // Loop: wrap back to slice start
                        double slice_length = static_cast<double>(s_end - s_start);
                        voices_[v].playback_pos = s_start +
                            std::fmod(voices_[v].playback_pos - s_start, slice_length);
                    } else {
                        // one_shot or gate: deactivate
                        voices_[v].active = false;
                        continue;
                    }
                }

                // Linear interpolation
                size_t idx = static_cast<size_t>(voices_[v].playback_pos);
                float frac = static_cast<float>(voices_[v].playback_pos - static_cast<double>(idx));

                size_t idx_next = idx + 1;
                if (idx_next >= s_end) {
                    if (p_mode == 1) {
                        idx_next = s_start;  // wrap for interpolation
                    } else {
                        idx_next = idx;  // clamp at end
                    }
                }

                // Safety clamp to sample bounds
                if (idx >= total_frames) { voices_[v].active = false; continue; }
                if (idx_next >= total_frames) idx_next = total_frames - 1;

                float samp_L = sample.samples_L[idx] * (1.0f - frac) +
                               sample.samples_L[idx_next] * frac;
                float samp_R;
                if (sample.stereo) {
                    samp_R = sample.samples_R[idx] * (1.0f - frac) +
                             sample.samples_R[idx_next] * frac;
                } else {
                    samp_R = samp_L;
                }

                // Apply velocity gain. Per-note pressure scales gain (Phase 5).
                const float pressure_scale = 1.0f + p_amp_depth * voices_[v].pressure;
                float gain = voices_[v].velocity * pressure_scale;
                samp_L *= gain;
                samp_R *= gain;

                // Advance ADSR and apply envelope
                vivid::adsr::advance(voices_[v].envelope, dt,
                                     p_attack, p_decay, p_sustain, p_release);
                samp_L *= voices_[v].envelope.env_value;
                samp_R *= voices_[v].envelope.env_value;

                // Advance playback position (pitch bend + per-note timbre
                // both scale the rate; timbre gives ±t_pitch_semis at full).
                const double rate_mult = std::pow(2.0,
                    (static_cast<double>(voices_[v].pitch_bend_semis) +
                     static_cast<double>(t_pitch_semis * voices_[v].timbre)) / 12.0);
                voices_[v].playback_pos += voices_[v].playback_rate * rate_mult;

                // Deactivate if envelope reached IDLE
                if (voices_[v].envelope.stage == vivid::adsr::IDLE) {
                    voices_[v].active = false;
                }

                out_L += samp_L;
                out_R += samp_R;

                // Mirror to voices_out as a mono mix (L+R / 2 * p_volume).
                if (voices_out_buf && slot_to_pos[v] >= 0) {
                    const int pos = slot_to_pos[v];
                    voices_out_buf[pos * frames + s] = (samp_L + samp_R) * 0.5f * p_volume;
                }
            }

            out_L *= p_volume;
            out_R *= p_volume;

            ctx->output_buffers[0][s]                      = out_L;
            ctx->output_buffers[0][frames + s]             = out_R;

            frame_counter_++;
        }

        // Emit voice_*/voices_out aligned to active-note-by-note_id order.
        // ctx->value_outputs[] is indexed by overall OUTPUT port position.
        // Output port order: output(0), voices_out(1), voice_ids(2),
        // voice_gates(3), voice_velocities(4), voice_freqs(5).
        // Inlined value-API equivalent of
        // vivid_sequencers::emit_voice_breakouts_from_sorted for the four
        // voice_* lanes (ids/gates/velocities/freqs), preserving byte-identical
        // ordering and counts.
        if (ctx->value_outputs) {
            const uint32_t n = static_cast<uint32_t>(active_count);
            auto emit_lane = [&](int port, auto value_for_slot) {
                float* buf = vivid_value_output_floats(&ctx->value_outputs[port], n);
                if (buf) {
                    for (int i = 0; i < active_count; ++i) {
                        buf[i] = value_for_slot(
                            static_cast<const vivid::VoiceSlot&>(voices_[sorted[i]]));
                    }
                }
                vivid_value_output_commit(&ctx->value_outputs[port], n);
            };
            emit_lane(2, [](const vivid::VoiceSlot& s) {
                return static_cast<float>(s.note_id); });        // voice_ids
            emit_lane(3, [](const vivid::VoiceSlot& s) {
                return s.gate ? 1.0f : 0.0f; });                  // voice_gates
            emit_lane(4, [](const vivid::VoiceSlot& s) {
                return s.velocity; });                            // voice_velocities
            emit_lane(5, [](const vivid::VoiceSlot& s) {
                return vivid_sequencers::voice_freq_hz(s); });    // voice_freqs
        }
    }

    void refresh_sample_data() {
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        const std::string& path = file.str_value;
        if (path == last_path_) return;
        last_path_ = path;

        SlicerData* new_data = nullptr;
        if (!path.empty()) {
            auto sample = decode_wav(path);
            if (sample) {
                new_data = new SlicerData{std::move(sample)};
            }
        }

        SlicerData* old = data_.exchange(new_data, std::memory_order_acq_rel);
        deferred_delete_ = old;
    }
};

static const char* kSlicerDropExts[] = {".wav"};
static const VividFileDropHandlerDescriptor kSlicerFileDrops[] = {{
    "Slice Sample",
    kSlicerDropExts,
    1,
    "file",
    100,
    "Create a Slicer node from a dropped WAV file.",
}};

VIVID_DEFINE_OP(Slicer) {
}

VIVID_FILE_DROP(kSlicerFileDrops)
