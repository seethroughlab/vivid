#include "audio/builtin_audio_ops.h"
#include "audio/sampler_op.h"        // SamplerLoadable escape hatch
#include "gpu/op_runtime.h"          // OpRegistry / register_op (includes operator_api)
#include "audio/audio_op_runtime.h"  // audio_op_mark_note_op (ADR-0015)
#include "operator_api/movie_audio.h" // the movie-audio bus (MovieAudio source op)
#include "audio/sample_engine/voice.h" // ported sample-playback engine (ADSR/interp/loop/poly)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace vivid {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline VividPortDescriptor aud_in()  { return { "input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT  }; }
inline VividPortDescriptor aud_out() { return { "output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT }; }
}

// --- Bitcrush: bit-depth + sample-rate reduction (an effect spike) --------------------
struct BitcrushOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Bitcrush";
    static constexpr const char* kSummary = "Bit-depth + sample-rate reduction (lo-fi crush).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "effect", "bitcrush" };

    Param<float> bits{ "bits", 8.f, 1.f, 16.f };
    Param<int>   downsample{ "downsample", 1, 1, 64 };
    Param<float> mix{ "mix", 1.f, 0.f, 1.f };

    float hold_[2] = { 0.f, 0.f };
    int   cnt_[2]  = { 0, 0 };

    void collect_params(std::vector<ParamBase*>& o) override {
        semantic_intent(bits, "bit depth");           description(bits, "quantization resolution");
        semantic_intent(downsample, "sample-rate reduction factor");
        semantic_shape(mix, "scalar");                semantic_intent(mix, "dry/wet mix");
        o.push_back(&bits); o.push_back(&downsample); o.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_in()); o.push_back(aud_out()); }

    void process_audio(const VividAudioContext* c) override {
        if (!c->input_buffers || !c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const uint8_t nch = c->input_channel_counts ? c->input_channel_counts[0] : 1;
        const float b   = c->param_values ? c->param_values[0] : bits.value;
        int         ds  = c->param_values ? static_cast<int>(c->param_values[1] + 0.5f) : static_cast<int>(downsample.value);
        if (ds < 1) ds = 1;
        const float mx  = c->param_values ? c->param_values[2] : mix.value;
        const float levels = std::pow(2.f, b < 1.f ? 1.f : b);
        for (uint8_t ch = 0; ch < nch && ch < 2; ++ch) {
            const float* in  = c->input_buffers[0]  + ch * N;
            float*       out = c->output_buffers[0] + ch * N;
            for (uint32_t i = 0; i < N; ++i) {
                if (--cnt_[ch] < 0) { hold_[ch] = in[i]; cnt_[ch] = ds - 1; }
                const float q = std::round(hold_[ch] * levels) / levels;   // quantize the held sample
                out[i] = in[i] * (1.f - mx) + q * mx;
            }
        }
    }
};

// --- State-Variable Filter: one filter core, LP/HP/BP by a type param -----------------
// A TPT (topology-preserving transform) state-variable filter — the audio-graph's missing
// primitive. The frequency-split rack instantiates it twice (one LowPass, one HighPass) and
// fans a source out to both branches, so this single op powers the whole parallel-FX demo.
// RT-safe: per-channel POD state, coefficients computed once per block, denormal-flushed.
struct StateVariableFilterOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Filter";
    static constexpr const char* kSummary = "State-variable filter (low/high/band-pass) with resonance.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "filter", "eq" };

    Param<int>   type{ "type", 0, 0, 2 };                    // 0 = LowPass, 1 = HighPass, 2 = BandPass
    Param<float> cutoff{ "cutoff", 1000.f, 20.f, 20000.f };  // Hz
    Param<float> resonance{ "resonance", 0.1f, 0.f, 1.f };

    float ic1eq_[2] = { 0.f, 0.f };   // integrator states (per channel)
    float ic2eq_[2] = { 0.f, 0.f };

    static inline float flush(float x) { return std::abs(x) < 1e-18f ? 0.f : x; }   // denormal guard

    void collect_params(std::vector<ParamBase*>& o) override {
        semantic_intent(type, "filter type: 0 low-pass, 1 high-pass, 2 band-pass");
        semantic_intent(cutoff, "cutoff frequency (Hz)");    description(cutoff, "corner frequency");
        semantic_shape(resonance, "scalar");                 semantic_intent(resonance, "resonance / Q");
        o.push_back(&type); o.push_back(&cutoff); o.push_back(&resonance);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_in()); o.push_back(aud_out()); }

    void process_audio(const VividAudioContext* c) override {
        if (!c->input_buffers || !c->output_buffers) return;
        const uint32_t N   = c->buffer_size;
        const uint8_t  nch = c->input_channel_counts ? c->input_channel_counts[0] : 1;
        const float    sr  = static_cast<float>(c->sample_rate > 0 ? c->sample_rate : 48000);
        const int   ty  = c->param_values ? static_cast<int>(std::lround(c->param_values[0])) : type.int_value();
        float       fc  = c->param_values ? c->param_values[1] : cutoff.value;
        const float res = c->param_values ? c->param_values[2] : resonance.value;

        // Clamp cutoff to a stable range; map resonance 0..1 -> damping k (2 = flat, ~0.02 = high-Q).
        fc = std::min(std::max(fc, 20.f), 0.45f * sr);
        const float g  = std::tan(static_cast<float>(kPi) * fc / sr);
        const float k  = std::max(0.02f, 2.f - 1.98f * std::min(std::max(res, 0.f), 1.f));
        const float a1 = 1.f / (1.f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        for (uint8_t ch = 0; ch < nch && ch < 2; ++ch) {
            const float* in  = c->input_buffers[0]  + ch * N;
            float*       out = c->output_buffers[0] + ch * N;
            float ic1 = ic1eq_[ch], ic2 = ic2eq_[ch];
            for (uint32_t i = 0; i < N; ++i) {
                const float x  = in[i];
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.f * v1 - ic1;
                ic2 = 2.f * v2 - ic2;
                const float low  = v2;
                const float band = v1;
                const float high = x - k * v1 - v2;
                out[i] = (ty == 1) ? high : (ty == 2) ? band : low;
            }
            ic1eq_[ch] = flush(ic1);
            ic2eq_[ch] = flush(ic2);
        }
    }
};

// --- Test Tone: a simple polyphonic sine instrument (an instrument spike) --------------
struct TestToneOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_SOURCE; }   // ADR-0046
    static constexpr const char* kDisplayName = "Test Tone";
    static constexpr const char* kSummary = "Simple sine instrument (native-instrument spike).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "instrument", "tone" };

    Param<float> gain{ "gain", 0.5f, 0.f, 1.f };

    struct Voice { bool on = false; int pitch = -1; int32_t id = 0; double phase = 0.0; float vel = 0.f; };
    static constexpr int kV = 16;
    Voice v_[kV];

    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&gain); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }

    void note_on(int pitch, float vel, int32_t id) {
        for (auto& v : v_) if (!v.on) { v = Voice{ true, pitch, id, 0.0, vel }; return; }
    }
    void note_off(int32_t id) { for (auto& v : v_) if (v.on && v.id == id) v.on = false; }

    void process_audio(const VividAudioContext* c) override {
        if (!c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const float sr = static_cast<float>(c->sample_rate);
        const float g  = c->param_values ? c->param_values[0] : gain.value;
        float* L = c->output_buffers[0];
        float* R = c->output_buffers[0] + N;
        uint32_t ei = 0;
        for (uint32_t i = 0; i < N; ++i) {
            while (ei < c->note_event_count && c->note_events[ei].sample_offset <= i) {
                const VividNoteEvent& e = c->note_events[ei++];
                if (e.on) note_on(e.pitch, e.velocity, e.note_id); else note_off(e.note_id);
            }
            float s = 0.f;
            for (auto& v : v_) if (v.on) {
                const double f = 440.0 * std::pow(2.0, (v.pitch - 69) / 12.0);
                s += static_cast<float>(std::sin(v.phase)) * v.vel * 0.25f;
                v.phase += 2.0 * kPi * f / sr;
                if (v.phase > 2.0 * kPi) v.phase -= 2.0 * kPi;
            }
            s *= g;
            L[i] = s; R[i] = s;
        }
        while (ei < c->note_event_count) {   // events at/after the block end
            const VividNoteEvent& e = c->note_events[ei++];
            if (e.on) note_on(e.pitch, e.velocity, e.note_id); else note_off(e.note_id);
        }
    }
};

