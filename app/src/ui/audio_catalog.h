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

// ChooserEntry::tag — how the owner spawns the chosen row.
enum AudioEntryTag {
    kAudioNativeEffect = 0,   // session_audio_graph_add_op
    kAudioNativeSource = 1,   // session_audio_graph_add_source
    kAudioPluginEffect = 2,   // session_audio_graph_add_plugin(..., is_source=0)
    kAudioPluginSource = 3,   // session_audio_graph_add_plugin(..., is_source=1)
};

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
            e.tag = want_source ? kAudioNativeSource : kAudioNativeEffect;
            e.accent = want_source ? sty.audio : sty.fx;
            e.summary = want_source ? "native instrument" : "native effect";
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
        e.tag = is_inst ? kAudioPluginSource : kAudioPluginEffect;
        e.accent = is_inst ? sty.audio : sty.fx;
        e.summary = p.vendor.empty() ? (is_inst ? "instrument" : "effect")
                                     : p.vendor + " \xC2\xB7 " + (is_inst ? "instrument" : "effect");
        if (!p.probed) e.summary = "not yet classified";
        // ADR-0015: a note-effect (an arpeggiator, a chord generator) transforms NOTES, and notes are
        // not yet a signal in the graph. Spawning one as an audio effect would give it no notes and
        // write silence over the chain. Show it — hiding it would make the catalog lie about what you
        // own — but refuse to spawn it until note edges exist.
        if (is_note) {
            e.enabled = false;
            e.disabled_note = "note effect \xE2\x80\x94 needs note routing (ADR-0015)";
            e.badge = "NOTE";
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace vivid::ui
