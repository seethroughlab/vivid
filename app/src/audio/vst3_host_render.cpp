// ADR-0025 (vst3_host split, PR-B): the per-source/effect RENDER PRIMITIVES, extracted verbatim from
// vst3_host.cpp. Each runs one VST3/CLAP plugin for one block (or drains the notes it generated) —
// pure DSP over a handle, with no session/graph engine state. process_step + session_process (both
// still in vst3_host.cpp) call these through the declarations in vst3_host_internal.h, so the inline
// path and the audio-graph node dispatch share identical code (parity by construction). All RT-safe:
// fixed-capacity scratch, no heap, no locks. Pure code move — behaviour unchanged. Only possible now
// that PR-A gave the host types (Vst3Handle/Vst3EventList/...) external linkage.
#include "audio/vst3_host_internal.h"                 // Track, NoteEvent/ExprEvent, Vst3/ClapHandle, kGraphMaxNotes, Steinberg using-directives
#include "audio/plugin_crash_name.h"                  // plugin_crash_name (ADR-0045 P0-01)
#include "app/crash_guard.h"                          // ADR-0018: attribute a plugin crash (RT-safe pointer store)
#include "pluginterfaces/vst/ivstnoteexpression.h"    // kTuningTypeID / kBrightnessTypeID (note-expression axes)

#include <vector>
#include <cstring>
#include <algorithm>