// --- MovieAudio: the audio track of a Video node's movie, as a graph source ------------
// A source op (audio output only) that DRAINS a movie-audio bus channel filled by a self-decoding
// Video op on the render thread (operator_api/movie_audio.h). Because it's a real graph node, its
// output can be wired through effects. The pull is transport-gated at the bus and advances the
// channel's master A/V clock, which the Video op reads back to keep the picture locked to this sound.
// Link the two nodes by matching this `source` to the Video node's `audio_bus`.
struct MovieAudioOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_SOURCE; }   // ADR-0046
    static constexpr const char* kDisplayName = "Movie Audio";
    static constexpr const char* kSummary = "Audio track of a Video node's movie, as a graph source (wire through effects).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "movie", "source" };

    Param<int>   source{ "source", 0, 0, VIVID_MOVIE_AUDIO_CHANNELS - 1 };
    Param<float> gain{ "gain", 1.f, 0.f, 2.f };

    void collect_params(std::vector<ParamBase*>& o) override {
        vivid::description(source, "Movie-audio bus channel to play (match a Video node's 'audio_bus')");
        o.push_back(&source); o.push_back(&gain);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }

    void process_audio(const VividAudioContext* c) override {
        if (!c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        float* L = c->output_buffers[0];
        float* R = c->output_buffers[0] + N;
        int ch = c->param_values ? static_cast<int>(c->param_values[0] + 0.5f) : source.value;
        if (ch < 0) ch = 0; if (ch >= VIVID_MOVIE_AUDIO_CHANNELS) ch = VIVID_MOVIE_AUDIO_CHANNELS - 1;
        vivid_movie_audio_pull(ch, L, R, N);   // silence + frozen clock when the transport is paused
        const float g = c->param_values ? c->param_values[1] : gain.value;
        if (g != 1.f) for (uint32_t i = 0; i < N; ++i) { L[i] *= g; R[i] *= g; }
    }
};

