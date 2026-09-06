// ADR-0025 (vst3_host split, PR-B): the per-source/effect RENDER PRIMITIVES, extracted verbatim from
// vst3_host.cpp. Each runs one VST3/CLAP plugin for one block (or drains the notes it generated) —
// pure DSP over a handle, with no session/graph engine state. process_step + session_process (both
// still in vst3_host.cpp) call these through the declarations in vst3_host_internal.h, so the inline
// path and the audio-graph node dispatch share identical code (parity by construction). All RT-safe:
// fixed-capacity scratch, no heap, no locks. Pure code move — behaviour unchanged. Only possible now
// that PR-A gave the host types (Vst3Handle/Vst3EventList/...) external linkage.
#include "audio/vst3_host_internal.h"                 // Track, NoteEvent/ExprEvent, Vst3/ClapHandle, kGraphMaxNotes, Steinberg using-directives
#include "audio/plugin_crash_name.h"                  // plugin_crash_name (ADR-0045 P0-01)
#include "audio/plugin_watchdog.h"                    // ADR-0045 Tier 2a: over-budget watchdog (RT-safe)
#include "audio/plugin_hang_monitor.h"                // ADR-0045 Tier 2a: in-flight beacon for the hang monitor
#include "app/crash_guard.h"                          // ADR-0018: attribute a plugin crash (RT-safe pointer store)
#include "pluginterfaces/vst/ivstnoteexpression.h"    // kTuningTypeID / kBrightnessTypeID (note-expression axes)

#include <chrono>
#include <vector>
#include <cstring>
#include <cassert>
#include <algorithm>

namespace vivid::session {

// Drain a device's pending UI parameter changes into its ParamChanges block. File-local: only the
// two VST3 render primitives below feed a plugin's param queue, so it stays out of the shared header.
static void drain_params(Vst3Handle* h, Vst3ParamChanges& pc) {
    ParamMsg m;
    while (h->param_q.pop(m)) pc.add(m.id, m.value);   // sets id AND value (addParameterData+addPoint left value unset)
}

// P4: deliver this block's controller events. VST3 has NO MIDI-CC event — a controller reaches the
// plugin as a PARAMETER CHANGE on the id IMidiMapping bound it to, which is why this rides the same
// queue as UI edits and modulation rather than the event list.
//
// Deliberately NOT routed through h->param_q: that is a UI->audio SPSC ring, and this stream already
// originates on the audio thread, so pushing into a ring only to pop it in the same callback would
// add a drop-on-full failure mode for nothing.
//
// Block-granular by construction: SinglePointQueue::getPoint reports sampleOffset 0 whatever we
// stamp, so only the LAST value per controller in a block can reach the plugin anyway — pc.add()
// dedupes by ParamID, which collapses a fast sweep to one slot and gives that behaviour for free.
// A fast filter sweep therefore steps rather than glides at large buffer sizes; fixing that means
// replacing SinglePointQueue with a multi-point queue, which is orthogonal to everything here
// (CcEvent already carries a real sample_offset for when it lands).
//
// RT-safe: array indexing and a bounded pc.add; no allocation, no locks.
static void apply_cc_params(Vst3Handle* h, const std::vector<CcEvent>& cev, Vst3ParamChanges& pc) {
    if (!h->midi_map_ok || cev.empty()) return;
    for (const CcEvent& e : cev) {
        if (e.cc >= Vst3Handle::kCtrlCount || !h->midi_map_has[e.cc]) continue;   // unmapped: nothing to drive
        pc.add(h->midi_map[e.cc], std::clamp(static_cast<ParamValue>(e.value), 0.0, 1.0));
    }
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
    drain_params(h, pc);                    // UI param edits
    apply_cc_params(h, t.cev, pc);          // P4 clip automation / live controllers
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
    const char* wd_name = plugin_crash_name(h->plugin_name, h->vendor, "VST3 plugin");
    const auto  wd_t0   = std::chrono::steady_clock::now();                 // ADR-0045 Tier 2a
    vivid::audio::watchdog_mark_inflight(&h->watchdog, wd_name, t.id);      // beacon on (hang monitor)
    { vivid::CrashGuard cg(wd_name);  // ADR-0045 P0-01
      h->processor->process(data); }
    vivid::audio::watchdog_clear_inflight();                               // returned in time → beacon off
    vivid::audio::watchdog_note_process(h->watchdog, wd_name, t.id,
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wd_t0).count()), frames, ctx.sample_rate);
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
    assert(frames <= t.fxl.size() && frames <= t.fxr.size());   // Ph2 P3-02: pre-sized in reserve_track_graph; no alloc on the hot path
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
    const char* wd_name = plugin_crash_name(fx->plugin_name, fx->vendor, "VST3 plugin");
    const auto  wd_t0   = std::chrono::steady_clock::now();                 // ADR-0045 Tier 2a
    vivid::audio::watchdog_mark_inflight(&fx->watchdog, wd_name, t.id);     // beacon on (hang monitor)
    { vivid::CrashGuard cg(wd_name);  // ADR-0045 P0-01
      fx->processor->process(fd); }
    vivid::audio::watchdog_clear_inflight();                               // returned in time → beacon off
    vivid::audio::watchdog_note_process(fx->watchdog, wd_name, t.id,
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wd_t0).count()), frames, ctx.sample_rate);
    std::memcpy(L, oL, frames * sizeof(float));
    std::memcpy(R, oR, frames * sizeof(float));
}

