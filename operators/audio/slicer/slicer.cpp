#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "sample_bank.h"
#include "voice.h"
#include <atomic>
#include <algorithm>

using namespace vivid_sampler;

struct SlicerData {
    std::shared_ptr<SampleData> sample;
};

struct Slicer : vivid::AudioOperatorBase {
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

    Voice voices_[kMaxVoices];
    uint32_t voice_slice_start_[kMaxVoices] = {};
    uint32_t voice_slice_end_[kMaxVoices] = {};
    GateTracker gate_tracker_;
    std::atomic<SlicerData*> data_{nullptr};
    SlicerData* deferred_delete_ = nullptr;
    std::string last_path_;
    uint64_t frame_counter_ = 0;
    int last_slices_ = -1;

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
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gates",      VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"velocities", VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_in", VIVID_PORT_INPUT, VividMidiBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
    }

    void main_thread_update(double /*time*/) override {
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        const std::string& path = file.str_value;
        if (path == last_path_) return;
        last_path_ = path;

        if (path.empty()) {
            SlicerData* old = data_.exchange(nullptr, std::memory_order_acq_rel);
            deferred_delete_ = old;
            return;
        }

        auto sample = decode_wav(path);
        if (!sample) {
            SlicerData* old = data_.exchange(nullptr, std::memory_order_acq_rel);
            deferred_delete_ = old;
            return;
        }

        auto* new_data = new SlicerData{std::move(sample)};
        SlicerData* old = data_.exchange(new_data, std::memory_order_acq_rel);
        deferred_delete_ = old;
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

        // Read spread inputs
        SpreadInput gates_in = read_spread_input(ctx->input_spreads, 0);
        SpreadInput notes_in = read_spread_input(ctx->input_spreads, 1);
        SpreadInput vels_in  = read_spread_input(ctx->input_spreads, 2);

        // Process MIDI input
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* midi = static_cast<const VividMidiBuffer*>(ctx->custom_inputs[0]);
            for (uint32_t m = 0; m < midi->count; ++m) {
                const auto& msg = midi->messages[m];
                uint8_t status = msg.status & 0xF0;

                if (status == 0x90 && msg.data2 > 0) {
                    int note = msg.data1;
                    float vel = msg.data2 / 127.0f;

                    int slice_index = std::clamp(note - kBaseNote, 0, p_slices - 1);
                    uint32_t s_start = static_cast<uint32_t>(slice_index) * slice_len;
                    uint32_t s_end = s_start + slice_len;
                    if (s_end > total_frames) s_end = total_frames;

                    int vi = -1;
                    for (int j = 0; j < kMaxVoices; ++j) {
                        if (voices_[j].active && voices_[j].note == note) {
                            vi = j; break;
                        }
                    }
                    if (vi < 0) vi = find_free_voice(voices_, kMaxVoices);
                    if (vi < 0) vi = steal_oldest_voice(voices_, kMaxVoices);

                    voices_[vi].active = true;
                    voices_[vi].note = note;
                    voices_[vi].velocity = vel;
                    voices_[vi].region = nullptr;
                    voices_[vi].playback_rate = playback_rate;
                    voices_[vi].playback_pos = static_cast<double>(s_start);
                    voices_[vi].one_shot = (p_mode == 0);
                    voices_[vi].start_frame = frame_counter_;
                    vivid::adsr::gate_on(voices_[vi].envelope);

                    voice_slice_start_[vi] = s_start;
                    voice_slice_end_[vi] = s_end;
                } else if (status == 0x80 || (status == 0x90 && msg.data2 == 0)) {
                    int note = msg.data1;
                    for (int j = 0; j < kMaxVoices; ++j) {
                        if (voices_[j].active && voices_[j].note == note) {
                            voice_note_off(voices_[j]);
                            break;
                        }
                    }
                }
            }
        }

        // Process gate edges
        uint32_t num_slots = std::min(gates_in.length, static_cast<uint32_t>(kMaxVoices));
        for (uint32_t slot = 0; slot < num_slots; ++slot) {
            float gate = gates_in.data[slot];
            int edge = gate_tracker_.detect(static_cast<int>(slot), gate);

            if (edge == 1) {
                // Rising edge — note on
                int note = 60;
                if (slot < notes_in.length && notes_in.data)
                    note = static_cast<int>(notes_in.data[slot]);
                float vel = 1.0f;
                if (slot < vels_in.length && vels_in.data)
                    vel = vels_in.data[slot];

                // Note-to-slice mapping
                int slice_index = std::clamp(note - kBaseNote, 0, p_slices - 1);
                uint32_t slice_start = static_cast<uint32_t>(slice_index) * slice_len;
                uint32_t slice_end = slice_start + slice_len;
                if (slice_end > total_frames) slice_end = total_frames;

                // Find voice: reuse one already playing this note, or allocate
                int vi = -1;
                for (int j = 0; j < kMaxVoices; ++j) {
                    if (voices_[j].active && voices_[j].note == note) {
                        vi = j;
                        break;
                    }
                }
                if (vi < 0) vi = find_free_voice(voices_, kMaxVoices);
                if (vi < 0) vi = steal_oldest_voice(voices_, kMaxVoices);

                // Set up voice — use voice_note_on with a nullptr region,
                // then manually set playback state
                voices_[vi].active = true;
                voices_[vi].note = note;
                voices_[vi].velocity = vel;
                voices_[vi].region = nullptr;
                voices_[vi].playback_rate = playback_rate;
                voices_[vi].playback_pos = static_cast<double>(slice_start);
                voices_[vi].one_shot = (p_mode == 0);
                voices_[vi].start_frame = frame_counter_;
                vivid::adsr::gate_on(voices_[vi].envelope);

                voice_slice_start_[vi] = slice_start;
                voice_slice_end_[vi] = slice_end;
            } else if (edge == -1) {
                // Falling edge — note off
                int note = 60;
                if (slot < notes_in.length && notes_in.data)
                    note = static_cast<int>(notes_in.data[slot]);

                for (int j = 0; j < kMaxVoices; ++j) {
                    if (voices_[j].active && voices_[j].note == note) {
                        voice_note_off(voices_[j]);
                        break;
                    }
                }
            }
        }

        gate_tracker_.update(gates_in.data, gates_in.length);

        // Render audio
        for (uint32_t s = 0; s < ctx->buffer_size; ++s) {
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

                // Apply velocity gain
                float gain = voices_[v].velocity;
                samp_L *= gain;
                samp_R *= gain;

                // Advance ADSR and apply envelope
                vivid::adsr::advance(voices_[v].envelope, dt,
                                     p_attack, p_decay, p_sustain, p_release);
                samp_L *= voices_[v].envelope.env_value;
                samp_R *= voices_[v].envelope.env_value;

                // Advance playback position
                voices_[v].playback_pos += voices_[v].playback_rate;

                // Deactivate if envelope reached IDLE
                if (voices_[v].envelope.stage == vivid::adsr::IDLE) {
                    voices_[v].active = false;
                }

                out_L += samp_L;
                out_R += samp_R;
            }

            out_L *= p_volume;
            out_R *= p_volume;

            ctx->output_buffers[0][s]                      = out_L;
            ctx->output_buffers[0][ctx->buffer_size + s]   = out_R;

            frame_counter_++;
        }
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

VIVID_REGISTER(Slicer)
VIVID_FILE_DROP(kSlicerFileDrops)