// --- Sampler: polyphonic, pitched sample-playback instrument ---------------------------
// A source instrument that plays in-memory PCM per note through the shared sample-engine
// voice (audio/sample_engine/voice.h): real repitch with linear interpolation, an amplitude
// ADSR, optional loop, and polyphony with oldest-voice stealing. PCM arrives via the
// SamplerLoadable escape hatch (slice→MIDI or a direct file load), not params:
//   - direct load (nslices == 0): one region spanning the whole keyboard, so every note
//     transposes the sample — a playable melodic instrument.
//   - slice→MIDI  (nslices  > 0): one single-note region per slice at base_note+i (drum-rack),
//     now click-free with an envelope.
struct SamplerOp : OperatorBase, AudioProcessable, SamplerLoadable, SamplerPreviewable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_SOURCE; }   // ADR-0046
    static constexpr const char* kDisplayName = "Sampler";
    static constexpr const char* kSummary = "Plays pitched PCM per note with ADSR + polyphony (melodic or drum-rack).";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "instrument", "sampler", "slice" };

    // Params 0..2 keep their original identity/order so control-edge overrides and saved
    // projects stay valid; the ADSR / voices / tuning params are appended (ABI is additive).
    Param<int>   base_note{ "base_note", 36, 0, 127 };    // root pitch (direct load plays it 1:1; slice 0 maps here)
    Param<float> gain{ "gain", 1.f, 0.f, 2.f };
    Param<int>   gate{ "gate", 0, 0, 1 };                 // 0 = one-shot (ignore note-off), 1 = gate (ADSR release on note-off)
    Param<float> attack { "attack",  0.001f, 0.f, 4.f };
    Param<float> decay  { "decay",   0.0f,   0.f, 4.f };
    Param<float> sustain{ "sustain", 1.0f,   0.f, 1.f };
    Param<float> release{ "release", 0.05f,  0.f, 8.f };  // default > 0 so gated note-off fades, not clicks
    Param<int>   voices { "voices",  16, 1, 32 };
    Param<int>   transpose{ "transpose", 0, -48, 48 };    // semitones
    Param<int>   tune    { "tune", 0, -100, 100 };        // cents

    // The sample bank is swapped ATOMICALLY so load_pcm is safe on a LIVE op (loading a new sample
    // into an already-running node): the audio thread reads bank_ via one acquire load per block, and
    // a load publishes a fresh bank with a release store. Old banks are RETAINED in banks_ (never
    // freed on the audio thread) so a voice still playing an old region — or an in-flight block — stays
    // valid; everything is released when the op is destroyed (UI/shutdown thread). banks_ holds heap
    // SampleBanks, so pushing a new one never moves the existing bank objects (only the owning vector).
    std::vector<std::unique_ptr<sample_engine::SampleBank>> banks_;
    std::atomic<sample_engine::SampleBank*> bank_{nullptr};   // current bank (audio thread: acquire)
    static constexpr int kV = 32;
    sample_engine::Voice v_[kV];

    // A downsampled peak envelope of the loaded sample, computed once per load on the UI thread and
    // read by the UI to draw the node's waveform thumbnail (SamplerPreviewable). UI-thread-only.
    static constexpr int kPeakBins = 128;
    std::vector<float> peaks_;
    // For the animated playhead: the concatenated start frame of each region (parallel to peaks_) and
    // the total frame count, cached at load. The audio thread publishes the most-recent active voice's
    // normalized position (0..1 across the whole waveform, -1 = nothing playing) for the UI to draw.
    std::vector<uint32_t>  region_base_;
    uint64_t               total_frames_ = 0;
    uint32_t               voice_base_[kV] = {};       // concatenated base frame of each voice's region
    std::atomic<float>     playhead_{ -1.f };

    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&base_note); o.push_back(&gain); o.push_back(&gate);
        o.push_back(&attack); o.push_back(&decay); o.push_back(&sustain); o.push_back(&release);
        o.push_back(&voices); o.push_back(&transpose); o.push_back(&tune);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_out()); }

    // Build a fresh bank from injected PCM and publish it atomically. Each region owns its own
    // SampleData (a slice is copied out of the shared buffer) so the voice engine plays it from frame 0
    // with no start-offset concept. Runs on the UI thread — either before the op is published (the
    // slice→MIDI path) or on a live op (the direct file-load path); the atomic publish makes both safe.
    void load_pcm(const float* L, const float* R, size_t n, uint32_t sr,
                  const uint32_t* starts, const uint32_t* ends, int nslices, int base) override {
        base_note.value = static_cast<float>(base);
        auto bank = std::make_unique<sample_engine::SampleBank>();
        bank->groups.emplace_back();
        auto& regions = bank->groups[0].regions;
        const bool stereo = (R != nullptr);
        auto make_data = [&](size_t a, size_t b) {
            auto d = std::make_shared<sample_engine::SampleData>();
            d->sample_rate = sr;              // 0 => resolved to the device rate at note-on (ratio 1.0)
            d->stereo = stereo;
            d->samples_L.assign(L + a, L + b);
            if (stereo) d->samples_R.assign(R + a, R + b);
            return d;
        };
        if (nslices <= 0) {                    // whole sample mapped across the keyboard (melodic)
            sample_engine::SampleRegion r;
            r.root_note = base; r.lo_note = 0; r.hi_note = 127;
            r.data = make_data(0, n);
            regions.push_back(std::move(r));
        } else {                               // one single-note region per slice (drum-rack)
            for (int i = 0; i < nslices; ++i) {
                const size_t a = std::min<size_t>(starts[i], n);
                const size_t b = std::min<size_t>(ends[i], n);
                if (b <= a) continue;
                sample_engine::SampleRegion r;
                r.root_note = r.lo_note = r.hi_note = base + i;
                r.data = make_data(a, b);
                regions.push_back(std::move(r));
            }
        }
        for (auto& v : v_) { v.active = false; v.gate = false; }   // reload clears ringing voices (load contract)
        compute_peaks(regions);                      // cache the waveform thumbnail (UI thread)
        sample_engine::SampleBank* raw = bank.get();
        banks_.push_back(std::move(bank));           // retain (freed at op destroy, never on the audio thread)
        bank_.store(raw, std::memory_order_release); // publish
    }

    // Downsample the loaded regions (concatenated in order — the whole break, or one melodic sample)
    // to kPeakBins absolute-peak bins for the node thumbnail. Runs on the UI thread inside load_pcm.
    void compute_peaks(const std::vector<sample_engine::SampleRegion>& regions) {
        peaks_.assign(kPeakBins, 0.f);
        region_base_.assign(regions.size(), 0u);
        size_t total = 0;
        for (size_t k = 0; k < regions.size(); ++k) {
            region_base_[k] = static_cast<uint32_t>(total);           // concatenated start of region k
            if (regions[k].data) total += regions[k].data->samples_L.size();
        }
        total_frames_ = total;
        if (total == 0) { peaks_.clear(); return; }
        for (int i = 0; i < kPeakBins; ++i) {
            const size_t a = total * static_cast<size_t>(i) / kPeakBins;
            const size_t b = total * static_cast<size_t>(i + 1) / kPeakBins;
            float peak = 0.f;
            size_t base = 0;
            for (const auto& r : regions) {
                if (!r.data) continue;
                const auto& L = r.data->samples_L;
                const size_t len = L.size();
                const size_t lo = std::max(a, base), hi = std::min(b, base + len);
                for (size_t j = lo; j < hi; ++j) peak = std::max(peak, std::fabs(L[j - base]));
                base += len;
            }
            peaks_[i] = std::min(1.f, peak);
        }
    }

    // SamplerPreviewable: copy the cached peak envelope, nearest-resampled to `n` bins.
    int copy_peaks(float* out, int n) const override {
        if (!out || n <= 0 || peaks_.empty()) return 0;
        const int have = static_cast<int>(peaks_.size());
        for (int i = 0; i < n; ++i) out[i] = peaks_[std::min(have - 1, i * have / n)];
        return n;
    }

    // --- voice allocation over v_[0..nv) : note-id keyed, bounded by the `voices` param ---
    static uint64_t vid(int32_t id) { return static_cast<uint64_t>(static_cast<uint32_t>(id)); }
    int find_by_id(int32_t id, int nv) const {
        for (int i = 0; i < nv; ++i) if (v_[i].active && v_[i].note_id == vid(id)) return i;
        return -1;
    }

    void handle_on(const sample_engine::SampleGroup& grp,
                   int pitch, float vel, int32_t id, float tuning, int transp, int cents,
                   bool one_shot, double dev_sr, uint64_t frame, int nv) {
        // No nearest-region fallback: a drum-rack note outside its slice map stays silent, and the
        // melodic path already covers 0..127 with one region. (Nearest-fallback is a multisample-zone
        // feature for a later phase.)
        const sample_engine::SampleRegion* reg = sample_engine::find_region(grp, pitch, vel);
        if (!reg || !reg->data || reg->data->samples_L.empty()) return;
        int idx = find_by_id(id, nv);
        if (idx < 0) idx = sample_engine::find_free_voice(v_, nv);
        if (idx < 0) idx = sample_engine::steal_oldest_voice(v_, nv);
        if (idx < 0) return;
        const double src_sr = reg->data->sample_rate > 0 ? static_cast<double>(reg->data->sample_rate) : dev_sr;
        const double semis = (pitch - reg->root_note) + transp + tuning
                             + (cents + reg->tune_cents) / 100.0;
        const double rate = std::pow(2.0, semis / 12.0) * (src_sr / dev_sr);
        sample_engine::voice_note_on(v_[idx], pitch, vel, reg, rate, frame, one_shot);
        v_[idx].note_id = vid(id);             // for note-off matching (voice_note_on doesn't set it)
        const size_t ri = static_cast<size_t>(reg - grp.regions.data());   // region index → its base frame
        voice_base_[idx] = ri < region_base_.size() ? region_base_[ri] : 0u;
    }

    void handle_off(int32_t id, int nv) {
        const int idx = find_by_id(id, nv);
        if (idx >= 0) sample_engine::voice_note_off(v_[idx]);   // no-op for one-shot voices (ring out)
    }

    void process_audio(const VividAudioContext* c) override {
        if (!c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const double dev_sr = c->sample_rate > 0 ? static_cast<double>(c->sample_rate) : 48000.0;
        const float  dt = static_cast<float>(1.0 / dev_sr);
        const float* pv = c->param_values;
        const float g        = pv ? pv[1] : gain.value;
        const bool  one_shot = (pv ? pv[2] : gate.value) < 0.5f;   // gate == 0 => one-shot
        const float a        = pv ? pv[3] : attack.value;
        const float d        = pv ? pv[4] : decay.value;
        const float s        = pv ? pv[5] : sustain.value;
        const float rel      = pv ? pv[6] : release.value;
        const int   nv       = std::clamp(pv ? static_cast<int>(std::lround(pv[7])) : voices.int_value(), 1, kV);
        const int   transp   = pv ? static_cast<int>(std::lround(pv[8])) : transpose.int_value();
        const int   cents    = pv ? static_cast<int>(std::lround(pv[9])) : tune.int_value();
        float* L = c->output_buffers[0];
        float* R = c->output_buffers[0] + N;

        // One acquire load of the current bank for the whole block (the direct file-load path may
        // publish a new one concurrently). A note-on needs a loaded group; a note-off is honored
        // regardless (voices from a prior bank stay valid and must still release).
        sample_engine::SampleBank* bank = bank_.load(std::memory_order_acquire);
        const sample_engine::SampleGroup* grp =
            (bank && !bank->groups.empty()) ? &bank->groups[0] : nullptr;

        auto on_event = [&](const VividNoteEvent& e, uint32_t i) {
            if (e.on) { if (grp) handle_on(*grp, e.pitch, e.velocity, e.note_id, e.tuning,
                                           transp, cents, one_shot, dev_sr, c->frame + i, nv); }
            else handle_off(e.note_id, nv);
        };

        uint32_t ei = 0;
        for (uint32_t i = 0; i < N; ++i) {
            while (ei < c->note_event_count && c->note_events[ei].sample_offset <= i)
                on_event(c->note_events[ei++], i);
            float ml = 0.f, mr = 0.f;
            for (int k = 0; k < nv; ++k)
                if (v_[k].active)
                    sample_engine::voice_render_frame(v_[k], ml, mr, dt, a, d, s, rel);
            L[i] = ml * g;
            R[i] = mr * g;
        }
        while (ei < c->note_event_count)                    // events at/after the block end
            on_event(c->note_events[ei++], N);

        // Publish the most-recent active voice's position for the UI playhead (0..1 across the whole
        // waveform, -1 = nothing playing). Newest voice = largest start_frame among the active ones.
        int newest = -1;
        for (int k = 0; k < nv; ++k)
            if (v_[k].active && (newest < 0 || v_[k].start_frame > v_[newest].start_frame)) newest = k;
        float ph = -1.f;
        if (newest >= 0 && total_frames_ > 0) {
            const double pos = static_cast<double>(voice_base_[newest]) + v_[newest].playback_pos;
            ph = std::clamp(static_cast<float>(pos / static_cast<double>(total_frames_)), 0.f, 1.f);
        }
        playhead_.store(ph, std::memory_order_release);
    }

    // SamplerPreviewable: the current playhead position (0..1), or -1 when nothing is sounding.
    float playhead() const override { return playhead_.load(std::memory_order_acquire); }
};


