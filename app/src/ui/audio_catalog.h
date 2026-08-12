#pragma once
#include "audio/plugin_catalog.h"
#include "audio/vst3_host.h"
#include "ui/chooser.h"
#include "ui/ui_style.h"

#include <string>
#include <vector>

// The ONE catalog of things you can add to an audio track: native audio operators + every installed
// VST3 + every installed CLAP, in one list, classified into instruments and effects.
//
// This is the fix for the old split, where "+ Src"/"+ FX" could only ever offer native operators
// (they query the operator registry) and the plugin browser could only ever offer plugins — so no
// surface could add everything, and the two hard-coded 5-item menus (kEffectCatalog /
// kInstrumentCatalog) pretended otherwise.
namespace vivid::ui {

// Each row carries a typed `CatalogSpawn` (ADR-0023 step 5, ui/graph_catalog.h) describing how the
// audio editor spawns it; `audio_chooser_spawn` in input_graph.cpp switches on `spawn.kind`.
//
// `instruments_only` = the "+ Track" case (pick something that can START a signal).
inline std::vector<ChooserEntry> audio_catalog(vivid::session::Session* s, bool instruments_only = false) {
    namespace S = vivid::session;
    const Style& sty = style();
    std::vector<ChooserEntry> out;
    if (!s) return out;

    // --- native audio operators (the registry knows which are sources: no audio input port) ---
    for (int pass = 0; pass < 2; ++pass) {
        const bool want_source = (pass == 0);
        if (instruments_only && !want_source) continue;
        const int n = S::session_available_audio_op_count(s, want_source ? 1 : 0);
        for (int i = 0; i < n; ++i) {
            const char* nm = S::session_available_audio_op_name(s, want_source ? 1 : 0, i);
            if (!nm || !*nm) continue;
            ChooserEntry e;
            e.label = nm;
            e.id = nm;
            e.badge = want_source ? "INS" : "FX";
            e.hay = std::string("native ") + (want_source ? "instrument synth" : "effect");
            e.spawn = { Domain::Audio, want_source ? SpawnKind::AudioNativeSource : SpawnKind::AudioNativeEffect, nm };
            e.role = static_cast<VividOperatorRole>(S::session_audio_op_role(s, nm));   // ADR-0046 chip
            e.accent = want_source ? sty.audio : sty.fx;
            e.summary = want_source ? "native instrument" : "native effect";
            out.push_back(std::move(e));
        }
    }

    // --- ADR-0015: notes are a signal you can wire ---
    if (!instruments_only) {
        {   // ADR-0033 P5: a sticky NOTE — annotate the audio graph (not a node, makes no sound).
            ChooserEntry e;
            e.label = "Note";
            e.badge = "NOTE";
            e.summary = "sticky note \xC2\xB7 annotate the graph";
            e.hay = "note sticky annotation comment label";
            e.spawn = { Domain::Shared, SpawnKind::Note };
            e.accent = sty.gold;
            out.push_back(std::move(e));
        }
        {   // the track's note stream, as a node
            ChooserEntry e;
            e.label = "MIDI In";
            e.id = "";
            e.badge = "NOTE";
            e.summary = "the track's notes (clips, keyboard, live MIDI) as a node";
            e.hay = "midi notes input keyboard clip source";
            e.spawn = { Domain::Audio, SpawnKind::AudioMidiIn };
            e.accent = sty.control;
            out.push_back(std::move(e));
        }
        const int nn = S::session_available_note_op_count(s);
        for (int i = 0; i < nn; ++i) {
            const char* nm = S::session_available_note_op_name(s, i);
            if (!nm || !*nm) continue;
            ChooserEntry e;
            e.label = nm;
            e.id = nm;
            e.badge = "NOTE";
            e.summary = "note effect \xE2\x80\x94 transforms notes, makes no sound";
            e.hay = "note effect arpeggiator midi";
            e.spawn = { Domain::Audio, SpawnKind::AudioNoteOp, nm };
            e.role = static_cast<VividOperatorRole>(S::session_audio_op_role(s, nm));   // ADR-0046 chip
            e.accent = sty.control;
            out.push_back(std::move(e));
        }
        // ADR-0022: native MODULATORS (LFO / envelope) — no sound; wire the output to a param.
        const int nm_ct = S::session_available_mod_op_count(s);
        for (int i = 0; i < nm_ct; ++i) {
            const char* nm = S::session_available_mod_op_name(s, i);
            if (!nm || !*nm) continue;
            ChooserEntry e;
            e.label = nm;
            e.id = nm;
            e.badge = "MOD";
            e.summary = "modulator \xE2\x80\x94 drives a param over time, makes no sound";
            e.hay = "modulator lfo envelope control modulation";
            e.spawn = { Domain::Audio, SpawnKind::AudioModOp, nm };
            e.role = static_cast<VividOperatorRole>(S::session_audio_op_role(s, nm));   // ADR-0046 chip
            e.accent = sty.mod;
            out.push_back(std::move(e));
        }
    }

    // --- every installed plugin (VST3 + CLAP), classified by the background probe ---
    const int np = S::plugin_count();
    for (int i = 0; i < np; ++i) {
        const S::PluginInfo& p = S::plugin_at(i);
        const bool is_inst = (p.cls == S::kClassInstrument);
        const bool is_note = (p.cls == S::kClassNoteEffect);
        // An unprobed/unknown plugin is offered as an effect (the loader's own fallback), but says so.
        if (instruments_only && !is_inst) continue;
        if (p.cls == S::kClassFailed || p.cls == S::kClassCrashed) continue;   // can't be hosted at all

        ChooserEntry e;
        e.label = p.name;
        e.id = p.path;
        e.badge = S::plugin_format_name(p.format);
        e.hay = p.vendor + " " + S::plugin_class_name(p.cls) + " plugin";
        e.spawn = { Domain::Audio, is_inst ? SpawnKind::AudioPluginSource : SpawnKind::AudioPluginEffect,
                    p.path, p.format };
        e.accent = is_inst ? sty.audio : sty.fx;
        e.summary = p.vendor.empty() ? (is_inst ? "instrument" : "effect")
                                     : p.vendor + " \xC2\xB7 " + (is_inst ? "instrument" : "effect");
        if (!p.probed) e.summary = "not yet classified";
        // ADR-0015: a note effect (arpeggiator, chord generator) transforms NOTES. It is spawned as
        // a SOURCE-shaped node (no audio inputs, so it can never write silence over the chain) and
        // wired with note edges. The host drains the notes it emits (M2/M3).
        if (is_note) {
            e.spawn.kind = SpawnKind::AudioPluginSource;   // source-shaped; format/path already set
            e.badge = "NOTE";
            e.summary = "note effect \xE2\x80\x94 transforms notes; wire it with note edges";
            e.accent = sty.control;
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace vivid::ui
