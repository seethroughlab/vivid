#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"
#include <cmath>
#include <algorithm>

// Pentatonic intervals within one octave (always consonant)
static constexpr int kPentatonic[] = {0, 2, 4, 7, 9};
static constexpr int kPentatonicSize = 5;

static float midi_to_freq(float note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

// Quantize a continuous semitone offset to the nearest pentatonic degree
static float quantize_pentatonic(float semitones, float base_note) {
    int octave = static_cast<int>(std::floor(semitones / 12.0f));
    float within = semitones - octave * 12.0f;

    // Find closest pentatonic interval
    int best = 0;
    float best_dist = 100.0f;
    for (int i = 0; i < kPentatonicSize; ++i) {
        float dist = std::fabs(within - kPentatonic[i]);
        if (dist < best_dist) {
            best_dist = dist;
            best = kPentatonic[i];
        }
    }
    return base_note + octave * 12.0f + best;
}

struct PlexusSynth : vivid::OperatorBase {
    static constexpr const char* kName   = "PlexusSynth";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> volume    {"volume",     0.3f, 0.0f, 1.0f};
    vivid::Param<int>   base_note {"base_note",  48,   24,   84};
    vivid::Param<int>   note_range{"note_range", 36,   12,   60};
    vivid::Param<int>   waveform  {"waveform",   0, {"sine", "triangle"}};
    vivid::Param<float> attack    {"attack",     0.1f,  0.01f, 1.0f};
    vivid::Param<float> release   {"release",    0.3f,  0.01f, 2.0f};

    static constexpr int kMaxVoices = 256;

    struct Voice {
        double phase      = 0.0;
        float  amplitude  = 0.0f;
        float  target_amp = 0.0f;
        float  freq       = 440.0f;
    };

    Voice voices_[kMaxVoices];
    int   active_count_ = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&volume);
        out.push_back(&base_note);
        out.push_back(&note_range);
        out.push_back(&waveform);
        out.push_back(&attack);
        out.push_back(&release);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"positions",   VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"connections", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"output",      VIVID_PORT_AUDIO_FLOAT,    VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out = audio->output_buffers[0];
        double sr = static_cast<double>(audio->sample_rate);
        float vol = volume.value;
        int wave = waveform.int_value();
        // Map waveform choice: 0=sine, 1=triangle (type 3 in audio_dsp)
        int wave_type = (wave == 1) ? 3 : 0;
        float base = static_cast<float>(base_note.int_value());
        float range = static_cast<float>(note_range.int_value());
        float atk_time = attack.value;
        float rel_time = release.value;

        // Read spread inputs at block rate
        const float* pos_data = nullptr;
        uint32_t pos_len = 0;
        const float* conn_data = nullptr;
        uint32_t conn_len = 0;

        if (ctx->input_spreads) {
            if (ctx->input_spreads[0].length > 0) {
                pos_data = ctx->input_spreads[0].data;
                pos_len  = ctx->input_spreads[0].length;
            }
            if (ctx->input_spreads[1].length > 0) {
                conn_data = ctx->input_spreads[1].data;
                conn_len  = ctx->input_spreads[1].length;
            }
        }

        // Determine particle count from positions spread (pairs of x,y)
        int new_count = static_cast<int>(pos_len / 2);
        if (new_count > kMaxVoices) new_count = kMaxVoices;

        // Handle particle count changes
        if (new_count > active_count_) {
            // New voices start silent with phase=0
            for (int i = active_count_; i < new_count; ++i) {
                voices_[i].phase = 0.0;
                voices_[i].amplitude = 0.0f;
                voices_[i].target_amp = 0.0f;
            }
        }
        active_count_ = new_count;

        // Update voice targets from spread data
        for (int i = 0; i < active_count_; ++i) {
            float y = pos_data[i * 2 + 1];  // Y position (0–1)
            float semitones = y * range;
            float note = quantize_pentatonic(semitones, base);
            voices_[i].freq = midi_to_freq(note);

            // Connection count → target amplitude
            float conn = (conn_data && i < static_cast<int>(conn_len))
                         ? conn_data[i] : 0.0f;
            voices_[i].target_amp = conn;
        }

        // Envelope smoothing coefficients (one-pole filter)
        float atk_coeff = 1.0f - std::exp(-1.0f / (atk_time * static_cast<float>(sr)));
        float rel_coeff = 1.0f - std::exp(-1.0f / (rel_time * static_cast<float>(sr)));

        // Normalization factor: 1/sqrt(N) to keep volume stable
        float norm = (active_count_ > 0)
                     ? 1.0f / std::sqrt(static_cast<float>(active_count_))
                     : 0.0f;

        for (uint32_t s = 0; s < audio->buffer_size; ++s) {
            float mix = 0.0f;

            for (int i = 0; i < active_count_; ++i) {
                // Smooth amplitude toward target
                float target = voices_[i].target_amp;
                float coeff = (target > voices_[i].amplitude) ? atk_coeff : rel_coeff;
                voices_[i].amplitude += (target - voices_[i].amplitude) * coeff;

                // Generate sample
                float sample = static_cast<float>(
                    audio_dsp::waveform(voices_[i].phase, wave_type));
                mix += sample * voices_[i].amplitude;

                // Advance phase
                voices_[i].phase += voices_[i].freq / sr;
                if (voices_[i].phase >= 1.0) voices_[i].phase -= 1.0;
            }

            out[s] = mix * vol * norm;
        }
    }
};

VIVID_REGISTER(PlexusSynth)