// ---- Arp: the first NOTE EFFECT (ADR-0015 / ABI v12) --------------------------------------
// Notes in -> notes out. It holds whatever keys are down and re-issues them as a rhythmic
// sequence, so ONE held note becomes an arpeggio. It makes no sound of its own: it is the proof
// that notes are a real signal in the graph, and the reason the graph needed note edges at all.
//
// RT-safe: fixed-size held-note table, no allocation, no locking. All timing is in samples so the
// pattern stays phase-locked across blocks.
struct ArpOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Arp";
    static constexpr const char* kSummary = "Note effect: holds the keys you play and re-issues them as an arpeggio.";
    static constexpr std::array<const char*, 4> kKeywords = {"audio", "note", "arpeggiator", "midi"};

    // ADR-0047: role lives in the descriptor (retires the audio_op_mark_* name table) — notes in, notes out.
    VividAudioRole declared_audio_role() const override { return VIVID_AUDIO_ROLE_NOTE_EFFECT; }

    Param<int>   rate{"rate", 2, {"1/4", "1/8", "1/8T", "1/16", "1/32"}};
    Param<int>   mode{"mode", 0, {"Up", "Down", "UpDown", "Random"}};
    Param<int>   octaves{"octaves", 1, 1, 4};
    Param<float> gate{"gate", 0.5f, 0.05f, 1.0f};

    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&rate); o.push_back(&mode); o.push_back(&octaves); o.push_back(&gate);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        // ADR-0047: the graph still routes this port as an audio buffer, but the real stream is a note
        // stream (emitted via note_out) — declare that truth in semantic_shape so the catalog is honest.
        VividPortDescriptor p{};
        p.name = "output"; p.type = VIVID_PORT_SCALAR; p.direction = VIVID_PORT_OUTPUT;   // ADR-0047: transport (below) is the real type
        p.value_type = VIVID_VALUE_FLOAT; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
        p.semantic_shape = "note_stream"; p.transport = VIVID_PORT_TRANSPORT_NOTE_STREAM;
        o.push_back(p);
    }

    static constexpr int kMaxHeld = 16;
    int      held[kMaxHeld] = {};
    int      n_held = 0;
    long long pos = 0;             // absolute sample position (keeps the pattern phase-locked)
    long long next_step = 0;       // when the next arp step fires
    long long off_at = -1;         // when the sounding arp note releases
    int      step_i = 0;           // index into the pattern
    bool     going_up = true;      // UpDown direction
    int      sounding = -1;        // pitch currently issued by the arp (-1 = none)
    int32_t  next_id = 900000;     // note-id namespace of our own (never collides with clip/live ids)
    uint32_t rng = 0x1234567u;

    void hold(int pitch) {
        for (int i = 0; i < n_held; ++i) if (held[i] == pitch) return;
        if (n_held >= kMaxHeld) return;
        int i = n_held++;                                   // keep sorted (ascending) for Up/Down
        while (i > 0 && held[i - 1] > pitch) { held[i] = held[i - 1]; --i; }
        held[i] = pitch;
    }
    void release(int pitch) {
        for (int i = 0; i < n_held; ++i)
            if (held[i] == pitch) {
                for (int k = i; k + 1 < n_held; ++k) held[k] = held[k + 1];
                --n_held;
                return;
            }
    }
    // The pitch for step `k` over the held notes, extended across `oct` octaves.
    int pitch_for_step(int k, int oct) {
        const int span = n_held * (oct < 1 ? 1 : oct);
        if (span <= 0) return -1;
        int idx = 0;
        switch (mode.int_value()) {
            case 1: idx = span - 1 - (k % span); break;                       // Down
            case 2: {                                                          // UpDown
                const int cycle = span > 1 ? (span - 1) * 2 : 1;
                const int c = k % cycle;
                idx = (c < span) ? c : cycle - c;
                break;
            }
            case 3:                                                            // Random
                rng = rng * 1664525u + 1013904223u;
                idx = static_cast<int>((rng >> 16) % static_cast<uint32_t>(span));
                break;
            default: idx = k % span; break;                                    // Up
        }
        const int base = held[idx % n_held];
        return base + 12 * (idx / n_held);
    }

    void process_audio(const VividAudioContext* c) override {
        // Silent: an arpeggiator makes no audio. (Its node's audio output is unused.)
        const uint32_t frames = c->buffer_size;
        for (uint32_t ch = 0; ch < 2 && c->output_buffers; ++ch)
            if (c->output_buffers[0]) std::memset(c->output_buffers[0] + ch * frames, 0, frames * sizeof(float));

        uint32_t out_n = 0;
        VividNoteEvent* out = c->note_out;
        const uint32_t cap = c->note_out_capacity;
        const auto emit = [&](uint32_t off, bool on, int pitch, float vel, int32_t id) {
            if (!out || out_n >= cap || pitch < 0 || pitch > 127) return;
            out[out_n++] = VividNoteEvent{ off, static_cast<uint8_t>(on ? 1 : 0),
                                           static_cast<int16_t>(pitch), vel, id, 0.f };
        };

        // Step length in samples, from the transport (so the arp is in time, not free-running).
        const double bpm = c->metronome_bpm > 1.f ? c->metronome_bpm : 120.0;
        const double spb = 60.0 * static_cast<double>(c->sample_rate) / bpm;   // samples per beat
        static const double kBeats[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };  // 1/4 1/8 1/8T 1/16 1/32
        const int ri = rate.int_value() < 0 ? 0 : (rate.int_value() > 4 ? 4 : rate.int_value());
        const long long step = static_cast<long long>(spb * kBeats[ri]);
        const long long step_len = step > 32 ? step : 32;                       // sanity floor

        // Fold this block's incoming note-ons/offs into the held set, at their sample offsets.
        // (Held-note edits are quantized to the block; the arp's own output is sample-accurate.)
        for (uint32_t i = 0; i < c->note_event_count; ++i) {
            const VividNoteEvent& e = c->note_events[i];
            if (e.on) hold(e.pitch); else release(e.pitch);
        }

        for (uint32_t i = 0; i < frames; ++i) {
            const long long now = pos + static_cast<long long>(i);
            if (sounding >= 0 && off_at >= 0 && now >= off_at) {   // release the sounding step
                emit(i, false, sounding, 0.f, next_id);
                sounding = -1; off_at = -1;
            }
            if (now >= next_step) {
                if (n_held > 0) {
                    if (sounding >= 0) { emit(i, false, sounding, 0.f, next_id); sounding = -1; }
                    const int p = pitch_for_step(step_i++, octaves.int_value());
                    if (p >= 0) {
                        ++next_id;
                        emit(i, true, p, 0.9f, next_id);
                        sounding = p;
                        const float g = gate.value < 0.05f ? 0.05f : (gate.value > 1.f ? 1.f : gate.value);
                        off_at = now + static_cast<long long>(step_len * static_cast<double>(g));
                    }
                } else {
                    step_i = 0;   // nothing held: idle, and restart the pattern on the next key
                }
                next_step = now + step_len;
            }
        }
        pos += frames;
        if (c->note_out_count) *c->note_out_count = out_n;
    }
};

