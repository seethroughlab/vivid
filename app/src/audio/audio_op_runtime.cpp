#include "audio/audio_op_runtime.h"
#include "app/crash_guard.h"   // ADR-0018: attribute an operator crash (RT-safe)

#include <set>

#include "audio/sampler_op.h"        // SamplerLoadable (RTTI cross-cast target)
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
    NoteFlushable*      nf = nullptr;      // ADR-0022 P3.3: cast once; null unless a generator implements it
    bool                is_source = false; // no audio-input port
    std::string         type;

    std::unique_ptr<std::atomic<float>[]> pvals;   // UI-set param values (lock-free)
    // ADR-0030 Phase 2: the frame-side bridge's non-destructive override. `fovr_on[i]` gates it: when
    // set, the render uses `fovr[i]` as the param's EFFECTIVE base (an absolute automation value the
    // control edges still modulate on top) instead of `pvals[i]`. `pvals` — the authored base — is
    // never written, so disconnecting a mapping just clears `fovr_on[i]` and the knob returns to base.
    // Same lock-free single-UI-producer discipline as `pvals`.
    std::unique_ptr<std::atomic<float>[]>   fovr;
    std::unique_ptr<std::atomic<uint8_t>[]> fovr_on;
    int                 nparams = 0;
    std::vector<float>  pscratch;                  // param_values for the context
    std::vector<float>  in_planar, out_planar;     // [2 * kMaxBlock] planar stereo
    std::vector<VividNoteEvent> nscratch;          // preallocated note-event scratch (input)
    std::vector<VividNoteEvent> noutscratch;       // preallocated note-event scratch (output, v12)
};

AudioOp* audio_op_create(OpRegistry& reg, const char* type_name) {
    if (!type_name || !*type_name) return nullptr;
    // Gate on the descriptor's declared audio capability, not just the C++ cast: the loaded-dylib
    // adapter implements AudioProcessable even for gpu/frame ops (the cast would wrongly succeed),
    // so a non-audio op must be rejected by its has_process_audio flag.
    const VividOperatorDescriptor* d = reg.descriptor_for(type_name);
    if (!d || !d->has_process_audio) return nullptr;
    std::vector<DescriptorValidationIssue> issues;
    auto opt = reg.create(type_name, issues);
    if (!opt) return nullptr;
    auto* ap = dynamic_cast<AudioProcessable*>(opt->op.get());
    if (!ap) return nullptr;   // not an audio operator

    auto* a = new AudioOp();
    a->inst = std::move(*opt);
    a->ap = dynamic_cast<AudioProcessable*>(a->inst.op.get());   // re-resolve after move
    a->nf = dynamic_cast<NoteFlushable*>(a->inst.op.get());      // ADR-0022 P3.3: null unless a generator
    a->type = type_name;
    a->nparams = static_cast<int>(a->inst.param_ptrs.size());

    // A source has no audio-input port (instrument or generator).
    bool has_audio_in = false;
    for (const auto& p : a->inst.ports)
        if (p.type == VIVID_PORT_AUDIO_BUFFER && p.direction == VIVID_PORT_INPUT) has_audio_in = true;
    a->is_source = !has_audio_in;

    a->pvals.reset(new std::atomic<float>[a->nparams]);
    a->fovr.reset(new std::atomic<float>[a->nparams]);
    a->fovr_on.reset(new std::atomic<uint8_t>[a->nparams]);
    for (int i = 0; i < a->nparams; ++i) {
        a->pvals[i].store(a->inst.param_ptrs[i]->value, std::memory_order_relaxed);   // seed with defaults
        a->fovr[i].store(0.f, std::memory_order_relaxed);
        a->fovr_on[i].store(0u, std::memory_order_relaxed);   // no bridge override until one is delivered
    }
    a->pscratch.assign(a->nparams > 0 ? a->nparams : 1, 0.f);
    a->in_planar.assign(2 * kMaxBlock, 0.f);
    a->out_planar.assign(2 * kMaxBlock, 0.f);
    a->nscratch.resize(kMaxNotes);
    a->noutscratch.resize(kMaxNotes);
    return a;
}

