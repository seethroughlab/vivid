#pragma once
#include "ui/ui_style.h"   // Domain {Audio,Visual,Bridge,Shared}
#include <string>

// ADR-0023 step 5 — the shared graph-catalog vocabulary (the Layer-2 companion to node_canvas.h /
// graph_canvas.h). One typed domain/kind/spawn descriptor that BOTH node editors' Tab catalogs emit,
// replacing the two colliding, untyped `ChooserEntry.tag` int schemes (where 0/1 meant different
// things per editor) and the `id`/`badge` overloading (a char_id smuggled as a string, a plugin
// format recovered by string-comparing the badge). The Chooser widget never reads this — each editor's
// spawn dispatcher switches on `kind`. The spawn *actions* stay per-editor (their side effects differ);
// only the entry SHAPE is unified here.
namespace vivid::ui {

// One kind per spawnable node, across both editors.
enum class SpawnKind {
    None,                 // a non-catalog chooser (e.g. a param picker) — spawns no node
    VisualOp,             // a visuals operator      -> VisualGraph::add_node(type)
    BridgeNode,           // a bridge data node      -> NodeGraph::add_data_node(label, char_id)
    AudioNativeSource,    // native instrument       -> session_audio_graph_add_source(type)
    AudioNativeEffect,    // native effect           -> session_audio_graph_add_op(type)
    AudioPluginSource,    // plugin instrument       -> session_audio_graph_add_plugin(path, format, /*src*/1)
    AudioPluginEffect,    // plugin effect           -> session_audio_graph_add_plugin(path, format, /*src*/0)
    AudioNoteOp,          // note effect (ADR-0015)  -> session_audio_graph_add_note_op(type)
    AudioMidiIn,          // the track note stream   -> session_audio_graph_add_midi_in()
    AudioModOp,           // a modulator (ADR-0022)  -> session_audio_graph_add_mod_op(type)
};

// The typed spawn payload a catalog entry carries. `type` is an op type name OR a plugin bundle path;
// `format` is the session plugin-format constant (session::kFmtVST3 / kFmtCLAP) for the plugin kinds;
// `char_id` is the master-characteristic id for a bridge data node. `domain` is display/zoning metadata
// (reuses the ui_style Domain), somewhat derivable from `kind` but carried explicitly per ADR-0023.
struct CatalogSpawn {
    Domain      domain  = Domain::Shared;
    SpawnKind   kind    = SpawnKind::None;
    std::string type;         // op type name OR plugin bundle path
    int         format  = 0;  // session plugin-format constant (plugin kinds only)
    int         char_id = 0;  // bridge data node — legacy packed master/track characteristic id
    std::string source;       // bridge data node — canonical string source id (empty = use char_id); e.g. "track_5.fft.2"
};

}  // namespace vivid::ui