namespace vivid::session {

// Drain a device's pending UI parameter changes into its ParamChanges block. File-local: only the
// two VST3 render primitives below feed a plugin's param queue, so it stays out of the shared header.
static void drain_params(Vst3Handle* h, Vst3ParamChanges& pc) {
    ParamMsg m;
    while (h->param_q.pop(m)) pc.add(m.id, m.value);   // sets id AND value (addParameterData+addPoint left value unset)
}

// Note on/off + per-note expression. Note events are added first so a same-offset
// expression for a just-started note never precedes its note-on (VST3 wants the list
// sorted; continuing-note expression is at offset 0 with its note-on in a prior block).
// Axis mapping: bend -> kTuningTypeID (±120 semis, norm = semis/240 + 0.5), timbre ->
// kBrightnessTypeID (0..1), pressure -> per-note PolyPressureEvent (0..1).
void emit_vst3(Vst3EventList& events, const std::vector<NoteEvent>& nev,
               const std::vector<ExprEvent>& eev) {
    for (const NoteEvent& ne : nev) {
        Event e{};
        e.sampleOffset = static_cast<int32>(ne.sample_offset);
        e.busIndex = 0;
        if (ne.on) {
            e.type = Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<int16>(ne.pitch);
            e.noteOn.velocity = ne.vel;
            e.noteOn.noteId = ne.note_id;
            e.noteOn.channel = 0;
            e.noteOn.tuning = ne.tuning;   // semitone offset for a click-free bent start
        } else {
            e.type = Event::kNoteOffEvent;
            e.noteOff.pitch = static_cast<int16>(ne.pitch);
            e.noteOff.velocity = 0.f;
            e.noteOff.noteId = ne.note_id;
            e.noteOff.channel = 0;
        }
        events.addEvent(e);
    }
    for (const ExprEvent& xe : eev) {
        Event e{};
        e.sampleOffset = static_cast<int32>(xe.sample_offset);
        e.busIndex = 0;
        if (xe.axis == vivid::session::AXIS_PRESSURE) {
            e.type = Event::kPolyPressureEvent;
            e.polyPressure.channel = 0;
            e.polyPressure.pitch = static_cast<int16>(xe.pitch);
            e.polyPressure.pressure = std::clamp(xe.value, 0.f, 1.f);
            e.polyPressure.noteId = xe.note_id;
        } else {
            e.type = Event::kNoteExpressionValueEvent;
            e.noteExpressionValue.noteId = xe.note_id;
            if (xe.axis == vivid::session::AXIS_BEND) {
                e.noteExpressionValue.typeId = kTuningTypeID;
                e.noteExpressionValue.value = std::clamp(xe.value / 240.0 + 0.5, 0.0, 1.0);
            } else {  // AXIS_TIMBRE
                e.noteExpressionValue.typeId = kBrightnessTypeID;
                e.noteExpressionValue.value = std::clamp(static_cast<double>(xe.value), 0.0, 1.0);
            }
        }
        events.addEvent(e);
    }
}

// Copy only the note/expr events whose pitch is within [lo,hi] into dst — the key-range router.
// RT-safe: dst is pre-reserved (reserve_track_graph). note-on and note-off both carry pitch, so a
// filtered-in note's off is filtered in too — on/off pairs stay balanced (no stuck notes).
void filter_notes_by_range(const std::vector<NoteEvent>& src, uint8_t lo, uint8_t hi,
                           std::vector<NoteEvent>& dst) {
    dst.clear();
    for (const NoteEvent& n : src) if (n.pitch >= lo && n.pitch <= hi) dst.push_back(n);
}
void filter_expr_by_range(const std::vector<ExprEvent>& src, uint8_t lo, uint8_t hi,
                          std::vector<ExprEvent>& dst) {
    dst.clear();
    for (const ExprEvent& x : src) if (x.pitch >= lo && x.pitch <= hi) dst.push_back(x);
}

// VST3 instrument source. Runs the processor into L/R using the caller-supplied `events` list.
// For a full-range source the caller passes t.vev (primed with scene-switch releases + this block's
// notes), keeping behavior identical; for a key-split source it passes a filtered per-source list.
void render_vst3_instrument(Track& t, Vst3Handle* h, Vst3EventList& events,
                            const VividAudioContext& ctx, uint32_t frames, float* L, float* R,
                            const ParamMsg* mod, uint32_t mod_n) {
    float* ch[2] = { L, R };
    AudioBusBuffers ob{}; ob.channelBuffers32 = ch; ob.numChannels = 2; ob.silenceFlags = 0;
    Vst3ParamChanges pc; pc.clear();
    drain_params(h, pc);
    for (uint32_t k = 0; k < mod_n; ++k) pc.add(mod[k].id, mod[k].value);   // ADR-0034: modulation wins
    ProcessContext pctx = vst3_build_process_context(&ctx, t.steady);
    ProcessData data{};
    data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
    data.numSamples = static_cast<int32>(frames); data.numInputs = 0; data.numOutputs = 1;
    data.inputs = nullptr; data.outputs = &ob;
    data.inputEvents = &events; data.inputParameterChanges = &pc; data.processContext = &pctx;
    // ADR-0015 (M3): give a note-GENERATING plugin (a chord generator / arpeggiator) somewhere to
    // put the notes it makes. Before this the host never assigned data.outputEvents at all, so every
    // note such a plugin produced was silently discarded.
    if (h->has_note_out) { h->out_events.clear(); data.outputEvents = &h->out_events; }
    { vivid::CrashGuard cg(plugin_crash_name(h->plugin_name, h->vendor, "VST3 plugin"));  // ADR-0045 P0-01
      h->processor->process(data); }
}

// Drain the notes a VST3 plugin GENERATED this block into `out` (ADR-0015 / M3). RT-safe: fixed
// capacity, no allocation. Only note-on/off are taken — the note stream is what the graph carries.
void drain_vst3_notes(Vst3Handle* h, std::vector<NoteEvent>& out) {
    out.clear();
    if (!h || !h->has_note_out) return;
    const int32 n = h->out_events.getEventCount();
    for (int32 i = 0; i < n; ++i) {
        Event e{};
        if (h->out_events.getEvent(i, e) != kResultOk) continue;
        if (out.size() >= kGraphMaxNotes) break;   // truncate rather than allocate
        if (e.type == Event::kNoteOnEvent) {
            out.push_back(NoteEvent{ static_cast<uint32_t>(e.sampleOffset), true, e.noteOn.pitch,
                                     e.noteOn.velocity, e.noteOn.noteId, e.noteOn.tuning });
        } else if (e.type == Event::kNoteOffEvent) {
            out.push_back(NoteEvent{ static_cast<uint32_t>(e.sampleOffset), false, e.noteOff.pitch,
                                     e.noteOff.velocity, e.noteOff.noteId, 0.f });
        }
    }
}

// VST3 effect. Transforms L/R in place, using the track's fx scratch (t.fxl/t.fxr) as the plugin's
// output bus, then copies back. Caller guards `fx && fx->processing`.
void render_vst3_effect(Track& t, Vst3Handle* fx, const VividAudioContext& ctx,
                        uint32_t frames, float* L, float* R,
                        const ParamMsg* mod, uint32_t mod_n) {
    if (t.fxl.size() < frames) { t.fxl.resize(frames); t.fxr.resize(frames); }
    float* oL = t.fxl.data(); float* oR = t.fxr.data();
    float* inCh[2] = { L, R }; float* outCh[2] = { oL, oR };
    AudioBusBuffers ib{}; ib.channelBuffers32 = inCh;  ib.numChannels = 2; ib.silenceFlags = 0;
    AudioBusBuffers fob{}; fob.channelBuffers32 = outCh; fob.numChannels = 2; fob.silenceFlags = 0;
    Vst3ParamChanges fpc; fpc.clear();
    drain_params(fx, fpc);
    for (uint32_t k = 0; k < mod_n; ++k) fpc.add(mod[k].id, mod[k].value);   // ADR-0034: modulation wins
    ProcessContext fpctx = vst3_build_process_context(&ctx, t.steady);
    ProcessData fd{};
    fd.processMode = kRealtime; fd.symbolicSampleSize = kSample32;
    fd.numSamples = static_cast<int32>(frames);
    fd.numInputs = 1; fd.inputs = &ib;
    fd.numOutputs = 1; fd.outputs = &fob;
    fd.inputEvents = nullptr; fd.inputParameterChanges = &fpc; fd.processContext = &fpctx;
    { vivid::CrashGuard cg(plugin_crash_name(fx->plugin_name, fx->vendor, "VST3 plugin"));  // ADR-0045 P0-01
      fx->processor->process(fd); }
    std::memcpy(L, oL, frames * sizeof(float));
    std::memcpy(R, oR, frames * sizeof(float));
}

// CLAP instrument. Builds this block's note + param events into the handle's scratch, then
// processes into L/R (silent input fed to any declared input port). Plays `notes` (t.nev for a
// full-range source, or a key-range-filtered list) plus this block's scene-switch note-offs.
// RT-safe (fixed scratch, no alloc/lock).
void render_clap_instrument(Track& t, ClapHandle* h, const std::vector<NoteEvent>& notes,
                            uint32_t frames, float* L, float* R,
                            const ClapParamMsg* mod, uint32_t mod_n) {
    h->events.clear();
    clap_flush_params(h);
    for (uint32_t k = 0; k < mod_n; ++k) h->events.add_param(mod[k].id, mod[k].value);   // ADR-0034: modulation wins
    for (const NoteEvent& ne : t.scene_rel)   // scene-switch note-offs first, so held voices release
        h->events.add_note(ne.on, ne.pitch, ne.vel, ne.note_id, ne.sample_offset);
    for (const NoteEvent& ne : notes)         // this source's notes (full range = t.nev; key-split = filtered)
        h->events.add_note(ne.on, ne.pitch, ne.vel, ne.note_id, ne.sample_offset);
    float* out[2] = { L, R };
    float* in[2]  = { h->silence.data(), h->silence.data() + h->max_block };
    { vivid::CrashGuard cg(plugin_crash_name(h->name, h->bundle_path, "CLAP plugin"));  // ADR-0045 P0-01
      clap_run(h, static_cast<int64_t>(t.steady), frames, h->audio_in > 0 ? in : nullptr, 2, out, 2); }
}

// CLAP effect. Transforms L/R in place via the track's fx scratch (t.fxl/t.fxr), like the VST3
// effect path. Caller guards `clap && clap->processing`.
void render_clap_effect(Track& t, ClapHandle* h, uint32_t frames, float* L, float* R,
                        const ClapParamMsg* mod, uint32_t mod_n) {
    if (t.fxl.size() < frames) { t.fxl.resize(frames); t.fxr.resize(frames); }
    h->events.clear();
    clap_flush_params(h);
    for (uint32_t k = 0; k < mod_n; ++k) h->events.add_param(mod[k].id, mod[k].value);   // ADR-0034: modulation wins
    float* in[2]  = { L, R };
    float* out[2] = { t.fxl.data(), t.fxr.data() };
    { vivid::CrashGuard cg(plugin_crash_name(h->name, h->bundle_path, "CLAP plugin"));  // ADR-0045 P0-01
      clap_run(h, static_cast<int64_t>(t.steady), frames, in, 2, out, 2); }
    std::memcpy(L, out[0], frames * sizeof(float));
    std::memcpy(R, out[1], frames * sizeof(float));
}

// Drain the notes a CLAP plugin GENERATED this block (ADR-0015 / M2). ClapEventScratch::ev_push
// captured them during clap_run; the host used to throw them away.
void drain_clap_notes(ClapHandle* h, std::vector<NoteEvent>& out) {
    out.clear();
    if (!h || !h->has_note_out) return;
    for (uint32_t i = 0; i < h->events.out_count; ++i) {
        if (out.size() >= kGraphMaxNotes) break;   // truncate rather than allocate
        const clap_event_note_t& e = h->events.out_notes[i];
        const bool on = e.header.type == CLAP_EVENT_NOTE_ON;
        out.push_back(NoteEvent{ e.header.time, on, static_cast<int>(e.key),
                                 static_cast<float>(e.velocity), e.note_id, 0.f });
    }
}

}  // namespace vivid::session