// --- LFO: the first MODULATOR (ADR-0022) ---------------------------------------------
// Emits no sound. It writes a normalized 0..1 signal to `control_out` (ABI v13), and a CONTROL
// edge carries that to any param on any node — the audio peer of what the visuals graph has had
// since the mapping bridge existed.
//
// Deliberately NOT here: amount, offset, and polarity. Those belong to the EDGE (ControlShape),
// not the source, so one LFO can drive a cutoff bipolar and a delay-mix unipolar at different
// depths from the same wire. vivid-classic put `amplitude`/`offset`/`polarity` on its LFO *and* a
// polarity on the assignment, then never consulted the source's — an author could set it and get
// nothing. One place, and it is the wire.
//
// RT-safe: no allocation, no locking. `phase_` and friends are plain members, never Param<> —
// the host copies param values into Param<> members every block, so computed state living in a
// param would be clobbered on arrival.
struct LfoOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_SOURCE; }   // ADR-0046
    static constexpr const char* kDisplayName = "LFO";
    static constexpr const char* kSummary = "Modulator: a low-frequency oscillator that drives any param through a control wire.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "control", "modulation", "lfo" };

    // ADR-0047: role lives in the descriptor (retires the audio_op_mark_* name table) — emits control.
    VividAudioRole declared_audio_role() const override { return VIVID_AUDIO_ROLE_MODULATOR; }

    Param<int>   waveform{ "waveform", 0, { "Sine", "Triangle", "Saw", "Square", "Random" } };
    Param<int>   sync{ "sync", 0, { "Free", "Sync" } };
    Param<float> rate{ "rate", 1.f, 0.01f, 20.f };                              // Hz (Free)
    Param<int>   division{ "division", 2, { "1/1", "1/2", "1/4", "1/8", "1/16" } };   // beats (Sync)

    double   phase_ = 0.0;          // 0..1, free-running; continuous ACROSS blocks
    double   prev_ph_ = 0.0;        // wrap detection for Random (sample & hold)
    float    sh_ = 0.5f;            // the held random value
    uint32_t rng_ = 0x2545F49u;

    void collect_params(std::vector<ParamBase*>& o) override {
        semantic_intent(waveform, "oscillator shape");
        semantic_intent(sync, "0 free-running (rate in Hz), 1 locked to the transport (division in beats)");
        semantic_intent(rate, "oscillation rate in Hz when sync = Free");
        semantic_intent(division, "cycle length in beats when sync = Sync");
        o.push_back(&waveform); o.push_back(&sync); o.push_back(&rate); o.push_back(&division);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        // ADR-0047: no audio — the real stream leaves via control_out. The port stays an audio buffer
        // for graph routing, but semantic_shape declares the truth (an Audio wire off an LFO is silence).
        auto out = aud_out(); out.type = VIVID_PORT_SCALAR;   // ADR-0047: not audio
        out.semantic_shape = "control_signal"; out.transport = VIVID_PORT_TRANSPORT_CONTROL_SIGNAL;
        o.push_back(out);
    }

    static float beats_per_cycle(int d) {
        switch (d) { case 0: return 4.f; case 1: return 2.f; case 2: return 1.f; case 3: return 0.5f; default: return 0.25f; }
    }
    float rand01() {
        rng_ = rng_ * 1664525u + 1013904223u;                      // LCG; no allocation, no <random>
        return static_cast<float>((rng_ >> 8) & 0xFFFFFFu) / 16777215.f;
    }
    float shape(int wf, double ph) {
        switch (wf) {
            case 0:  return 0.5f + 0.5f * static_cast<float>(std::sin(2.0 * kPi * ph));
            case 1:  return ph < 0.5 ? static_cast<float>(ph * 2.0) : static_cast<float>(2.0 - ph * 2.0);
            case 2:  return static_cast<float>(ph);
            case 3:  return ph < 0.5 ? 0.f : 1.f;
            default: return sh_;                                    // Random: held for the cycle
        }
    }

    void process_audio(const VividAudioContext* c) override {
        // Not wired to anything: no control output means nothing to do. (An unwired LFO must not
        // burn a block's worth of sin() for a buffer no one reads.)
        if (!c->control_out || c->control_out_capacity == 0) return;
        const uint32_t N  = c->control_out_capacity;
        const double   sr = c->sample_rate > 0 ? c->sample_rate : 48000.0;
        const int wf   = c->param_values ? static_cast<int>(std::lround(c->param_values[0])) : waveform.int_value();
        const int syn  = c->param_values ? static_cast<int>(std::lround(c->param_values[1])) : sync.int_value();
        const float hz = c->param_values ? c->param_values[2] : rate.value;
        const int div  = c->param_values ? static_cast<int>(std::lround(c->param_values[3])) : division.int_value();

        // Sync reads phase straight off the transport rather than integrating, so the LFO is
        // locked to the bar wherever playback starts and cannot drift across blocks.
        const bool   synced = (syn == 1);
        const double bpm    = c->metronome_bpm > 0.f ? c->metronome_bpm : 120.0;
        const double cyc    = beats_per_cycle(div);
        const double per_s  = synced ? (bpm / 60.0 / sr) / cyc                  // cycles per sample
                                     : static_cast<double>(hz < 0.f ? 0.f : hz) / sr;
        double ph = synced ? c->metronome_beats_elapsed / cyc : phase_;
        ph -= std::floor(ph);

        for (uint32_t i = 0; i < N; ++i) {
            if (ph < prev_ph_) sh_ = rand01();     // wrapped -> hold a fresh value for this cycle
            prev_ph_ = ph;
            c->control_out[i] = shape(wf, ph);
            ph += per_s;
            if (ph >= 1.0) ph -= std::floor(ph);
        }
        if (!synced) phase_ = ph;                  // free-running: carry the phase to the next block
    }
};

// A note-gated ADSR envelope modulator. Like the LFO it emits a 0..1 CONTROL signal (no audio) on
// control_out — so its value is auto-exposed to the visuals as node_<t>_<n>.ctl. Unlike the LFO it is
// SHAPED BY THE PERFORMANCE: a note-on (re)triggers the attack, the last note-off starts the release —
// so a mapped visual param swells and fades with the phrasing (musical dynamics, not a fixed wobble).
struct AdsrOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_SOURCE; }   // ADR-0046
    static constexpr const char* kDisplayName = "ADSR";
    static constexpr const char* kSummary = "Modulator: a note-gated attack/decay/sustain/release envelope that drives any param through a control wire.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "control", "modulation", "envelope" };

    // ADR-0047: role lives in the descriptor (retires the audio_op_mark_* name table) — emits control.
    VividAudioRole declared_audio_role() const override { return VIVID_AUDIO_ROLE_MODULATOR; }

    Param<float> attack{ "attack", 0.01f, 0.001f, 3.f };    // seconds: 0 -> 1 on note-on
    Param<float> decay{ "decay", 0.15f, 0.001f, 3.f };      // seconds: 1 -> sustain
    Param<float> sustain{ "sustain", 0.7f, 0.f, 1.f };      // held level while a note is down
    Param<float> release{ "release", 0.3f, 0.001f, 5.f };   // seconds: sustain -> 0 on the last note-off

    float env_ = 0.f;   // current envelope value (carried across blocks)
    int   stage_ = 0;   // 0 idle · 1 attack · 2 decay · 3 sustain · 4 release
    int   held_ = 0;    // voices currently down (gate = held_ > 0)

    void collect_params(std::vector<ParamBase*>& o) override {
        semantic_intent(attack, "attack time in seconds (rise on note-on)");
        semantic_intent(decay, "decay time in seconds (fall to the sustain level)");
        semantic_intent(sustain, "sustain level 0..1 held while a note is down");
        semantic_intent(release, "release time in seconds (fall to 0 after the last note-off)");
        o.push_back(&attack); o.push_back(&decay); o.push_back(&sustain); o.push_back(&release);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        // ADR-0047: emits a control signal via control_out, not audio (like the LFO).
        auto out = aud_out(); out.type = VIVID_PORT_SCALAR;   // ADR-0047: not audio
        out.semantic_shape = "control_signal"; out.transport = VIVID_PORT_TRANSPORT_CONTROL_SIGNAL;
        o.push_back(out);
    }

    void process_audio(const VividAudioContext* c) override {
        if (!c->control_out || c->control_out_capacity == 0) return;
        const uint32_t N  = c->control_out_capacity;
        const float    sr = c->sample_rate > 0 ? static_cast<float>(c->sample_rate) : 48000.f;
        const float A = c->param_values ? c->param_values[0] : attack.value;
        const float D = c->param_values ? c->param_values[1] : decay.value;
        const float S = c->param_values ? c->param_values[2] : sustain.value;
        const float R = c->param_values ? c->param_values[3] : release.value;
        // Gate from the block's events (applied at block start — a few ms of timing slack, inaudible for
        // an envelope): a note-on (re)triggers attack; the last note-off begins release.
        for (uint32_t e = 0; e < c->note_event_count; ++e) {
            if (c->note_events[e].on) { ++held_; stage_ = 1; }
            else { if (held_ > 0) --held_; if (held_ == 0) stage_ = 4; }
        }
        const float aInc = 1.f / std::max(1e-4f, A * sr);
        const float dInc = (1.f - S) / std::max(1e-4f, D * sr);
        const float rInc = 1.f / std::max(1e-4f, R * sr);
        for (uint32_t i = 0; i < N; ++i) {
            switch (stage_) {
                case 1: env_ += aInc; if (env_ >= 1.f) { env_ = 1.f; stage_ = 2; } break;
                case 2: env_ -= dInc; if (env_ <= S)   { env_ = S;   stage_ = 3; } break;
                case 3: env_ = S; break;
                case 4: env_ -= rInc; if (env_ <= 0.f) { env_ = 0.f; stage_ = 0; } break;
                default: env_ = 0.f; break;
            }
            c->control_out[i] = std::clamp(env_, 0.f, 1.f);
        }
    }
};

