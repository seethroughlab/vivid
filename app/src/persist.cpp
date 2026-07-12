#include "persist.h"
#include "audio/vst3_host.h"
#include "midi/note_json.h"
#include "ui/node_graph.h"
#include "gpu/shader_uniforms.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <cstdio>

using json = nlohmann::json;

namespace vivid {

json session_to_json(vivid::session::Session* s, vivid::ui::NodeGraph& g,
                     int win_w, int win_h, float split_x, float dock_h) {
    json j;
    if (!s) return j;
    j["version"] = kSessionSchemaVersion;
    j["window"] = { {"w", win_w}, {"h", win_h}, {"split", split_x}, {"dock", dock_h} };

    const int nt = vivid::session::session_track_count(s);
    const int ns = vivid::session::session_scene_count(s);
    json tracks = json::array();
    for (int t = 0; t < nt; ++t) {
        json jt;
        jt["name"]   = vivid::session::session_track_name(s, t);
        jt["gain"]   = vivid::session::session_track_gain(s, t);
        jt["active"] = vivid::session::session_active_clip(s, t);
        const bool aud = vivid::session::session_track_is_audio(s, t);
        jt["is_audio"] = aud;
        // v2: the track set is the document. `kind` + `instrument` let load recreate the
        // track (the track name is the plugin name, which doubles as the catalog/match spec).
        jt["kind"] = aud ? "audio" : "instrument";
        jt["id"]   = vivid::session::session_track_id(s, t);   // stable id (mapping sources reference it)
        if (!aud) jt["instrument"] = vivid::session::session_track_name(s, t);
        if (aud) {
            json trims = json::array(), aclips = json::array();
            for (int sc = 0; sc < ns; ++sc) {
                float a = 0.f, b = 1.f; vivid::session::session_get_audio_trim(s, t, sc, &a, &b);
                trims.push_back({ a, b });
                // Per-scene clip shaping (A3). `warp` = -1 off, else the mode 0..2.
                float fin = 0.f, fout = 0.f, fx = 0.f;
                vivid::session::session_get_audio_fades(s, t, sc, &fin, &fout, &fx);
                aclips.push_back({
                    { "gain",    vivid::session::session_get_audio_gain(s, t, sc) },
                    { "pitch",   vivid::session::session_get_audio_pitch(s, t, sc) },
                    { "reverse", vivid::session::session_get_audio_reverse(s, t, sc) != 0 },
                    { "warp",    vivid::session::session_get_audio_warp(s, t, sc) },
                    { "fade_in_ms", fin }, { "fade_out_ms", fout }, { "loop_xfade_ms", fx },
                    // Source WAV so the loop reloads on open (empty = a generated loop, left as-is).
                    { "src_path", vivid::session::session_get_audio_path(s, t, sc) },
                    { "src_bpm",  vivid::session::session_get_audio_src_bpm(s, t, sc) },
                });
            }
            jt["trims"] = trims;
            jt["audio_clips"] = aclips;
        } else {
            json clips = json::array();
            for (int sc = 0; sc < ns; ++sc) {
                vivid::session::ClipNote buf[256];
                const int n = vivid::session::session_get_clip(s, t, sc, buf, 256);
                json notes = json::array();
                for (int i = 0; i < n; ++i) {
                    json jn = { {"p", buf[i].pitch}, {"s", buf[i].start}, {"d", buf[i].dur}, {"v", buf[i].vel} };
                    vivid::session::expr_to_json(buf[i], jn);
                    notes.push_back(jn);
                }
                json jc = { {"length", vivid::session::session_clip_length(s, t, sc)}, {"notes", notes} };
                double ls = 0, le = 0; vivid::session::session_get_clip_loop(s, t, sc, &ls, &le);
                if (le > ls) { jc["loop_start"] = ls; jc["loop_end"] = le; }   // in-clip loop region
                clips.push_back(jc);
            }
            jt["clips"] = clips;
            const std::string state = vivid::session::session_get_track_state(s, t);  // plugin preset (VST3 or CLAP)
            if (!state.empty()) jt["state"] = state;
            // CLAP instrument + effects: save the .clap path (load recreates the plugin) + its state.
            const char* cpath = vivid::session::session_track_clap_instrument_path(s, t);
            if (cpath && *cpath) jt["clap_instrument"] = cpath;
            json cfx = json::array();
            for (int e = 0; e < vivid::session::session_track_clap_effect_count(s, t); ++e) {
                json je = { {"path", vivid::session::session_track_clap_effect_path(s, t, e)} };
                const std::string est = vivid::session::session_get_track_clap_effect_state(s, t, e);
                if (!est.empty()) je["state"] = est;
                cfx.push_back(je);
            }
            if (!cfx.empty()) jt["clap_effects"] = cfx;
        }
        json fx = json::array();   // per-track effect chain (name + exposed param values)
        for (int e = 0; e < vivid::session::session_effect_count(s, t); ++e) {
            json je; je["name"] = vivid::session::session_effect_name(s, t, e);
            json ps = json::array();
            const int pc = vivid::session::session_param_count(s, t, e + 1);  // device = effect+1
            for (int p = 0; p < pc; ++p)
                ps.push_back({ {"id", vivid::session::session_param_id(s, t, e + 1, p)},
                               {"v",  vivid::session::session_param_value(s, t, e + 1, p)} });
            if (!ps.empty()) je["params"] = ps;
            fx.push_back(je);
        }
        if (!fx.empty()) jt["fx"] = fx;

        // Native audio operators (AO-2): the instrument slot (index -1) + effect chain,
        // each as { op, params:{name:value} }.
        auto audio_op = [&](int index) {
            json jo; jo["op"] = vivid::session::session_audio_op_type(s, t, index);
            json ps = json::object();
            for (int p = 0; p < vivid::session::session_audio_op_param_count(s, t, index); ++p)
                ps[vivid::session::session_audio_op_param_name(s, t, index, p)] =
                    vivid::session::session_audio_op_param_get(s, t, index, p);
            jo["params"] = ps;
            return jo;
        };
        // AG-1 step 2: a rewired (authoritative) graph is saved as topology — nodes (stable id +
        // kind + op + params) and (from->to) edges — since the linear chain can't represent a DAG.
        // Otherwise the linear instrument/fx chain is saved as before (unchanged for old projects).
        if (vivid::session::session_track_audio_graph_authoritative(s, t)) {
            json g, nodes = json::array(), edges = json::array();
            const int nn = vivid::session::session_track_audio_graph_node_count(s, t);
            for (int i = 0; i < nn; ++i) {
                const int id = vivid::session::session_track_audio_graph_node_id(s, t, i);
                json jn; jn["id"] = id;
                jn["kind"] = vivid::session::session_track_audio_graph_node_kind(s, t, i);   // 0 inst / 1 fx / 2 out
                jn["op"] = vivid::session::session_track_audio_graph_node_type(s, t, i);
                float nx = 0.f, ny = 0.f;   // editor position (only when the user has placed it)
                if (vivid::session::session_track_audio_graph_node_pos(s, t, i, &nx, &ny)) { jn["x"] = nx; jn["y"] = ny; }
                json ps = json::object();
                for (int p = 0; p < vivid::session::session_audio_graph_node_param_count(s, t, id); ++p)
                    ps[vivid::session::session_audio_graph_node_param_name(s, t, id, p)] =
                        vivid::session::session_audio_graph_node_param_get(s, t, id, p);
                jn["params"] = ps;
                nodes.push_back(jn);
            }
            const int ne = vivid::session::session_track_audio_graph_edge_count(s, t);
            for (int e = 0; e < ne; ++e)
                edges.push_back({ {"from", vivid::session::session_track_audio_graph_edge_from(s, t, e)},
                                  {"to",   vivid::session::session_track_audio_graph_edge_to(s, t, e)} });
            g["nodes"] = nodes; g["edges"] = edges;
            g["output"] = vivid::session::session_track_audio_graph_output_id(s, t);
            jt["audio_graph"] = g;
        } else {
            if (*vivid::session::session_audio_op_type(s, t, -1)) jt["audio_instrument"] = audio_op(-1);
            json afx = json::array();
            for (int e = 0; e < vivid::session::session_audio_effect_count(s, t); ++e) afx.push_back(audio_op(e));
            if (!afx.empty()) jt["audio_fx"] = afx;
        }

        tracks.push_back(jt);
    }
    j["tracks"] = tracks;

    // Clip pool — loose clips stashed outside the track grid (browser sidebar).
    // Audio pool clips are runtime-only (like grid audio content, their PCM isn't persisted).
    json pool = json::array();
    for (int i = 0; i < vivid::session::session_pool_count(s); ++i) {
        if (vivid::session::session_pool_is_audio(s, i)) continue;
        vivid::session::ClipNote buf[256];
        const int n = vivid::session::session_pool_get(s, i, buf, 256);
        json notes = json::array();
        for (int k = 0; k < n; ++k) {
            json jn = { {"p", buf[k].pitch}, {"s", buf[k].start}, {"d", buf[k].dur}, {"v", buf[k].vel} };
            vivid::session::expr_to_json(buf[k], jn);
            notes.push_back(jn);
        }
        pool.push_back({ {"name", vivid::session::session_pool_name(s, i)},
                         {"length", vivid::session::session_pool_length(s, i)}, {"notes", notes} });
    }
    j["pool"] = pool;

    json jg;
    float sx = 0.f, sy = 0.f; g.get_shader(sx, sy);
    jg["shader"] = { sx, sy };
    json nodes = json::array();
    for (int i = 0; i < g.node_count(); ++i) {
        float x = 0.f, y = 0.f; int cid = 0; std::string title;
        g.get_node(i, x, y, cid, title);
        nodes.push_back({ {"char_id", cid}, {"title", title}, {"x", x}, {"y", y} });
    }
    jg["nodes"] = nodes;
    json maps = json::array();
    for (const auto& m : g.mappings())
        maps.push_back({ {"src", m.source}, {"dst", m.dest}, {"amt", m.amount},
                         {"curve", m.curve}, {"inv", m.invert}, {"lo", m.out_lo}, {"hi", m.out_hi} });
    jg["mappings"] = maps;
    json chain = json::array();
    for (int i = 0; i < g.op_count(); ++i) {
        int op = 0, in = -1, id = 0; float x = 0.f, y = 0.f; g.get_op(i, op, in, id, x, y);
        float base[4]; g.get_op_base(i, base);
        json params = json::object();
        for (int p = 0; p < g.op_param_count_at(i); ++p)
            params[g.op_param_label_at(i, p)] = g.op_param_base_at(i, p);
        json jn = { {"op_type", g.op_type_at(i)}, {"in", in}, {"id", id}, {"x", x}, {"y", y},
                    {"base", { base[0], base[1], base[2], base[3] }}, {"params", params} };
        if (const int inb = g.op_input_b_at(i); inb >= 0) jn["in_b"] = inb;   // 2-in ops (Composite)
        const std::string asset = g.op_asset_at(i);   // CustomShader .glsl (project-relative)
        if (!asset.empty()) jn["asset"] = asset;
        chain.push_back(jn);
    }
    jg["chain"] = chain;
    float vox = 0.f, voy = 0.f, vscale = 1.f; g.get_view(vox, voy, vscale);
    jg["view"] = { {"ox", vox}, {"oy", voy}, {"scale", vscale} };
    j["graph"] = jg;
    return j;
}

// v2: the document owns the track SET. Replace the live tracks with the document's, so
// the existing per-track restore loop (below) then fills state onto them by index. An
// instrument that won't load on this machine falls back to an audio placeholder, keeping
// indices aligned with the document.
static void rebuild_tracks_from_doc(vivid::session::Session* s, const json& T) {
    while (vivid::session::session_track_count(s) > 0)
        vivid::session::session_remove_track(s, vivid::session::session_track_count(s) - 1);
    for (const auto& jt : T) {
        const std::string kind = jt.value("kind", std::string("instrument"));
        int added;
        if (kind == "audio") {
            added = vivid::session::session_add_audio_track(s);
        } else if (jt.contains("audio_graph")) {
            // A rewired (authoritative) track: the graph carries the instrument + effects, so create
            // a bare native track and let the audio_graph block below populate it.
            added = vivid::session::session_add_graph_track(s, jt.value("name", std::string()).c_str());
            // A graph-authoritative track may ALSO carry a CLAP instrument (save writes both keys) —
            // restore it + its state so they aren't lost. NOTE: the authoritative graph loader still
            // recreates only native op nodes, so a CLAP *source node's* graph binding isn't yet rebuilt
            // on load (tracked as a follow-up); the plugin + patch state are at least preserved here.
            if (added >= 0 && jt.contains("clap_instrument"))
                vivid::session::session_request_track_clap_instrument_state(
                    s, added, jt["clap_instrument"].get<std::string>().c_str(),
                    jt.value("state", std::string()).c_str());
        } else if (jt.contains("clap_instrument")) {
            // A CLAP-instrument track: a bare instrument track with the plugin attached from its path.
            // Load ASYNC (a slow plugin ctor must not block load_project on the main thread) and carry
            // the saved patch `state` so it's restored once the load lands (session_poll_plugin_loads).
            added = vivid::session::session_add_graph_track(s, jt.value("name", std::string()).c_str());
            if (added >= 0)
                vivid::session::session_request_track_clap_instrument_state(
                    s, added, jt["clap_instrument"].get<std::string>().c_str(),
                    jt.value("state", std::string()).c_str());
        } else {
            const std::string inst = jt.value("instrument", jt.value("name", std::string()));
            added = vivid::session::session_add_instrument_track(s, inst.c_str());
            if (added < 0) {
                std::fprintf(stderr, "[vivid] load: instrument '%s' unavailable — placeholder audio track\n", inst.c_str());
                added = vivid::session::session_add_audio_track(s);
            }
        }
        // Restore the stable id so saved mappings ("track_<id>.…") reload onto the same track.
        if (added >= 0 && jt.contains("id")) vivid::session::session_set_track_id(s, added, jt.value("id", added));
    }
}

bool session_from_json(const json& j, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                       int& win_w, int& win_h, float& split_x, float& dock_h) {
    if (!s) return false;

    // Version guard: refuse a session written by a NEWER Vivid rather than silently
    // half-reading it (older/equal are read best-effort — fields are all optional).
    int file_ver = 0;
    if (classify_session_version(j, &file_ver) == SessionVersionStatus::TooNew) {
        std::fprintf(stderr, "[vivid] session schema v%d is newer than supported v%d — refusing to load\n",
                     file_ver, kSessionSchemaVersion);
        return false;
    }

    // v2+: rebuild the track set from the document before restoring per-track state.
    // (v1 files keep the pre-built role set and restore onto it by index — migration.)
    if (file_ver >= 2 && j.contains("tracks") && j["tracks"].is_array())
        rebuild_tracks_from_doc(s, j["tracks"]);

    if (j.contains("window")) {
        win_w   = j["window"].value("w", win_w);
        win_h   = j["window"].value("h", win_h);
        split_x = j["window"].value("split", split_x);
        dock_h  = j["window"].value("dock", dock_h);
    }

    if (j.contains("tracks")) {
        const int nt = vivid::session::session_track_count(s);
        const json& T = j["tracks"];
        for (int t = 0; t < nt && t < static_cast<int>(T.size()); ++t) {
            const json& jt = T[t];
            vivid::session::session_set_track_gain(s, t, jt.value("gain", 0.8f));
            if (vivid::session::session_track_is_audio(s, t) && jt.contains("trims")) {
                const json& tr = jt["trims"];
                for (int sc = 0; sc < static_cast<int>(tr.size()); ++sc)
                    if (tr[sc].size() >= 2)
                        vivid::session::session_set_audio_trim(s, t, sc, tr[sc][0].get<float>(), tr[sc][1].get<float>());
                // Per-scene clip shaping (A3); absent in older sessions -> defaults.
                if (jt.contains("audio_clips"))
                    for (int sc = 0; sc < static_cast<int>(jt["audio_clips"].size()); ++sc) {
                        const json& ac = jt["audio_clips"][sc];
                        // Reload the source loop first (if it was from disk); a missing/empty path
                        // leaves the freshly-created track's generated loop in place.
                        const std::string src = ac.value("src_path", std::string());
                        if (!src.empty())
                            vivid::session::session_load_audio_clip(s, t, sc, src.c_str(), ac.value("src_bpm", 0.0));
                        vivid::session::session_set_audio_gain(s, t, sc, ac.value("gain", 1.0f));
                        vivid::session::session_set_audio_pitch(s, t, sc, ac.value("pitch", 0.0f));
                        vivid::session::session_set_audio_reverse(s, t, sc, ac.value("reverse", false) ? 1 : 0);
                        vivid::session::session_set_audio_fades(s, t, sc, ac.value("fade_in_ms", 0.0f),
                                                                ac.value("fade_out_ms", 0.0f), ac.value("loop_xfade_ms", 0.0f));
                        const int warp = ac.value("warp", -1);   // -1 off, else mode 0..2
                        if (warp >= 0) vivid::session::session_set_audio_warp(s, t, sc, 1, warp);
                    }
            } else if (jt.contains("clips")) {
                const json& cl = jt["clips"];
                for (int sc = 0; sc < static_cast<int>(cl.size()); ++sc) {
                    const json& jc = cl[sc];
                    std::vector<vivid::session::ClipNote> notes;
                    if (jc.contains("notes"))
                        for (const auto& jn : jc["notes"]) {
                            vivid::session::ClipNote cn{ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f), {} };
                            vivid::session::expr_from_json(jn, cn);
                            notes.push_back(std::move(cn));
                        }
                    vivid::session::session_set_clip(s, t, sc, notes.data(), static_cast<int>(notes.size()), jc.value("length", 4.0));
                    if (jc.contains("loop_end"))
                        vivid::session::session_set_clip_loop(s, t, sc, jc.value("loop_start", 0.0), jc.value("loop_end", 0.0));
                }
            }
            if (jt.contains("state"))
                vivid::session::session_set_track_state(s, t, jt["state"].get<std::string>());
            if (jt.contains("clap_effects"))   // re-add each CLAP effect (async) + restore its state on land
                for (const auto& je : jt["clap_effects"])   // serial loader preserves the saved chain order
                    vivid::session::session_request_track_clap_effect_state(
                        s, t, je.value("path", std::string()).c_str(),
                        je.value("state", std::string()).c_str());
            if (jt.contains("fx")) {  // rebuild the effect chain: clear, then re-add by catalog name
                while (vivid::session::session_effect_count(s, t) > 0)
                    vivid::session::session_remove_effect(s, t, vivid::session::session_effect_count(s, t) - 1);
                int added = 0;   // positional device index of the next effect (device = added+1)
                for (const auto& jn : jt["fx"]) {
                    const std::string name = jn.is_string() ? jn.get<std::string>() : jn.value("name", std::string());
                    bool ok = false;
                    for (int k = 0; k < vivid::session::session_available_effect_count(); ++k)
                        if (name == vivid::session::session_available_effect_name(k)) {
                            ok = vivid::session::session_add_effect_by_index(s, t, k); break;
                        }
                    if (ok && jn.is_object() && jn.contains("params"))
                        for (const auto& jp : jn["params"])
                            vivid::session::session_set_param(s, t, added + 1, jp.value("id", 0u), jp.value("v", 0.f));
                    if (ok) ++added;
                }
            }
            // Native audio operators (AO-2): recreate the instrument slot + effect chain,
            // applying saved params by name (param order is stable per op type).
            auto apply_audio_params = [&](int index, const json& params) {
                for (auto it = params.begin(); it != params.end(); ++it)
                    for (int p = 0; p < vivid::session::session_audio_op_param_count(s, t, index); ++p)
                        if (it.key() == vivid::session::session_audio_op_param_name(s, t, index, p)) {
                            vivid::session::session_audio_op_param_set(s, t, index, p, it.value().get<float>()); break;
                        }
            };
            if (jt.contains("audio_graph")) {
                // AG-1 step 2: rebuild an authoritative graph. The host assigns fresh node ids, so
                // remap each saved id -> new id and replay edges (+ the output) by the new ids.
                const auto& g = jt["audio_graph"];
                vivid::session::session_audio_graph_clear(s, t);
                std::unordered_map<int, int> id_map;
                if (g.contains("nodes"))
                    for (const auto& jn : g["nodes"]) {
                        const int saved = jn.value("id", -1);
                        const int nid = vivid::session::session_audio_graph_load_node(
                            s, t, jn.value("kind", 1), jn.value("op", std::string()).c_str());
                        if (nid < 0) continue;
                        id_map[saved] = nid;
                        if (jn.contains("x") && jn.contains("y"))   // restore the editor position
                            vivid::session::session_audio_graph_node_set_pos(s, t, nid, jn["x"].get<float>(), jn["y"].get<float>());
                        if (jn.contains("params"))   // set params by name on the new node id
                            for (auto it = jn["params"].begin(); it != jn["params"].end(); ++it)
                                for (int p = 0; p < vivid::session::session_audio_graph_node_param_count(s, t, nid); ++p)
                                    if (it.key() == vivid::session::session_audio_graph_node_param_name(s, t, nid, p)) {
                                        vivid::session::session_audio_graph_node_param_set(s, t, nid, p, it.value().get<float>());
                                        break;
                                    }
                    }
                if (g.contains("edges"))
                    for (const auto& je : g["edges"]) {
                        auto f = id_map.find(je.value("from", -1)), o = id_map.find(je.value("to", -1));
                        if (f != id_map.end() && o != id_map.end())
                            vivid::session::session_audio_graph_load_edge(s, t, f->second, o->second);
                    }
                auto out = id_map.find(g.value("output", -1));
                vivid::session::session_audio_graph_finish_load(s, t, out != id_map.end() ? out->second : -1);
            }
            if (jt.contains("audio_instrument")) {
                const auto& ai = jt["audio_instrument"];
                if (vivid::session::session_set_track_audio_instrument(s, t, ai.value("op", std::string()).c_str())
                    && ai.contains("params")) apply_audio_params(-1, ai["params"]);
            }
            if (jt.contains("audio_fx"))
                for (const auto& jn : jt["audio_fx"]) {
                    const int idx = vivid::session::session_add_audio_effect(s, t, jn.value("op", std::string()).c_str());
                    if (idx >= 0 && jn.contains("params")) apply_audio_params(idx, jn["params"]);
                }
            const int act = jt.value("active", -1);
            if (act >= 0) vivid::session::session_launch_clip(s, t, act);
        }
    }

