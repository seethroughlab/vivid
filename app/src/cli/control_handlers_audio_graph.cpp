#include "cli/control_handlers_audio_domains.h"
#include "cli/audio_analysis_tools.h"
#include "cli/control_handlers_internal.h"

#include "audio/plugin_catalog.h"
#include "audio/vst3_host.h"

#include <string>

namespace vivid {

void register_audio_graph_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    // AG-1 step 2: authoritative topology edits. The first flips the track's audio graph to the
    // editable source of truth; get_audio_graph reflects the result (nodes/edges/output_id).
    handlers_["audio_graph_add_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add audio effect node: '" + op + "'");
        json r = ok(); r["node"] = nid; return r;
    };
    // A2: spawn a VST3/CLAP plugin as a graph NODE (the peer of audio_graph_add_op, which is
    // native-only). `path` = the bundle; `source` = instrument (fan-in) vs effect (splice). The
    // node id comes back immediately — a CLAP binds when its async load lands, so poll
    // get_audio_graph / plugin_loads_pending to know when it's live.
    handlers_["audio_graph_add_plugin"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "path required (a .vst3 / .clap bundle)");
        const bool clap = path.size() > 5 && path.compare(path.size() - 5, 5, ".clap") == 0;
        const int fmt = b.contains("format") ? b.value("format", 0)
                                             : (clap ? P::kFmtCLAP : P::kFmtVST3);
        const int src = b.value("source", 0) ? 1 : 0;
        const std::string uid = b.value("uid", std::string());
        const int nid = P::session_audio_graph_add_plugin(c.session, track, path.c_str(), fmt, src, uid.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add plugin node: '" + path + "'");
        json r = ok(); r["node"] = nid;
        r["ready"] = P::session_audio_graph_node_plugin_ready(c.session, track, nid);   // 0 = still loading
        return r;
    };
    handlers_["audio_graph_add_source"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_source(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add audio source node: '" + op + "' (unknown or not an instrument)");
        json r = ok(); r["node"] = nid; return r;
    };
    handlers_["audio_graph_set_node_key_range"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        const int lo = b.value("lo", 0), hi = b.value("hi", 127);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_node_key_range_set(c.session, track, node, lo, hi);
        return ok();
    };
    handlers_["audio_graph_remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!P::session_audio_graph_remove_node(c.session, track, node))
            return err(code::kBadArg, "node not removable (unknown, or an instrument/output)");
        return ok();
    };
    // ADR-0015: `kind` picks the signal the edge carries — "audio" (default; sums at the
    // destination) or "note" (merges). A note edge is how an instrument gets its notes once the
    // graph, rather than an invisible per-track broadcast, is doing the routing.
    handlers_["audio_graph_connect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string kind = b.value("kind", std::string("audio"));
        if (kind != "audio" && kind != "note") return err(code::kBadArg, "kind must be 'audio' or 'note'");
        if (!P::session_audio_graph_connect_kind(c.session, track, from, to, kind == "note" ? 1 : 0))
            return err(code::kBadArg, "edge rejected (duplicate, self-loop, unknown node, or would create a cycle)");
        return ok();
    };
    // A native NOTE EFFECT (ADR-0015), e.g. "Arp": notes in -> notes out, no audio. Wire MidiIn ->
    // it -> an instrument with NOTE edges.
    handlers_["audio_graph_add_note_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_note_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add note op '" + op + "'");
        json r = ok(); r["node"] = nid; return r;
    };
    // A native MODULATOR (ADR-0022), e.g. "LFO": no audio, emits a 0..1 control signal. Wire its
    // output to a param with audio_graph_connect_control.
    handlers_["audio_graph_add_mod_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_mod_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add modulator '" + op + "' (unknown op or not a modulator)");
        json r = ok(); r["node"] = nid; return r;
    };
    // Wire a modulator -> one param of a node (ADR-0022). amount is a fraction of the param's
    // declared range; bipolar straddles the base (an LFO for pitch/pan), unipolar runs up from it.
    handlers_["audio_graph_connect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_audio_graph_connect_control(c.session, track, from, to, param, amount, curve, invert, bipolar))
            return err(code::kBadArg, "control edge rejected (duplicate param, self-loop, unknown node, or would create a cycle)");
        return ok();
    };
    handlers_["audio_graph_disconnect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_disconnect_control(c.session, track, from, to, param);
        return ok();
    };
    // ADR-0022 P2a.2: a SESSION-level CROSS-TRACK control edge — a modulator on `src_track` node
    // `src_node` drives `dst_track` node `dst_node`'s `param`. Tracks are indices; nodes are stable
    // graph node ids; shape = amount/curve/invert/bipolar (same as the in-track edge).
    handlers_["session_connect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_connect_control(c.session, src_track, src_node, dst_track, dst_node, param, amount, curve, invert, bipolar))
            return err(code::kBadArg, "cross-track control edge rejected (unknown track/node, no param, or duplicate)");
        return ok();
    };
    handlers_["session_disconnect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        P::session_disconnect_control(c.session, src_track, src_node, dst_track, dst_node, param);
        return ok();
    };
    handlers_["session_set_control_shape"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_set_control_shape(c.session, src_track, src_node, dst_track, dst_node, param, amount, curve, invert, bipolar))
            return err(code::kNotFound, "no cross-track control edge for that (src, dst, param)");
        return ok();
    };
    // ADR-0022 P2b.4: cross-track AUDIO edges — a node's output on one track summed into a node on
    // another. No param/shape (audio is a full signal, not a scalar). Rejected on same-track, a source
    // destination, a duplicate, or a cross-track cycle.
    handlers_["session_connect_audio"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        if (!P::session_connect_audio(c.session, src_track, src_node, dst_track, dst_node))
            return err(code::kBadArg, "cross-track audio edge rejected (unknown track/node, same track, source destination, duplicate, or would cycle)");
        return ok();
    };
    handlers_["session_disconnect_audio"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        P::session_disconnect_audio(c.session, src_track, src_node, dst_track, dst_node);
        return ok();
    };
    // ADR-0022 P2b.5: cross-track NOTE edges — a note-emitting node (MidiIn / note effect / note-
    // generating plugin) on one track drives a note-consuming node (instrument / note effect) on another.
    handlers_["session_connect_note"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        if (!P::session_connect_note(c.session, src_track, src_node, dst_track, dst_node))
            return err(code::kBadArg, "cross-track note edge rejected (unknown track/node, same track, non-emitter source, non-consumer destination, duplicate, or would cycle)");
        return ok();
    };
    handlers_["session_disconnect_note"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        P::session_disconnect_note(c.session, src_track, src_node, dst_track, dst_node);
        return ok();
    };
    // Re-shape an existing modulation edge (ADR-0022) without rewiring.
    handlers_["audio_graph_set_control_shape"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_audio_graph_set_control_shape(c.session, track, from, to, param, amount, curve, invert, bipolar))
            return err(code::kNotFound, "no control edge for that (from, to, param)");
        return ok();
    };
    // The track's note stream as a NODE (ADR-0015). Wire its note edge into an instrument.
    handlers_["audio_graph_add_midi_in"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const int nid = P::session_audio_graph_add_midi_in(c.session, track);
        if (nid < 0) return err(code::kInternal, "could not add a MidiIn node");
        json r = ok(); r["node"] = nid; return r;
    };
    handlers_["audio_graph_disconnect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_disconnect(c.session, track, from, to);
        return ok();
    };
    handlers_["audio_graph_set_node_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1), param = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_node_param_set(c.session, track, node, param, b.value("value", 0.f));
        return ok();
    };
    handlers_["audio_graph_set_node_param_by_name"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = lower_copy_audio(b.value("name", b.value("param", std::string())));
        if (name.empty()) return err(code::kBadArg, "need name");
        const int pc = P::session_audio_graph_node_param_count(c.session, track, node);
        int hit = -1;
        for (int p = 0; p < pc; ++p) {
            if (lower_copy_audio(P::session_audio_graph_node_param_name(c.session, track, node, p)) == name) {
                if (hit >= 0) return err(code::kBadArg, "ambiguous param name '" + name + "'");
                hit = p;
            }
        }
        if (hit < 0) return err(code::kNotFound, "no audio graph node param named '" + name + "'");
        P::session_audio_graph_node_param_set(c.session, track, node, hit, b.value("value", 0.f));
        json r = ok(); r["param"] = hit; r["name"] = P::session_audio_graph_node_param_name(c.session, track, node, hit); return r;
    };
    handlers_["slice_to_midi"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        const int mode = b.value("mode", 1);   // 1=transients, 3=16-grid
        json e; if (!need_track(c.session, track, e)) return e;
        const int nt = P::session_slice_to_midi(c.session, track, scene, mode);
        if (nt < 0) return err(code::kBadArg, "slice-to-MIDI failed (not an audio clip, or no slices)");
        json r = ok(); r["track"] = nt; return r;
    };
    handlers_["list_audio_ops"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        auto op_json = [&](int index) {
            json jo; jo["type"] = P::session_audio_op_type(c.session, track, index);
            json ps = json::array();
            for (int p = 0; p < P::session_audio_op_param_count(c.session, track, index); ++p)
                ps.push_back({ {"name", P::session_audio_op_param_name(c.session, track, index, p)},
                               {"value", P::session_audio_op_param_get(c.session, track, index, p)} });
            jo["params"] = ps; return jo;
        };
        json r = ok();
        if (*P::session_audio_op_type(c.session, track, -1)) r["instrument"] = op_json(-1);
        json fx = json::array();
        for (int i = 0; i < P::session_audio_effect_count(c.session, track); ++i) fx.push_back(op_json(i));
        r["effects"] = fx; return r;
    };

    // AG-1: the track's authoritative audio graph (nodes + edges the RT executor runs). Distinct
    // from list_audio_ops (the linear device view): this reports the persistent topology model —
    // stable node ids, kinds, and (from_id -> to_id) edges — that the audio-graph UI + future
    // rewiring build on. graph_ok is false for VST3 / inline tracks (empty graph).
    handlers_["get_audio_graph"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        static const char* kKind[] = { "instrument", "effect", "output", "midi_in", "note_effect", "modulator", "midi_clip" };   // 3,4 ADR-0015; 5,6 ADR-0022
        json r = ok();
        r["graph_ok"]   = P::session_track_audio_graph_ok(c.session, track) != 0;
        r["output_id"]  = P::session_track_audio_graph_output_id(c.session, track);
        json nodes = json::array();
        for (int i = 0; i < P::session_track_audio_graph_node_count(c.session, track); ++i) {
            const int k   = P::session_track_audio_graph_node_kind(c.session, track, i);
            const int nid = P::session_track_audio_graph_node_id(c.session, track, i);
            int nin = 0, nout = 0;   // ADR-0015: does it take / emit notes?
            P::session_track_audio_graph_node_note_ports(c.session, track, i, &nin, &nout);
            json jn = { {"id",   nid},
                        {"gnid", P::session_track_audio_graph_node_gnid(c.session, track, i)},   // ADR-0022 P2b.3c: session-global id (-1 if unassigned)
                        {"kind", (k >= 0 && k < 7) ? kKind[k] : "unknown"},
                        {"note_in", nin != 0}, {"note_out", nout != 0},
                        {"type", P::session_track_audio_graph_node_type(c.session, track, i)} };
            if (k == 0) {   // source node: report its key range (a key-split shows disjoint ranges)
                int lo = 0, hi = 127;
                if (P::session_audio_graph_node_key_range_get(c.session, track, nid, &lo, &hi)) {
                    jn["key_lo"] = lo; jn["key_hi"] = hi;
                }
            }
            // ADR-0022: each param as base / value (resolved) / wired — the same shape the visuals
            // introspection dump uses. `value` == `base` unless a control edge drives the param.
            json params = json::array();
            for (int p = 0; p < P::session_audio_graph_node_param_count(c.session, track, nid); ++p) {
                const bool wired = P::session_audio_graph_node_param_wired(c.session, track, nid, p) != 0;
                json jp = { {"name",  P::session_audio_graph_node_param_name(c.session, track, nid, p)},
                            {"base",  P::session_audio_graph_node_param_get(c.session, track, nid, p)},
                            {"value", P::session_audio_graph_node_param_resolved(c.session, track, nid, p)},
                            {"wired", wired} };
                params.push_back(jp);
            }
            if (!params.empty()) jn["params"] = params;
            nodes.push_back(jn);
        }
        r["nodes"] = nodes;
        json edges = json::array();
        for (int i = 0; i < P::session_track_audio_graph_edge_count(c.session, track); ++i) {
            const int ek = P::session_track_audio_graph_edge_kind(c.session, track, i);
            // Which SIGNAL the wire carries. Without this an agent can't tell a note edge from an
            // audio one from a control one, and the three mean very different things.
            json je = { {"from", P::session_track_audio_graph_edge_from(c.session, track, i)},
                        {"to",   P::session_track_audio_graph_edge_to(c.session, track, i)},
                        {"kind", ek == 2 ? "control" : (ek == 1 ? "note" : "audio")} };
            if (ek == 2) {   // ADR-0022: a control edge carries its target param + shaper
                je["param"] = P::session_track_audio_graph_edge_dest_param(c.session, track, i);
                float amount = 1.f, curve = 0.f; int invert = 0, bipolar = 0;
                P::session_track_audio_graph_edge_control_shape(c.session, track, i, &amount, &curve, &invert, &bipolar);
                je["amount"] = amount; je["curve"] = curve;
                je["invert"] = invert != 0; je["bipolar"] = bipolar != 0;
            }
            edges.push_back(je);
        }
        r["edges"] = edges;
        // ADR-0022 P2a.2/P2a.3: the session-level CROSS-TRACK control edges (all of them — they are a
        // session concept, not per-track). Each is {src_track, src_node, dst_track, dst_node, param}
        // + shape, mirroring the in-track control-edge report.
        json xctl = json::array();
        for (int i = 0; i < P::session_xctl_count(c.session); ++i) {
            int st = 0, sn = 0, dt = 0, dn = 0, pr = 0, inv = 0, bip = 0; float am = 1.f, cv = 0.f;
            if (!P::session_xctl_get(c.session, i, &st, &sn, &dt, &dn, &pr, &am, &cv, &inv, &bip)) continue;
            xctl.push_back({ {"src_track", st}, {"src_node", sn}, {"dst_track", dt}, {"dst_node", dn},
                             {"param", pr}, {"amount", am}, {"curve", cv}, {"invert", inv != 0}, {"bipolar", bip != 0} });
        }
        r["xcontrol"] = xctl;
        // ADR-0022 P2b.4: the session-level CROSS-TRACK audio edges (also a session concept, not
        // per-track). Each is {src_track, src_node, dst_track, dst_node} — no shape (a full signal).
        json xaudio = json::array();
        for (int i = 0; i < P::session_xaudio_count(c.session); ++i) {
            int st = 0, sn = 0, dt = 0, dn = 0;
            if (!P::session_xaudio_get(c.session, i, &st, &sn, &dt, &dn)) continue;
            xaudio.push_back({ {"src_track", st}, {"src_node", sn}, {"dst_track", dt}, {"dst_node", dn} });
        }
        r["xaudio"] = xaudio;
        // ADR-0022 P2b.5: the session-level CROSS-TRACK note edges. Each is {src_track, src_node,
        // dst_track, dst_node} — no shape.
        json xnote = json::array();
        for (int i = 0; i < P::session_xnote_count(c.session); ++i) {
            int st = 0, sn = 0, dt = 0, dn = 0;
            if (!P::session_xnote_get(c.session, i, &st, &sn, &dt, &dn)) continue;
            xnote.push_back({ {"src_track", st}, {"src_node", sn}, {"dst_track", dt}, {"dst_node", dn} });
        }
        r["xnote"] = xnote;
        return r;
    };


}

}  // namespace vivid