// ==== Note GENERATORS (ADR-0022 P3.3) ====================================================
// Algorithmic note SOURCES: they read NO input notes and emit their own, phase-locked to the
// transport (c->metronome_beats_elapsed), so a scene that gates one off then on resyncs with no
// drift. Each declares only an audio OUTPUT port (silent) like Arp, so it reads as a "source"; it
// is marked a GENERATOR (audio_op_mark_gen_op) to keep it out of the instrument list. They implement
// NoteFlushable so the host can release their held voices when their scene stops (see note_flush).
//
// NoteGenBase carries the shared machinery: the transport step clock, note-out emission with
// sample-accurate offsets, and voice tracking. A subclass only decides which pitches a given step
// fires (step_notes) plus its rate/gate/velocity.
struct NoteGenBase : OperatorBase, AudioProcessable, NoteFlushable {
    // v14: built-in generators carry the role in their descriptor too (not only the audio_op_mark_gen_op
    // name table), so built-in and loaded-dylib generators classify through the identical path.
    VividAudioRole declared_audio_role() const override { return VIVID_AUDIO_ROLE_GENERATOR; }
    // ADR-0046: Euclid/Chord/RandMelody bundle timing + note material + gate + voicing in one node,
    // so they classify as RECIPES — offered but ranked below composable note/rhythm primitives.
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_RECIPE; }
    static constexpr int kRateN = 6;
    static double rate_beats(int i) {   // 1/1 1/2 1/4 1/8 1/16 1/8T, in beats per step
        static const double b[kRateN] = { 4.0, 2.0, 1.0, 0.5, 0.25, 1.0 / 3.0 };
        return b[i < 0 ? 0 : (i >= kRateN ? kRateN - 1 : i)];
    }
    static constexpr int kMaxVoices = 8;
    struct Voice { int pitch; int32_t id; };
    Voice     voices_[kMaxVoices] = {};
    int       n_voices_ = 0;
    double    off_beat_ = -1.0;     // absolute transport beat when the held voices release
    long long last_step_ = -1;      // last step index fired (dedup across block boundaries)
    int32_t   next_id_ = 0;
    int32_t   id_base_ = 900000;    // our own note-id namespace (never collides with clip/live ids)

    // Subclass hooks:
    virtual int    step_notes(long long step_index, int* out) = 0;   // pitches this step fires (0 = rest)
    virtual double step_beats() const = 0;
    virtual double gate_frac() const = 0;
    virtual float  velocity()   const = 0;

    template <class Emit> void release_all(uint32_t off, Emit&& emit) {
        for (int i = 0; i < n_voices_; ++i) emit(off, false, voices_[i].pitch, 0.f, voices_[i].id);
        n_voices_ = 0; off_beat_ = -1.0;
    }

    void process_audio(const VividAudioContext* c) override {
        const uint32_t frames = c->buffer_size;
        for (uint32_t ch = 0; ch < 2 && c->output_buffers && c->output_buffers[0]; ++ch)
            std::memset(c->output_buffers[0] + ch * frames, 0, frames * sizeof(float));   // silent
        VividNoteEvent* out = c->note_out;
        const uint32_t cap = c->note_out_capacity;
        uint32_t out_n = 0;
        const auto emit = [&](uint32_t off, bool on, int pitch, float vel, int32_t id) {
            if (!out || out_n >= cap || pitch < 0 || pitch > 127) return;
            out[out_n++] = VividNoteEvent{ off, static_cast<uint8_t>(on ? 1 : 0),
                                           static_cast<int16_t>(pitch), vel, id, 0.f };
        };
        const double bpm  = c->metronome_bpm > 1.f ? c->metronome_bpm : 120.0;
        const double spb  = 60.0 * static_cast<double>(c->sample_rate) / bpm;   // samples per beat
        const double beat0 = c->metronome_beats_elapsed;
        const double beat1 = beat0 + static_cast<double>(frames) / spb;
        const double sb = step_beats();
        if (sb <= 0.0) { if (c->note_out_count) *c->note_out_count = 0; return; }
        auto to_sample = [&](double beat) -> uint32_t {
            const double s = (beat - beat0) * spb;
            return static_cast<uint32_t>(std::min<double>(frames - 1, std::max(0.0, s)));
        };
        // 1) release held voices whose off-beat has passed (clamped to block start if slightly before).
        if (n_voices_ > 0 && off_beat_ >= 0.0 && off_beat_ < beat1)
            release_all(to_sample(off_beat_), emit);
        // 2) fire every step boundary that falls in [beat0, beat1).
        long long k = static_cast<long long>(std::ceil(beat0 / sb - 1e-9));
        for (; static_cast<double>(k) * sb < beat1 - 1e-12; ++k) {
            if (k == last_step_) continue;
            const double step_beat = static_cast<double>(k) * sb;
            if (step_beat < beat0 - 1e-12) continue;
            int pitches[kMaxVoices];
            const int np = step_notes(k, pitches);
            const uint32_t on_s = to_sample(step_beat);
            if (n_voices_ > 0) release_all(on_s, emit);      // retrigger: release the prior step first
            for (int i = 0; i < np && i < kMaxVoices; ++i) {
                const int32_t id = (++next_id_) + id_base_;
                emit(on_s, true, pitches[i], velocity(), id);
                voices_[n_voices_++] = { pitches[i], id };
            }
            if (np > 0) off_beat_ = step_beat + gate_frac() * sb;
            last_step_ = k;
        }
        if (c->note_out_count) *c->note_out_count = out_n;
    }

    // NoteFlushable: emit an off for every held voice (offset 0), then forget them — the host is
    // releasing us because our scene stopped, so on re-activation we start clean and resync to beat.
    void note_flush(VividNoteEvent* out, uint32_t cap, uint32_t* count) override {
        uint32_t n = 0;
        for (int i = 0; i < n_voices_ && n < cap; ++i)
            out[n++] = VividNoteEvent{ 0u, 0, static_cast<int16_t>(voices_[i].pitch), 0.f, voices_[i].id, 0.f };
        if (count) *count = n;
        n_voices_ = 0; off_beat_ = -1.0; last_step_ = -1;
    }
};