void audio_op_destroy(AudioOp* a) { delete a; }

bool audio_op_load_sampler(AudioOp* a, const float* L, const float* R, size_t n, uint32_t sr,
                           const uint32_t* starts, const uint32_t* ends, int nslices, int base_note) {
    if (!a || !L || n == 0) return false;
    auto* sl = dynamic_cast<SamplerLoadable*>(a->inst.op.get());   // cross-cast to the escape hatch
    if (!sl) return false;
    sl->load_pcm(L, R, n, sr, starts, ends, nslices, base_note);
    return true;
}

int audio_op_sampler_peaks(const AudioOp* a, float* out, int n) {
    if (!a || !out || n <= 0) return 0;
    auto* sp = dynamic_cast<const SamplerPreviewable*>(a->inst.op.get());   // read side of the escape hatch
    return sp ? sp->copy_peaks(out, n) : 0;
}

float audio_op_sampler_playhead(const AudioOp* a) {
    if (!a) return -1.f;
    auto* sp = dynamic_cast<const SamplerPreviewable*>(a->inst.op.get());
    return sp ? sp->playhead() : -1.f;
}

// ADR-0049: the richer read side (sample geometry + slice→note map + source identity).
bool audio_op_sampler_info(const AudioOp* a, SamplerInfo& out) {
    if (!a) return false;
    auto* si = dynamic_cast<const SamplerInspectable*>(a->inst.op.get());
    return si ? si->sample_info(out) : false;
}
int audio_op_sampler_slices(const AudioOp* a, SamplerSlice* out, int cap) {
    if (!a) return 0;
    auto* si = dynamic_cast<const SamplerInspectable*>(a->inst.op.get());
    return si ? si->slices(out, cap) : 0;
}
const char* audio_op_sampler_source(const AudioOp* a) {
    if (!a) return "";
    auto* si = dynamic_cast<const SamplerInspectable*>(a->inst.op.get());
    return si ? si->source_path() : "";
}
void audio_op_set_sampler_source(AudioOp* a, const char* path) {
    if (!a) return;
    auto* sl = dynamic_cast<SamplerLoadable*>(a->inst.op.get());
    if (sl) sl->set_source_path(path);
}

// Registry inspection (UI thread) — enumerate audio operators for the device pickers.
// want_source: true = instruments/generators (no audio input), false = effects.
static bool descriptor_is_source(const VividOperatorDescriptor* d) {
    for (uint32_t i = 0; i < d->port_count; ++i)
        if (d->ports[i].type == VIVID_PORT_AUDIO_BUFFER && d->ports[i].direction == VIVID_PORT_INPUT) return false;
    return true;
}
// v14 effective-role helpers (defined below, after the three mark sets exist) — a built-in's name
// mark OR a loaded dylib's declared audio_role. Forward-declared here so the note/mod/gen
// enumerations that follow can use them.
static bool role_is_note(const std::string& nm, const VividOperatorDescriptor* d);
static bool role_is_mod (const std::string& nm, const VividOperatorDescriptor* d);
static bool role_is_gen (const std::string& nm, const VividOperatorDescriptor* d);
int audio_note_op_count(OpRegistry& reg) {
    int n = 0;
    for (const auto& nm : reg.type_names()) if (role_is_note(nm, reg.descriptor_for(nm))) ++n;
    return n;
}
// Returns the REGISTRY-OWNED name, not `nm.c_str()`: type_names() hands back a vector BY VALUE, so
// returning a pointer into one of its strings dangles the moment this function returns. It read
// correctly for as long as the freed bytes happened to survive — until a caller allocated between
// the call and the read. (audio_op_registry_name below always did this correctly.)
const char* audio_note_op_name(OpRegistry& reg, int idx) {
    int n = 0;
    for (const auto& nm : reg.type_names()) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);
        if (!role_is_note(nm, d) || n++ != idx) continue;
        return d && d->name ? d->name : "";
    }
    return "";
}
int audio_mod_op_count(OpRegistry& reg) {
    int n = 0;
    for (const auto& nm : reg.type_names()) if (role_is_mod(nm, reg.descriptor_for(nm))) ++n;
    return n;
}
const char* audio_mod_op_name(OpRegistry& reg, int idx) {
    int n = 0;
    for (const auto& nm : reg.type_names()) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);   // registry-owned; nm dangles
        if (!role_is_mod(nm, d) || n++ != idx) continue;
        return d && d->name ? d->name : "";
    }
    return "";
}
int audio_gen_op_count(OpRegistry& reg) {
    int n = 0;
    for (const auto& nm : reg.type_names()) if (role_is_gen(nm, reg.descriptor_for(nm))) ++n;
    return n;
}
const char* audio_gen_op_name(OpRegistry& reg, int idx) {
    int n = 0;
    for (const auto& nm : reg.type_names()) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);   // registry-owned; nm dangles
        if (!role_is_gen(nm, d) || n++ != idx) continue;
        return d && d->name ? d->name : "";
    }
    return "";
}