// CLAP instrument. Builds this block's note + param events into the handle's scratch, then
// processes into L/R (silent input fed to any declared input port). Plays `notes` (t.nev for a
// full-range source, or a key-range-filtered list) plus this block's scene-switch note-offs.
// RT-safe (fixed scratch, no alloc/lock).
void render_clap_instrument(Track& t, ClapHandle* h, const std::vector<NoteEvent>& notes,
                            const std::vector<ExprEvent>& expr, uint32_t frames, float* L, float* R,
                            const ClapParamMsg* mod, uint32_t mod_n) {
    h->events.clear();
    clap_flush_params(h);
    for (uint32_t k = 0; k < mod_n; ++k) h->events.add_param(mod[k].id, mod[k].value);   // ADR-0034: modulation wins
    for (const NoteEvent& ne : t.scene_rel)   // scene-switch note-offs first, so held voices release
        h->events.add_note(ne.on, ne.pitch, ne.vel, ne.note_id, ne.sample_offset);
    for (const NoteEvent& ne : notes)         // this source's notes (full range = t.nev; key-split = filtered)
        h->events.add_note(ne.on, ne.pitch, ne.vel, ne.note_id, ne.sample_offset);
    // Per-note expression. This was MISSING: render_clap_instrument took notes and never read
    // t.eev, so the bend/pressure/timbre curves painted in the clip editor — which work on the
    // VST3 path — were silently dropped on EVERY CLAP instrument. Axis mapping mirrors emit_vst3,
    // except CLAP's TUNING is already in semitones so the bend value passes through unconverted.
    for (const ExprEvent& xe : expr) {
        const clap_note_expression id =
            xe.axis == vivid::session::AXIS_BEND     ? CLAP_NOTE_EXPRESSION_TUNING :
            xe.axis == vivid::session::AXIS_PRESSURE ? CLAP_NOTE_EXPRESSION_PRESSURE
                                                     : CLAP_NOTE_EXPRESSION_BRIGHTNESS;
        h->events.add_note_expression(id, xe.note_id, xe.pitch, xe.value, xe.sample_offset);
    }
    // P4: clip-level controllers as raw MIDI. Legal only when the plugin's note input advertises
    // CLAP_NOTE_DIALECT_MIDI; a CLAP-dialect-only plugin simply gets the note expression above.
    if (h->note_in_dialects & CLAP_NOTE_DIALECT_MIDI) {
        for (const CcEvent& ce : t.cev) {
            const uint8_t ch = ce.channel & 0x0F;
            if (ce.cc < 128) {                                   // control change
                h->events.add_midi(uint8_t(0xB0 | ch), uint8_t(ce.cc),
                                   uint8_t(std::clamp(ce.value, 0.f, 1.f) * 127.f + 0.5f), ce.sample_offset);
            } else if (ce.cc == vivid::session::kCcChannelPressure) {
                h->events.add_midi(uint8_t(0xD0 | ch),
                                   uint8_t(std::clamp(ce.value, 0.f, 1.f) * 127.f + 0.5f), 0, ce.sample_offset);
            } else if (ce.cc == vivid::session::kCcPitchBend) {   // 14-bit, LSB first
                const int b = std::clamp(static_cast<int>(std::clamp(ce.value, 0.f, 1.f) * 16383.f + 0.5f), 0, 16383);
                h->events.add_midi(uint8_t(0xE0 | ch), uint8_t(b & 0x7F), uint8_t((b >> 7) & 0x7F), ce.sample_offset);
            }
        }
    }
    float* out[2] = { L, R };
    float* in[2]  = { h->silence.data(), h->silence.data() + h->max_block };
    const char* wd_name = plugin_crash_name(h->name, h->bundle_path, "CLAP plugin");
    const auto  wd_t0   = std::chrono::steady_clock::now();                 // ADR-0045 Tier 2a
    vivid::audio::watchdog_mark_inflight(&h->watchdog, wd_name, t.id);      // beacon on (hang monitor)
    { vivid::CrashGuard cg(wd_name);  // ADR-0045 P0-01
      clap_run(h, static_cast<int64_t>(t.steady), frames, h->audio_in > 0 ? in : nullptr, 2, out, 2); }
    vivid::audio::watchdog_clear_inflight();                               // returned in time → beacon off
    vivid::audio::watchdog_note_process(h->watchdog, wd_name, t.id,
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wd_t0).count()), frames, h->sample_rate);
}

// CLAP effect. Transforms L/R in place via the track's fx scratch (t.fxl/t.fxr), like the VST3
// effect path. Caller guards `clap && clap->processing`.
void render_clap_effect(Track& t, ClapHandle* h, uint32_t frames, float* L, float* R,
                        const ClapParamMsg* mod, uint32_t mod_n) {
    assert(frames <= t.fxl.size() && frames <= t.fxr.size());   // Ph2 P3-02: pre-sized in reserve_track_graph; no alloc on the hot path
    h->events.clear();
    clap_flush_params(h);
    for (uint32_t k = 0; k < mod_n; ++k) h->events.add_param(mod[k].id, mod[k].value);   // ADR-0034: modulation wins
    float* in[2]  = { L, R };
    float* out[2] = { t.fxl.data(), t.fxr.data() };
    const char* wd_name = plugin_crash_name(h->name, h->bundle_path, "CLAP plugin");
    const auto  wd_t0   = std::chrono::steady_clock::now();                 // ADR-0045 Tier 2a
    vivid::audio::watchdog_mark_inflight(&h->watchdog, wd_name, t.id);      // beacon on (hang monitor)
    { vivid::CrashGuard cg(wd_name);  // ADR-0045 P0-01
      clap_run(h, static_cast<int64_t>(t.steady), frames, in, 2, out, 2); }
    vivid::audio::watchdog_clear_inflight();                               // returned in time → beacon off
    vivid::audio::watchdog_note_process(h->watchdog, wd_name, t.id,
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wd_t0).count()), frames, h->sample_rate);
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