// Euclid: a Euclidean rhythm E(pulses, steps) on one note. The bucket formula
// (i*pulses)%steps < pulses distributes `pulses` hits as evenly as possible over `steps`.
struct EuclidOp : NoteGenBase {
    static constexpr const char* kDisplayName = "Euclid";
    static constexpr const char* kSummary = "Generator: a Euclidean rhythm on one note, locked to the transport.";
    static constexpr std::array<const char*, 4> kKeywords = {"audio", "note", "generator", "euclidean"};
    Param<int>   steps{"steps", 16, 1, 32};
    Param<int>   pulses{"pulses", 4, 0, 32};
    Param<int>   rotate{"rotate", 0, 0, 31};
    Param<int>   note{"note", 36, 0, 127};
    Param<int>   rate{"rate", 4, {"1/1", "1/2", "1/4", "1/8", "1/16", "1/8T"}};
    Param<float> gate{"gate", 0.5f, 0.05f, 1.0f};
    Param<float> vel{"velocity", 0.8f, 0.f, 1.f};
    EuclidOp() { id_base_ = 910000; }
    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&steps); o.push_back(&pulses); o.push_back(&rotate); o.push_back(&note);
        o.push_back(&rate); o.push_back(&gate); o.push_back(&vel);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        VividPortDescriptor p{}; p.name = "output"; p.type = VIVID_PORT_SCALAR;   // ADR-0047: transport is the real type
        p.direction = VIVID_PORT_OUTPUT; p.value_type = VIVID_VALUE_FLOAT;
        p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
        p.semantic_shape = "note_stream"; p.transport = VIVID_PORT_TRANSPORT_NOTE_STREAM;   // ADR-0047: real stream = notes
        o.push_back(p);
    }
    double step_beats() const override { return rate_beats(rate.int_value()); }
    double gate_frac()  const override { return gate.value; }
    float  velocity()   const override { return vel.value; }
    int step_notes(long long k, int* out) override {
        const int st = steps.int_value() < 1 ? 1 : steps.int_value();
        int pu = pulses.int_value(); pu = pu < 0 ? 0 : (pu > st ? st : pu);
        if (pu <= 0) return 0;
        const long long i = ((k % st) + st) % st;
        const long long j = ((i + rotate.int_value()) % st + st) % st;
        if (static_cast<int>((j * pu) % st) >= pu) return 0;   // not a hit
        out[0] = note.int_value();
        return 1;
    }
    // Thumbnail: the Euclidean pattern as a ring of `steps` dots, the `pulses` hits filled in the
    // track accent (same E(pulses,steps) formula as step_notes), rotated by `rotate`. ANIMATED: a
    // playhead orbits the ring at the transport position (ctx->time in beats) and the current step
    // flashes bright while its note is sounding (within the gate window).
    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        const VividDrawAPI& d = ctx->draw; if (!d.draw_rect) return;
        const auto pv = [&](int i, float def) { return ctx->param_count > (uint32_t)i ? ctx->param_values[i] : def; };
        int st = (int)std::lround(pv(0, 16.f)); if (st < 1) st = 1; if (st > 32) st = 32;
        int pu = (int)std::lround(pv(1, 4.f));  pu = pu < 0 ? 0 : (pu > st ? st : pu);
        const int   rot = (int)std::lround(pv(2, 0.f));
        const double gt = pv(5, 0.5f);
        const double sb = rate_beats((int)std::lround(pv(4, 4.f)));   // beats per step
        const long long curk = sb > 0.0 ? (long long)std::floor(ctx->time / sb) : 0;
        const int    cur  = (int)(((curk % st) + st) % st);
        const double frac = sb > 0.0 ? (ctx->time / sb - (double)curk) : 1.0;   // 0..1 within the step
        const float w = ctx->surface_width, h = ctx->surface_height;
        const VividColor on = ctx->accent, off = { 0.42f, 0.44f, 0.48f, 0.55f };
        const VividColor hot = { 1.f, 1.f, 1.f, 1.f };
        const float cx = w * 0.5f, cy = h * 0.5f, R = std::min(w, h) * 0.34f;
        const float ds = std::max(2.0f, std::min(w, h) * 0.11f);
        for (int i = 0; i < st; ++i) {
            const float a  = -1.5707963f + 6.2831853f * (float)i / (float)st;
            const float px = cx + R * std::cos(a), py = cy + R * std::sin(a);
            const int   j  = ((i + rot) % st + st) % st;
            const bool  hit = static_cast<int>(((long long)j * pu) % st) < pu;
            const bool  active = (i == cur) && hit && frac < gt;      // note currently sounding
            const float s  = active ? ds * 1.5f : (hit ? ds : ds * 0.6f);
            d.draw_rect(d.opaque, px - s * 0.5f, py - s * 0.5f, s, s, active ? hot : (hit ? on : off));
        }
        // the playhead marker orbiting the ring at the current step
        if (d.draw_circle && st > 0) {
            const float a = -1.5707963f + 6.2831853f * (float)cur / (float)st;
            d.draw_circle(d.opaque, cx + R * std::cos(a), cy + R * std::sin(a), ds * 0.95f, 1.5f, on);
        }
    }
};

// Chord: fires a chord stack every step. quality picks the intervals.
struct ChordOp : NoteGenBase {
    static constexpr const char* kDisplayName = "Chord";
    static constexpr const char* kSummary = "Generator: repeats a chord in time, locked to the transport.";
    static constexpr std::array<const char*, 4> kKeywords = {"audio", "note", "generator", "chord"};
    Param<int>   root{"root", 48, 0, 127};
    Param<int>   quality{"quality", 0, {"Maj", "Min", "Dom7", "Min7", "Sus4"}};
    Param<int>   rate{"rate", 2, {"1/1", "1/2", "1/4", "1/8", "1/16", "1/8T"}};
    Param<float> gate{"gate", 0.85f, 0.05f, 1.0f};
    Param<float> vel{"velocity", 0.8f, 0.f, 1.f};
    ChordOp() { id_base_ = 920000; }
    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&root); o.push_back(&quality); o.push_back(&rate); o.push_back(&gate); o.push_back(&vel);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        VividPortDescriptor p{}; p.name = "output"; p.type = VIVID_PORT_SCALAR;   // ADR-0047: transport is the real type
        p.direction = VIVID_PORT_OUTPUT; p.value_type = VIVID_VALUE_FLOAT;
        p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
        p.semantic_shape = "note_stream"; p.transport = VIVID_PORT_TRANSPORT_NOTE_STREAM;   // ADR-0047: real stream = notes
        o.push_back(p);
    }
    double step_beats() const override { return rate_beats(rate.int_value()); }
    double gate_frac()  const override { return gate.value; }
    float  velocity()   const override { return vel.value; }
    int step_notes(long long /*k*/, int* out) override {
        static const int chords[5][4] = { {0,4,7,-1}, {0,3,7,-1}, {0,4,7,10}, {0,3,7,10}, {0,5,7,-1} };
        int q = quality.int_value(); q = q < 0 ? 0 : (q > 4 ? 4 : q);
        int n = 0;
        for (int i = 0; i < 4; ++i) if (chords[q][i] >= 0) out[n++] = root.int_value() + chords[q][i];
        return n;
    }
    // Thumbnail: the chord as stacked horizontal note bars, higher intervals higher on the surface.
    // ANIMATED: the whole stack flashes bright on each step boundary while the chord sounds (within
    // the gate window), then dims until the next hit — pulsing at the transport rate.
    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        const VividDrawAPI& d = ctx->draw; if (!d.draw_rect) return;
        static const int chords[5][4] = { {0,4,7,-1}, {0,3,7,-1}, {0,4,7,10}, {0,3,7,10}, {0,5,7,-1} };
        const auto pv = [&](int i, float def) { return ctx->param_count > (uint32_t)i ? ctx->param_values[i] : def; };
        int q = (int)std::lround(pv(1, 0.f)); q = q < 0 ? 0 : (q > 4 ? 4 : q);
        const double sb = rate_beats((int)std::lround(pv(2, 2.f)));   // beats per step
        const double gt = pv(3, 0.85f);
        const double frac = sb > 0.0 ? (ctx->time / sb - std::floor(ctx->time / sb)) : 1.0;   // 0..1 in step
        const bool sounding = frac < gt;
        const float w = ctx->surface_width, h = ctx->surface_height;
        VividColor c = ctx->accent; c.a *= sounding ? 1.0f : 0.5f;   // flash on the hit, dim between
        const float bw = w * (sounding ? 0.72f : 0.58f), bx = (w - bw) * 0.5f, bh = std::max(2.0f, h * 0.13f);
        for (int i = 0; i < 4; ++i) {
            if (chords[q][i] < 0) continue;
            const float t = (float)chords[q][i] / 12.0f;                 // 0..~0.83 within an octave
            const float y = h - h * (0.12f + 0.72f * t) - bh * 0.5f;     // higher interval -> higher up
            d.draw_rect(d.opaque, bx, y, bw, bh, c);
        }
    }
};