// v14: effective role = a built-in's name mark (audio_op_mark_*) OR a loaded dylib's declared
// audio_role (from vivid_audio_role, recorded in the host descriptor). Honoring EITHER makes a
// project-shipped audio op first-class: it lands in the same generator / note-fx / modulator lists
// as a built-in instead of being mis-offered as a plain instrument.
// ADR-0047: role comes solely from the descriptor's declared audio_role (built-ins now override
// declared_audio_role(); dylibs export vivid_audio_role) — the audio_op_mark_* name tables are retired.
static bool role_is_note(const std::string& nm, const VividOperatorDescriptor* d) {
    (void)nm; return d && d->audio_role == VIVID_AUDIO_ROLE_NOTE_EFFECT;
}
static bool role_is_mod(const std::string& nm, const VividOperatorDescriptor* d) {
    (void)nm; return d && d->audio_role == VIVID_AUDIO_ROLE_MODULATOR;
}
static bool role_is_gen(const std::string& nm, const VividOperatorDescriptor* d) {
    (void)nm; return d && d->audio_role == VIVID_AUDIO_ROLE_GENERATOR;
}
// Registry-aware public overloads (v14): resolve the descriptor so a loaded dylib's declared role
// counts, not just the built-in name marks.
bool audio_op_is_note_op(OpRegistry& reg, const std::string& name) { return role_is_note(name, reg.descriptor_for(name)); }
bool audio_op_is_mod_op (OpRegistry& reg, const std::string& name) { return role_is_mod (name, reg.descriptor_for(name)); }
bool audio_op_is_gen_op (OpRegistry& reg, const std::string& name) { return role_is_gen (name, reg.descriptor_for(name)); }

int audio_op_registry_count(OpRegistry& reg, bool want_source) {
    int n = 0;
    for (const auto& nm : reg.type_names()) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);
        if (!d || !d->has_process_audio) continue;
        if (role_is_note(nm, d)) continue;   // a note effect is not an instrument
        if (role_is_mod(nm, d)) continue;    // ...nor is a modulator (ADR-0022)
        if (role_is_gen(nm, d)) continue;    // ...nor is a note generator (ADR-0022 P3.3)
        if (descriptor_is_source(d) == want_source) ++n;
    }
    return n;
}
const char* audio_op_registry_name(OpRegistry& reg, bool want_source, int idx) {
    if (idx < 0) return "";
    int n = 0;
    for (const auto& nm : reg.type_names()) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);
        if (!d || !d->has_process_audio) continue;
        if (role_is_note(nm, d)) continue;   // excluded: a note effect is not an instrument
        if (role_is_mod(nm, d)) continue;    // ...nor is a modulator (ADR-0022)
        if (role_is_gen(nm, d)) continue;    // ...nor is a note generator (ADR-0022 P3.3)
        if (descriptor_is_source(d) != want_source) continue;
        if (n == idx) return d->name ? d->name : "";   // descriptor name is stable (registry-owned)
        ++n;
    }
    return "";
}

