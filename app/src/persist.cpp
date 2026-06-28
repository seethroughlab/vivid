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

bool save_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x) {
    if (!s) return false;
    json j;
    j["version"] = 1;
    j["window"] = { {"w", win_w}, {"h", win_h}, {"split", split_x} };

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
    json wires = json::array();
    for (int p = 0; p < kNumShaderUniforms; ++p) wires.push_back(g.get_connected(p));
    jg["wires"] = wires;
    j["graph"] = jg;

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return static_cast<bool>(f);
}

bool load_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x) {
    if (!s) return false;
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { f >> j; } catch (...) { return false; }

    if (j.contains("window")) {
        win_w   = j["window"].value("w", win_w);
        win_h   = j["window"].value("h", win_h);
        split_x = j["window"].value("split", split_x);
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
            const int act = jt.value("active", -1);
            if (act >= 0) vivid_poc::session_launch_clip(s, t, act);
        }
    }

    if (j.contains("graph")) {
        const json& jg = j["graph"];
        g.reset_nodes();
        if (jg.contains("nodes"))
            for (const auto& jn : jg["nodes"])
                g.add_node_raw(jn.value("title", std::string("node")), jn.value("char_id", 0),
                               jn.value("x", 560.f), jn.value("y", 488.f));
        if (jg.contains("shader") && jg["shader"].size() >= 2)
            g.set_shader(jg["shader"][0].get<float>(), jg["shader"][1].get<float>());
        if (jg.contains("wires")) {
            const json& w = jg["wires"];
            for (int p = 0; p < static_cast<int>(w.size()) && p < kNumShaderUniforms; ++p)
                g.set_connected(p, w[p].get<int>());
        }
    }
    return true;
}

}  // namespace vivid
