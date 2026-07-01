#include "persist.h"
#include "audio/vst3_host.h"
#include "ui/node_graph.h"
#include "gpu/shader_uniforms.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cstdio>

using json = nlohmann::json;

namespace vivid {

json session_to_json(vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                     int win_w, int win_h, float split_x, float dock_h) {
    json j;
    if (!s) return j;
    j["version"] = kSessionSchemaVersion;
    j["window"] = { {"w", win_w}, {"h", win_h}, {"split", split_x}, {"dock", dock_h} };

    const int nt = vivid_poc::session_track_count(s);
    const int ns = vivid_poc::session_scene_count(s);
    json tracks = json::array();
    for (int t = 0; t < nt; ++t) {
        json jt;
        jt["name"]   = vivid_poc::session_track_name(s, t);
        jt["gain"]   = vivid_poc::session_track_gain(s, t);
        jt["active"] = vivid_poc::session_active_clip(s, t);
        const bool aud = vivid_poc::session_track_is_audio(s, t);
        jt["is_audio"] = aud;
        // v2: the track set is the document. `kind` + `instrument` let load recreate the
        // track (the track name is the plugin name, which doubles as the catalog/match spec).
        jt["kind"] = aud ? "audio" : "instrument";
        jt["id"]   = vivid_poc::session_track_id(s, t);   // stable id (mapping sources reference it)
        if (!aud) jt["instrument"] = vivid_poc::session_track_name(s, t);
        if (aud) {
            json trims = json::array();
            for (int sc = 0; sc < ns; ++sc) {
                float a = 0.f, b = 1.f; vivid_poc::session_get_audio_trim(s, t, sc, &a, &b);
                trims.push_back({ a, b });
            }
            jt["trims"] = trims;
        } else {
            json clips = json::array();
            for (int sc = 0; sc < ns; ++sc) {
                vivid_poc::ClipNote buf[256];
                const int n = vivid_poc::session_get_clip(s, t, sc, buf, 256);
                json notes = json::array();
                for (int i = 0; i < n; ++i)
                    notes.push_back({ {"p", buf[i].pitch}, {"s", buf[i].start}, {"d", buf[i].dur}, {"v", buf[i].vel} });
                clips.push_back({ {"length", vivid_poc::session_clip_length(s, t, sc)}, {"notes", notes} });
            }
            jt["clips"] = clips;
            const std::string state = vivid_poc::session_get_track_state(s, t);  // plugin preset
            if (!state.empty()) jt["state"] = state;
        }
        json fx = json::array();   // per-track effect chain (name + exposed param values)
        for (int e = 0; e < vivid_poc::session_effect_count(s, t); ++e) {
            json je; je["name"] = vivid_poc::session_effect_name(s, t, e);
            json ps = json::array();
            const int pc = vivid_poc::session_param_count(s, t, e + 1);  // device = effect+1
            for (int p = 0; p < pc; ++p)
                ps.push_back({ {"id", vivid_poc::session_param_id(s, t, e + 1, p)},
                               {"v",  vivid_poc::session_param_value(s, t, e + 1, p)} });
            if (!ps.empty()) je["params"] = ps;
            fx.push_back(je);
        }
        if (!fx.empty()) jt["fx"] = fx;
        tracks.push_back(jt);
    }
    j["tracks"] = tracks;

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
        chain.push_back({ {"op_type", g.op_type_at(i)}, {"in", in}, {"id", id}, {"x", x}, {"y", y},
                          {"base", { base[0], base[1], base[2], base[3] }}, {"params", params} });
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
static void rebuild_tracks_from_doc(vivid_poc::Session* s, const json& T) {
    while (vivid_poc::session_track_count(s) > 0)
        vivid_poc::session_remove_track(s, vivid_poc::session_track_count(s) - 1);
    for (const auto& jt : T) {
        const std::string kind = jt.value("kind", std::string("instrument"));
        int added;
        if (kind == "audio") {
            added = vivid_poc::session_add_audio_track(s);
        } else {
            const std::string inst = jt.value("instrument", jt.value("name", std::string()));
            added = vivid_poc::session_add_instrument_track(s, inst.c_str());
            if (added < 0) {
                std::fprintf(stderr, "[vivid] load: instrument '%s' unavailable — placeholder audio track\n", inst.c_str());
                added = vivid_poc::session_add_audio_track(s);
            }
        }
        // Restore the stable id so saved mappings ("track_<id>.…") reload onto the same track.
        if (added >= 0 && jt.contains("id")) vivid_poc::session_set_track_id(s, added, jt.value("id", added));
    }
}

bool session_from_json(const json& j, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
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
        const int nt = vivid_poc::session_track_count(s);
        const json& T = j["tracks"];
        for (int t = 0; t < nt && t < static_cast<int>(T.size()); ++t) {
            const json& jt = T[t];
            vivid_poc::session_set_track_gain(s, t, jt.value("gain", 0.8f));
            if (vivid_poc::session_track_is_audio(s, t) && jt.contains("trims")) {
                const json& tr = jt["trims"];
                for (int sc = 0; sc < static_cast<int>(tr.size()); ++sc)
                    if (tr[sc].size() >= 2)
                        vivid_poc::session_set_audio_trim(s, t, sc, tr[sc][0].get<float>(), tr[sc][1].get<float>());
            } else if (jt.contains("clips")) {
                const json& cl = jt["clips"];
                for (int sc = 0; sc < static_cast<int>(cl.size()); ++sc) {
                    const json& jc = cl[sc];
                    std::vector<vivid_poc::ClipNote> notes;
                    if (jc.contains("notes"))
                        for (const auto& jn : jc["notes"])
                            notes.push_back({ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f) });
                    vivid_poc::session_set_clip(s, t, sc, notes.data(), static_cast<int>(notes.size()), jc.value("length", 4.0));
                }
            }
            if (jt.contains("state"))
                vivid_poc::session_set_track_state(s, t, jt["state"].get<std::string>());
            if (jt.contains("fx")) {  // rebuild the effect chain: clear, then re-add by catalog name
                while (vivid_poc::session_effect_count(s, t) > 0)
                    vivid_poc::session_remove_effect(s, t, vivid_poc::session_effect_count(s, t) - 1);
                int added = 0;   // positional device index of the next effect (device = added+1)
                for (const auto& jn : jt["fx"]) {
                    const std::string name = jn.is_string() ? jn.get<std::string>() : jn.value("name", std::string());
                    bool ok = false;
                    for (int k = 0; k < vivid_poc::session_available_effect_count(); ++k)
                        if (name == vivid_poc::session_available_effect_name(k)) {
                            ok = vivid_poc::session_add_effect_by_index(s, t, k); break;
                        }
                    if (ok && jn.is_object() && jn.contains("params"))
                        for (const auto& jp : jn["params"])
                            vivid_poc::session_set_param(s, t, added + 1, jp.value("id", 0u), jp.value("v", 0.f));
                    if (ok) ++added;
                }
            }
            const int act = jt.value("active", -1);
            if (act >= 0) vivid_poc::session_launch_clip(s, t, act);
        }
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

bool save_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x, float dock_h) {
    if (!s) return false;
    const json j = session_to_json(s, g, win_w, win_h, split_x, dock_h);
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return static_cast<bool>(f);
}

bool load_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x, float& dock_h) {
    if (!s) return false;
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { f >> j; } catch (...) { return false; }
    return session_from_json(j, s, g, win_w, win_h, split_x, dock_h);
}

}  // namespace vivid