uint32_t audio_op_role(OpRegistry& reg, const char* name) {
    if (!name || !*name) return VIVID_OP_ROLE_DEFAULT;
    const VividOperatorDescriptor* d = reg.descriptor_for(name);
    return d ? d->role : VIVID_OP_ROLE_DEFAULT;
}

const char* audio_op_type(const AudioOp* a)   { return a ? a->type.c_str() : ""; }
bool audio_op_is_source(const AudioOp* a)     { return a && a->is_source; }
int  audio_op_param_count(const AudioOp* a)   { return a ? a->nparams : 0; }

const char* audio_op_param_name(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return "";
    const char* n = a->inst.param_ptrs[i]->name;
    return n ? n : "";
}
int audio_op_param_hint(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return 0;   // VIVID_DISPLAY_DEFAULT
    return (int)a->inst.param_ptrs[i]->display_hint;
}
float audio_op_param_get(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return 0.f;
    return a->pvals[i].load(std::memory_order_relaxed);
}
float audio_op_param_min(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return 0.f;
    return a->inst.param_ptrs[i]->min_value;
}
float audio_op_param_max(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return 1.f;
    return a->inst.param_ptrs[i]->max_value;
}
void audio_op_param_set(AudioOp* a, int i, float v) {
    if (!a || i < 0 || i >= a->nparams) return;
    a->pvals[i].store(v, std::memory_order_relaxed);
}
// ADR-0030 Phase 2: the frame bridge's non-destructive delivery. set makes `v` the param's effective
// base for the render (the authored `pvals` is untouched); clear removes it so the op returns to the
// authored base. Same lock-free single-UI-producer contract as audio_op_param_set.
void audio_op_param_override_set(AudioOp* a, int i, float v) {
    if (!a || i < 0 || i >= a->nparams) return;
    a->fovr[i].store(v, std::memory_order_relaxed);
    a->fovr_on[i].store(1u, std::memory_order_release);   // publish value before the gate
}
void audio_op_param_override_clear(AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return;
    a->fovr_on[i].store(0u, std::memory_order_relaxed);
}
// The EFFECTIVE base the render should start from: the bridge override when active, else the authored
// base. Reads the gate with acquire so a set's value is visible once the gate reads 1.
float audio_op_param_effective(const AudioOp* a, int i) {
    if (!a || i < 0 || i >= a->nparams) return 0.f;
    if (a->fovr_on[i].load(std::memory_order_acquire)) return a->fovr[i].load(std::memory_order_relaxed);
    return a->pvals[i].load(std::memory_order_relaxed);
}

