#include "audio/audio_op_runtime.h"

#include "gpu/op_runtime.h"          // OpRegistry / OpInstance / sync (includes operator_api)
#include "midi/midi_clip.h"          // vivid::session::NoteEvent
#include "operator_api/metronome_sync.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace vivid {

namespace {
constexpr uint32_t kMaxBlock = 4096;   // matches ClipDsp
constexpr uint32_t kMaxNotes = 512;
}

struct AudioOp {
    OpInstance          inst;
    AudioProcessable*   ap = nullptr;      // cast once at create (RT: no dynamic_cast in process)
    bool                is_source = false; // no audio-input port
    std::string         type;

    std::unique_ptr<std::atomic<float>[]> pvals;   // UI-set param values (lock-free)
    int                 nparams = 0;
    std::vector<float>  pscratch;                  // param_values for the context
    std::vector<float>  in_planar, out_planar;     // [2 * kMaxBlock] planar stereo
    std::vector<VividNoteEvent> nscratch;          // preallocated note-event scratch
};

AudioOp* audio_op_create(OpRegistry& reg, const char* type_name) {
    if (!type_name || !*type_name) return nullptr;
    std::vector<DescriptorValidationIssue> issues;
    auto opt = reg.create(type_name, issues);
    if (!opt) return nullptr;
    auto* ap = dynamic_cast<AudioProcessable*>(opt->op.get());
    if (!ap) return nullptr;   // not an audio operator

    auto* a = new AudioOp();
    a->inst = std::move(*opt);
    a->ap = dynamic_cast<AudioProcessable*>(a->inst.op.get());   // re-resolve after move
    a->type = type_name;
    a->nparams = static_cast<int>(a->inst.param_ptrs.size());

    // A source has no audio-input port (instrument or generator).
    bool has_audio_in = false;
    for (const auto& p : a->inst.ports)
        if (p.type == VIVID_PORT_AUDIO_BUFFER && p.direction == VIVID_PORT_INPUT) has_audio_in = true;
    a->is_source = !has_audio_in;

    a->pvals.reset(new std::atomic<float>[a->nparams]);
    for (int i = 0; i < a->nparams; ++i)
        a->pvals[i].store(a->inst.param_ptrs[i]->value, std::memory_order_relaxed);   // seed with defaults
    a->pscratch.assign(a->nparams > 0 ? a->nparams : 1, 0.f);
    a->in_planar.assign(2 * kMaxBlock, 0.f);
    a->out_planar.assign(2 * kMaxBlock, 0.f);
    a->nscratch.resize(kMaxNotes);
    return a;
}

void audio_op_destroy(AudioOp* a) { delete a; }

const char* audio_op_type(const AudioOp* a)   { return a ? a->type.c_str() : ""; }
bool audio_op_is_source(const AudioOp* a)     { return a && a->is_source; }
int  audio_op_param_count(const AudioOp* a)   { return a ? a->nparams : 0; }

const char* audio_op_param_name(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return "";
    const char* n = a->inst.param_ptrs[i]->name;
    return n ? n : "";
}
float audio_op_param_get(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return 0.f;
    return a->pvals[i].load(std::memory_order_relaxed);
}
void audio_op_param_set(AudioOp* a, int i, float v) {
    if (!a || i < 0 || i >= a->nparams) return;
    a->pvals[i].store(v, std::memory_order_relaxed);
}

void audio_op_process(AudioOp* a, float* L, float* R, uint32_t frames, uint32_t sr,
                      float bpm, uint32_t bpb, double beats,
                      const session::NoteEvent* notes, uint32_t note_count) {
    if (!a || !a->ap || frames == 0 || frames > kMaxBlock) return;

    // Pull the latest UI-set param values into the op's Param<> members + the context array.
    for (int i = 0; i < a->nparams; ++i) {
        const float v = a->pvals[i].load(std::memory_order_relaxed);
        a->pscratch[i] = v;
        a->inst.param_ptrs[i]->value = v;
    }

    float* inp = a->in_planar.data();
    float* outp = a->out_planar.data();
    std::memset(outp, 0, sizeof(float) * 2 * frames);

    VividAudioContext ctx{};
    ctx.buffer_size = frames;
    ctx.sample_rate = sr;
    ctx.param_values = a->pscratch.empty() ? nullptr : a->pscratch.data();
    ctx.metronome_bpm = bpm;
    ctx.metronome_beats_per_bar = bpb ? bpb : 4u;
    ctx.metronome_beats_elapsed = beats;
    const double bp = beats - std::floor(beats);
    ctx.metronome_beat_phase = static_cast<float>(bp);
    ctx.metronome_bar_phase  = static_cast<float>(std::fmod(beats, ctx.metronome_beats_per_bar) / ctx.metronome_beats_per_bar);
    ctx.metronome_beat_ms    = bpm > 0.f ? 60000.f / bpm : 0.f;

    // Output port 0 = stereo, planar [ch*frames + i].
    float* outb[1] = { outp };
    uint8_t outc[1] = { 2 };
    ctx.output_buffers = outb;
    ctx.output_channel_counts = outc;

    float* inb[1] = { inp };
    uint8_t inc[1] = { 2 };
    if (!a->is_source) {                          // effect: feed L/R as the input port
        std::memcpy(inp, L, sizeof(float) * frames);
        std::memcpy(inp + frames, R, sizeof(float) * frames);
        ctx.input_buffers = inb;
        ctx.input_channel_counts = inc;
    }

    if (a->is_source && notes && note_count) {    // instrument: hand over the block's notes
        const uint32_t m = note_count < kMaxNotes ? note_count : kMaxNotes;
        for (uint32_t i = 0; i < m; ++i) {
            const session::NoteEvent& n = notes[i];
            a->nscratch[i] = VividNoteEvent{ n.sample_offset, static_cast<uint8_t>(n.on ? 1 : 0),
                                             static_cast<int16_t>(n.pitch), n.vel, n.note_id, n.tuning };
        }
        ctx.note_events = a->nscratch.data();
        ctx.note_event_count = m;
    }

    a->ap->process_audio(&ctx);

    std::memcpy(L, outp, sizeof(float) * frames);
    std::memcpy(R, outp + frames, sizeof(float) * frames);
}

}  // namespace vivid