// RandMelody: one in-scale note per step, fired with probability `density`. The RNG is seeded by
// (seed, step index) — NOT free-running — so relaunching the scene at the same transport position
// reproduces the same melody (required for A/B + undo/redo determinism).
struct RandMelodyOp : NoteGenBase {
    static constexpr const char* kDisplayName = "RandMelody";
    static constexpr const char* kSummary = "Generator: a deterministic in-scale random melody, locked to the transport.";
    static constexpr std::array<const char*, 4> kKeywords = {"audio", "note", "generator", "random"};
    Param<int>   root{"root", 48, 0, 127};
    Param<int>   scale{"scale", 0, {"Major", "Minor", "PentMin", "Dorian"}};
    Param<int>   octaves{"octaves", 2, 1, 4};
    Param<int>   rate{"rate", 4, {"1/1", "1/2", "1/4", "1/8", "1/16", "1/8T"}};
    Param<float> density{"density", 0.7f, 0.f, 1.f};
    Param<float> gate{"gate", 0.6f, 0.05f, 1.0f};
    Param<int>   seed{"seed", 1, 0, 9999};
    RandMelodyOp() { id_base_ = 930000; }
    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&root); o.push_back(&scale); o.push_back(&octaves); o.push_back(&rate);
        o.push_back(&density); o.push_back(&gate); o.push_back(&seed);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        VividPortDescriptor p{}; p.name = "output"; p.type = VIVID_PORT_SCALAR;   // ADR-0047: transport is the real type
        p.direction = VIVID_PORT_OUTPUT; p.value_type = VIVID_VALUE_FLOAT;
        p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
        p.semantic_shape = "note_stream"; p.transport = VIVID_PORT_TRANSPORT_NOTE_STREAM;   // ADR-0047: real stream = notes
        o.push_back(p);
    }
    double step_beats() const override { return rate_beats(rate.int_value()); }
    double gate_frac()  const override { return gate.value; }
    float  velocity()   const override { return 0.8f; }
    int step_notes(long long k, int* out) override {
        uint32_t h = static_cast<uint32_t>(seed.int_value()) * 2654435761u
                   ^ static_cast<uint32_t>(k * 40503LL);          // seed + step → deterministic
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        if (static_cast<double>(h & 0xffffu) / 65535.0 > density.value) return 0;   // a rest
        static const int scales[4][7] = { {0,2,4,5,7,9,11}, {0,2,3,5,7,8,10}, {0,3,5,7,10,-1,-1}, {0,2,3,5,7,9,10} };
        static const int lens[4] = { 7, 7, 5, 7 };
        int sc = scale.int_value(); sc = sc < 0 ? 0 : (sc > 3 ? 3 : sc);
        const int len = lens[sc];
        const int oc = octaves.int_value() < 1 ? 1 : octaves.int_value();
        const int deg = static_cast<int>((h >> 16) % static_cast<uint32_t>(len * oc));
        out[0] = root.int_value() + scales[sc][deg % len] + 12 * (deg / len);
        return 1;
    }
    // Thumbnail: a wandering note-line — the SAME deterministic per-step hash the generator fires.
    // ANIMATED: a SCROLLING piano-roll window ending at the current transport step (ctx->time), so
    // new notes stream in from the right as the generator improvises; the newest note (the one
    // currently sounding, within gate) flashes bright. density + octaves shape it.
    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        const VividDrawAPI& d = ctx->draw; if (!d.draw_rect) return;
        const auto pv = [&](int i, float def) { return ctx->param_count > (uint32_t)i ? ctx->param_values[i] : def; };
        int sc = (int)std::lround(pv(1, 0.f)); sc = sc < 0 ? 0 : (sc > 3 ? 3 : sc);
        const int   oc   = std::max(1, (int)std::lround(pv(2, 2.f)));
        const float dens = pv(4, 0.7f);
        const int   sd   = (int)std::lround(pv(6, 1.f));
        const double sb  = rate_beats((int)std::lround(pv(3, 4.f)));   // beats per step
        const double gt  = pv(5, 0.6f);
        const long long curk = sb > 0.0 ? (long long)std::floor(ctx->time / sb) : 0;
        const double frac = sb > 0.0 ? (ctx->time / sb - (double)curk) : 1.0;
        static const int lens[4] = { 7, 7, 5, 7 };
        const int span = std::max(1, lens[sc] * oc);
        const float w = ctx->surface_width, h = ctx->surface_height;
        const VividColor on = ctx->accent, off = { 0.42f, 0.44f, 0.48f, 0.4f };
        const VividColor hot = { 1.f, 1.f, 1.f, 1.f };
        const int N = 14;
        const float s = std::max(2.0f, std::min(w, h) * 0.09f);
        float px = -1.f, py = -1.f;
        for (int i = 0; i < N; ++i) {
            const long long step = curk - (N - 1) + i;    // scrolling window: rightmost = current step
            const float x = w * (0.08f + 0.84f * (N > 1 ? (float)i / (float)(N - 1) : 0.f));
            uint32_t hh = static_cast<uint32_t>(sd) * 2654435761u ^ static_cast<uint32_t>(step * 40503LL);
            hh ^= hh >> 15; hh *= 2246822519u; hh ^= hh >> 13; hh *= 3266489917u; hh ^= hh >> 16;
            if (static_cast<float>(hh & 0xffffu) / 65535.0f > dens) {           // a rest — baseline tick
                d.draw_rect(d.opaque, x - 1.0f, h * 0.5f - 0.75f, 2.0f, 1.5f, off); px = -1.f; continue;
            }
            const int deg = static_cast<int>((hh >> 16) % static_cast<uint32_t>(span));
            const float y = h - h * (0.14f + 0.72f * ((float)deg / (float)span));
            const bool active = (i == N - 1) && frac < gt;                      // newest note, sounding
            if (px >= 0.f && d.draw_line) d.draw_line(d.opaque, px, py, x, y, 1.5f, on);
            const float sz = active ? s * 1.6f : s;
            d.draw_rect(d.opaque, x - sz * 0.5f, y - sz * 0.5f, sz, sz, active ? hot : on);
            px = x; py = y;
        }
    }
};

void register_glitch_ops(OpRegistry& reg);   // audio/glitch/glitch_ops.cpp

void register_builtin_audio_ops(OpRegistry& reg) {
    register_op<BitcrushOp>(reg, "Bitcrush");
    register_op<StateVariableFilterOp>(reg, "SVFilter");
    register_op<TestToneOp>(reg, "TestTone");
    register_op<MovieAudioOp>(reg, "MovieAudio");   // audio track of a Video movie (drains the movie-audio bus)
    register_op<SamplerOp>(reg, "Sampler");
    // ADR-0047: note/control roles now come from each op's declared_audio_role() (in its descriptor),
    // not a host-side name table — so built-in and loaded-dylib ops classify through the identical path.
    register_op<ArpOp>(reg, "Arp");         // ADR-0015: the first note effect (notes in -> notes out)
    register_op<LfoOp>(reg, "LFO");         // ADR-0022: the first modulator (emits a control signal)
    register_op<AdsrOp>(reg, "ADSR");       // a note-gated envelope modulator (control_out -> node_<t>_<n>.ctl)
    register_op<EuclidOp>(reg, "Euclid");   // ADR-0022 P3.3: note generators (sources, not instruments)
    register_op<ChordOp>(reg, "Chord");
    register_op<RandMelodyOp>(reg, "RandMelody");
    register_glitch_ops(reg);
}

}  // namespace vivid