    // Clip pool — loose clips outside the grid (browser sidebar).
    vivid::session::session_pool_clear(s);
    if (j.contains("pool"))
        for (const auto& jp : j["pool"]) {
            std::vector<vivid::session::ClipNote> notes;
            if (jp.contains("notes"))
                for (const auto& jn : jp["notes"])
                    notes.push_back({ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f) });
            vivid::session::session_pool_add(s, notes.data(), static_cast<int>(notes.size()),
                                             jp.value("length", 4.0), jp.value("name", std::string()).c_str());
        }

    if (j.contains("graph")) {
        const json& jg = j["graph"];
        g.reset_nodes();
        if (jg.contains("chain")) {
            const json& ch = jg["chain"];
            g.chain_load_begin();
            // Ops persist by name ("op_type"). Migrate pre-P1 sessions that stored
            // the legacy VOp int ("op") via legacy_vop_name().
            for (int i = 0; i < static_cast<int>(ch.size()); ++i) {
                std::string type = ch[i].value("op_type", std::string());
                if (type.empty()) type = legacy_vop_name(ch[i].value("op", 0));
                g.chain_load_add(type, ch[i].value("id", i), ch[i].value("x", 0.f), ch[i].value("y", 0.f));
            }
            for (int i = 0; i < static_cast<int>(ch.size()); ++i) {
                g.chain_load_set_input(i, ch[i].value("in", -1));
                g.chain_load_set_input_b(i, ch[i].value("in_b", -1));   // 2-in ops (Composite)
                if (ch[i].contains("asset"))   // CustomShader .glsl reference (project-relative)
                    g.set_op_asset_at(i, ch[i]["asset"].get<std::string>());
                if (ch[i].contains("params") && ch[i]["params"].is_object()) {
                    for (int l = 0; l < g.op_param_count_at(i); ++l) {
                        const char* name = g.op_param_label_at(i, l);
                        if (ch[i]["params"].contains(name))
                            g.set_op_param_base_at(i, l, ch[i]["params"][name].get<float>());
                    }
                }
                if (ch[i].contains("base")) {
                    const json& jb = ch[i]["base"];
                    for (int l = 0; l < 4 && l < static_cast<int>(jb.size()); ++l)
                        if (!ch[i].contains("params"))
                            g.set_op_param_base_at(i, l, jb[l].get<float>());
                }
            }
        }
        if (jg.contains("nodes"))
            for (const auto& jn : jg["nodes"])
                g.add_node_raw(jn.value("title", std::string("node")), jn.value("char_id", 0),
                               jn.value("x", 560.f), jn.value("y", 488.f));
        if (jg.contains("shader") && jg["shader"].size() >= 2)
            g.set_shader(jg["shader"][0].get<float>(), jg["shader"][1].get<float>());
        if (jg.contains("view")) {
            const json& jv = jg["view"];
            g.set_view(jv.value("ox", 0.f), jv.value("oy", 0.f), jv.value("scale", 1.f));
        }
        if (jg.contains("mappings"))
            for (const auto& jm : jg["mappings"])
                g.add_mapping(jm.value("src", std::string()), jm.value("dst", std::string()),
                              jm.value("amt", 1.0f), jm.value("curve", 0.0f), jm.value("inv", false),
                              jm.value("lo", 0.0f), jm.value("hi", 1.0f));
    }
    return true;
}

bool save_session(const std::string& path, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x, float dock_h) {
    if (!s) return false;
    const json j = session_to_json(s, g, win_w, win_h, split_x, dock_h);
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return static_cast<bool>(f);
}

bool load_session(const std::string& path, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x, float& dock_h) {
    if (!s) return false;
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { f >> j; } catch (...) { return false; }
    return session_from_json(j, s, g, win_w, win_h, split_x, dock_h);
}

}  // namespace vivid
