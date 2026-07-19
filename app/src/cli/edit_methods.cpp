#include "cli/edit_methods.h"

#include <unordered_map>

namespace vivid {

const EditMethodInfo* edit_method_info(const std::string& method) {
    // label, coalesces. Structural edits (add/remove/connect) never coalesce — each is its own
    // undo boundary; value edits (set_*_param) coalesce a rapid run into one entry.
    static const std::unordered_map<std::string, EditMethodInfo> kTable = {
        // ---- visuals (G2) ----
        { "add_node",            { "Add Node",           false } },
        { "remove_node",         { "Delete Node",        false } },
        { "connect_nodes",       { "Connect",            false } },
        { "set_active_output",   { "Set Output",         false } },
        { "set_generator",       { "Set Generator",      false } },
        { "add_data_node",       { "Add Data Node",      false } },
        { "set_node_param",      { "Set Param",          true  } },
        { "set_node_file_param", { "Set File",           true  } },
        { "set_node_asset",      { "Set Asset",          true  } },
        // ---- the bridge (G2) ----
        { "connect_mapping",     { "Connect Mapping",    false } },
        { "disconnect_mapping",  { "Disconnect Mapping", false } },
        // ---- layout (G2) ----
        { "layout_graph",        { "Auto-Layout",        false } },
        // ---- audio session (G3): structure ----
        { "add_track",             { "Add Track",           false } },
        { "add_graph_track",       { "Add Track",           false } },
        { "add_scene",             { "Add Scene",           false } },
        { "remove_track",          { "Delete Track",        false } },
        { "add_effect",            { "Add Effect",          false } },
        { "remove_effect",         { "Remove Effect",       false } },
        { "add_audio_effect",      { "Add Audio Op",        false } },
        { "remove_audio_effect",   { "Remove Audio Op",     false } },
        { "add_track_clap_effect", { "Add CLAP Effect",     false } },
        { "set_track_audio_instrument", { "Set Instrument",  false } },
        { "set_track_clap_instrument",  { "Set Instrument",  false } },
        { "slice_to_midi",         { "Slice to MIDI",       false } },
        { "audio_graph_add_op",     { "Add Audio Node",     false } },
        { "audio_graph_add_source", { "Add Audio Node",     false } },
        { "audio_graph_add_note_op",{ "Add Audio Node",     false } },
        { "audio_graph_add_mod_op", { "Add Modulator",      false } },
        { "audio_graph_add_midi_in",{ "Add Audio Node",     false } },
        { "audio_graph_add_plugin", { "Add Audio Node",     false } },
        { "audio_graph_remove_node",{ "Remove Audio Node",  false } },
        { "audio_graph_connect",    { "Connect Audio",      false } },
        { "audio_graph_connect_control",    { "Connect Modulation",    false } },
        { "audio_graph_disconnect_control", { "Disconnect Modulation", false } },
        { "session_connect_control",        { "Connect Modulation",    false } },
        { "session_disconnect_control",     { "Disconnect Modulation", false } },
        { "session_connect_audio",          { "Connect Cross-Track Audio",    false } },
        { "session_disconnect_audio",       { "Disconnect Cross-Track Audio", false } },
        { "session_connect_note",           { "Connect Cross-Track Notes",    false } },
        { "session_disconnect_note",        { "Disconnect Cross-Track Notes", false } },
        { "session_set_control_shape",      { "Shape Modulation",      true  } },
        { "audio_graph_set_control_shape",  { "Shape Modulation",      true  } },
        { "audio_graph_disconnect", { "Disconnect Audio",   false } },
        { "pool_place",            { "Add Clip",            false } },
        { "pool_remove",           { "Remove Clip",         false } },
        { "pool_stash",            { "Stash Clip",          false } },
        // ---- audio session (G3): values (coalesce a rapid run into one entry) ----
        { "set_track_gain",        { "Set Gain",            true  } },
        { "set_master_gain",       { "Set Master Gain",     true  } },
        { "set_track_mute",        { "Mute",                false } },
        { "set_track_solo",        { "Solo",                false } },
        { "set_param",             { "Set Param",           true  } },
        { "set_audio_op_param",    { "Set Param",           true  } },
        { "audio_graph_set_node_param",     { "Set Param",       true } },
        { "audio_graph_set_node_key_range", { "Set Key Range",   true } },
        { "set_clip",              { "Edit Clip",           true  } },
        { "set_clip_loop",         { "Set Loop",            true  } },
        { "audio_set_warp",        { "Warp Clip",           true  } },
        { "audio_auto_warp",       { "Warp Clip",           true  } },
        { "audio_set_pitch",       { "Pitch Clip",          true  } },
        { "audio_set_gain",        { "Clip Gain",           true  } },
        { "audio_set_reverse",     { "Reverse Clip",        false } },
        // NOT undoable (performance / plugin-internal): launch_clip, launch_scene, arm_track, record,
        // metronome, note_on/off, set_playing, toggle_play, reset_transport, set_bpm, load_preset.
    };
    auto it = kTable.find(method);
    return it == kTable.end() ? nullptr : &it->second;
}

}  // namespace vivid