void audio_op_process(AudioOp* a, float* L, float* R, uint32_t frames, uint32_t sr,
                      float bpm, uint32_t bpb, double beats,
                      const session::NoteEvent* notes, uint32_t note_count,
                      session::NoteEvent* note_out, uint32_t note_out_cap, uint32_t* note_out_n,
                      const AudioOpParamOverride* overrides, uint32_t override_count,
                      float* control_out, uint32_t control_out_cap) {
    if (!a || !a->ap || frames == 0 || frames > kMaxBlock) return;

    // Pull the latest UI-set param values into the op's Param<> members + the context array. The
    // starting point is the EFFECTIVE base (ADR-0030 Phase 2): the frame-bridge override when active,
    // else `pvals` — the user's authored value. Neither `pvals` nor `fovr` is written on this thread.
    for (int i = 0; i < a->nparams; ++i) {
        const float v = a->fovr_on[i].load(std::memory_order_acquire)
                            ? a->fovr[i].load(std::memory_order_relaxed)
                            : a->pvals[i].load(std::memory_order_relaxed);
        a->pscratch[i] = v;
        a->inst.param_ptrs[i]->value = v;
    }
    // ADR-0022: lay this block's control-driven values ON TOP of the base. The host already
    // resolved base + modulation (control_resolve); we only place the result where the op reads.
    // Because this writes pscratch / Param<>::value and NOT pvals, the base survives untouched —
    // which is what lets a knob stay draggable under modulation and needs nothing restored on
    // disconnect. Empty for every node in a graph with no control edges.
    for (uint32_t k = 0; k < override_count; ++k) {
        const int p = overrides[k].param;
        if (p < 0 || p >= a->nparams) continue;
        a->pscratch[p] = overrides[k].value;
        a->inst.param_ptrs[p]->value = overrides[k].value;
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

    // Note OUTPUT (v12, ADR-0015): a note effect writes the notes it wants downstream. The host
    // owns the buffer; the operator appends up to the capacity and sets the count.
    uint32_t nout = 0;
    if (note_out && note_out_cap > 0) {
        const uint32_t cap = note_out_cap < kMaxNotes ? note_out_cap : kMaxNotes;
        ctx.note_out = a->noutscratch.data();
        ctx.note_out_capacity = cap;
        ctx.note_out_count = &nout;
    }

    // Control OUTPUT (v13, ADR-0022): a modulator writes its normalized 0..1 signal. Plain floats,
    // so unlike notes there is no scratch/conversion hop — the operator writes the host's buffer
    // directly. Zeroed first, so an operator that ignores it (every op built before v13, and every
    // non-modulator) leaves silence rather than last block's signal.
    if (control_out && control_out_cap > 0) {
        const uint32_t cap = control_out_cap < frames ? control_out_cap : frames;
        std::memset(control_out, 0, sizeof(float) * cap);
        ctx.control_out = control_out;
        ctx.control_out_capacity = cap;
    }

    { vivid::CrashGuard cg(a->type.c_str());   // ADR-0018: attribute a crash (RT-safe pointer store)
      a->ap->process_audio(&ctx); }

    if (note_out && note_out_cap > 0 && note_out_n) {          // hand the emitted notes back
        const uint32_t m = nout < note_out_cap ? nout : note_out_cap;
        for (uint32_t i = 0; i < m; ++i) {
            const VividNoteEvent& e = a->noutscratch[i];
            note_out[i] = session::NoteEvent{ e.sample_offset, e.on != 0, static_cast<int>(e.pitch),
                                              e.velocity, e.note_id, e.tuning };
        }
        *note_out_n = m;
    }

    std::memcpy(L, outp, sizeof(float) * frames);
    std::memcpy(R, outp + frames, sizeof(float) * frames);
}

// ADR-0022 P3.3: flush a generator's currently-sounding voices as note-offs (via NoteFlushable).
// RT-safe: the op writes into the preallocated VividNoteEvent scratch, converted to session events
// here (same hop as note_out in audio_op_process). count=0 for any op without NoteFlushable (nf null).
void audio_op_note_flush(AudioOp* a, session::NoteEvent* out, uint32_t cap, uint32_t* count) {
    if (count) *count = 0;
    if (!a || !a->nf || !out || cap == 0) return;
    const uint32_t c = cap < kMaxNotes ? cap : kMaxNotes;
    uint32_t nout = 0;
    a->nf->note_flush(a->noutscratch.data(), c, &nout);
    const uint32_t m = nout < c ? nout : c;
    for (uint32_t i = 0; i < m; ++i) {
        const VividNoteEvent& e = a->noutscratch[i];
        out[i] = session::NoteEvent{ e.sample_offset, e.on != 0, static_cast<int>(e.pitch),
                                     e.velocity, e.note_id, e.tuning };
    }
    if (count) *count = m;
}

void audio_op_draw_thumbnail(AudioOp* a, const ::VividThumbnailContext* ctx) {
    if (!a || !ctx) return;
    if (OperatorBase* op = a->inst.op.get()) op->draw_thumbnail(ctx);   // base no-op unless overridden
}

}  // namespace vivid
