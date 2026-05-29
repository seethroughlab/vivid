#include "runtime/graph/graph.h"
#include "runtime/graph/operator_aliases.h"
#include <nlohmann/json.hpp>
#include <dragonbox/dragonbox_to_chars.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <random>
#include <unordered_set>

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

namespace vivid {

static bool split_address(const char* addr, std::string& node, std::string& port) {
    const char* slash = std::strchr(addr, '/');
    if (!slash) return false;
    node.assign(addr, slash);
    port.assign(slash + 1);
    return !node.empty() && !port.empty();
}

static std::vector<std::string> json_str_array(const nlohmann::json& arr) {
    std::vector<std::string> out;
    if (!arr.is_array()) return out;
    for (const auto& v : arr) {
        if (v.is_string()) out.push_back(v.get<std::string>());
    }
    return out;
}

bool Graph::load(const char* path) {
    source_path_ = path;
    try {
        std::ifstream ifs(path);
        if (!ifs) {
            std::fprintf(stderr, "[vivid] Graph: failed to read %s: could not open file\n", path);
            return false;
        }
        auto root = nlohmann::json::parse(ifs);
        bool ok = load_from_json_doc(root, true, false);
        if (ok) {
            std::fprintf(stderr, "[vivid] Loaded graph: %s (%zu nodes, %zu connections)\n",
                path, nodes_.size(), connections_.size());
        }
        return ok;
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Graph: failed to read %s: %s\n", path, e.what());
        return false;
    }
}

bool Graph::load_from_string(const char* json, size_t len, bool preserve_source_path) {
    if (!preserve_source_path)
        source_path_.clear();

    if (len == 0) len = std::strlen(json);

    try {
        auto root = nlohmann::json::parse(json, json + len);
        bool ok = load_from_json_doc(root, true, false);
        if (ok) {
            std::fprintf(stderr, "[vivid] Loaded graph from string (%zu nodes, %zu connections)\n",
                nodes_.size(), connections_.size());
        }
        return ok;
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Graph: failed to parse JSON string: %s\n", e.what());
        return false;
    }
}

bool Graph::load_from_json_doc(const nlohmann::json& root,
                               bool preserve_source_path,
                               bool quiet) {
    if (!preserve_source_path)
        source_path_.clear();
    (void)quiet;
    return parse_doc(root);
}

// Parse common NodeDef fields from a JSON object. Does NOT set node.id (caller's job).
static bool parse_node_fields(const nlohmann::json& val, NodeDef& node) {
    // type (raw — alias resolution happens after params are parsed)
    std::string raw_type;
    auto type_it = val.find("type");
    if (type_it != val.end() && type_it->is_string())
        raw_type = type_it->get<std::string>();

    // pkg
    auto pkg_it = val.find("pkg");
    if (pkg_it != val.end() && pkg_it->is_object()) {
        auto pn = pkg_it->find("name");
        auto pv = pkg_it->find("version");
        node.pkg_name    = (pn != pkg_it->end() && pn->is_string()) ? pn->get<std::string>() : "";
        node.pkg_version = (pv != pkg_it->end() && pv->is_string()) ? pv->get<std::string>() : "";
    }

    // params
    auto params_it = val.find("params");
    if (params_it != val.end() && params_it->is_object()) {
        for (auto& [pkey, pval] : params_it->items()) {
            if (pval.is_number())
                node.params[pkey] = static_cast<float>(pval.get<double>());
            else if (pval.is_string())
                node.string_params[pkey] = pval.get<std::string>();
        }
    }

    // Apply operator-id alias migration (no-op for current/unknown types).
    // Only applies to core operators — package-provided types are left alone.
    if (!raw_type.empty() && node.pkg_name.empty()) {
        node.type = resolve_operator_alias(raw_type, node.params, node.string_params);
    } else {
        node.type = raw_type;
    }

    // layout
    auto layout_it = val.find("layout");
    if (layout_it != val.end() && layout_it->is_object()) {
        auto lx = layout_it->find("x");
        auto ly = layout_it->find("y");
        if (lx != layout_it->end() && lx->is_number() && ly != layout_it->end() && ly->is_number()) {
            node.layout_x = static_cast<float>(lx->get<double>());
            node.layout_y = static_cast<float>(ly->get<double>());
        }
    }

    // resolution
    auto res_it = val.find("resolution");
    if (res_it != val.end() && res_it->is_array() && res_it->size() == 2) {
        const auto& rw = (*res_it)[0];
        const auto& rh = (*res_it)[1];
        if (rw.is_number_integer() && rh.is_number_integer()) {
            node.tex_width  = static_cast<uint32_t>(rw.get<int64_t>());
            node.tex_height = static_cast<uint32_t>(rh.get<int64_t>());
            if (node.tex_width > 8192 || node.tex_height > 8192) {
                node.tex_width = 0;
                node.tex_height = 0;
            }
        }
    }

    // "cadence" key silently ignored for backward compat with old graphs.

    // locks
    auto locks_it = val.find("locks");
    if (locks_it != val.end() && locks_it->is_object()) {
        for (auto& [lkey, lval] : locks_it->items()) {
            if (lval.is_number_integer())
                node.param_lock_flags[lkey] = static_cast<uint8_t>(lval.get<int64_t>());
        }
    }

    // bypass (optional, defaults false)
    auto bypass_it = val.find("bypassed");
    if (bypass_it != val.end() && bypass_it->is_boolean())
        node.bypassed = bypass_it->get<bool>();

    // Subgraph module membership (present in flattened graphs)
    auto sg_owner_it = val.find("subgraph_owner");
    if (sg_owner_it != val.end() && sg_owner_it->is_string())
        node.subgraph_owner = sg_owner_it->get<std::string>();
    auto sg_type_it = val.find("subgraph_type");
    if (sg_type_it != val.end() && sg_type_it->is_string())
        node.subgraph_type = sg_type_it->get<std::string>();

    return true;
}

bool Graph::parse_doc(const nlohmann::json& root) {
    nodes_.clear();
    connections_.clear();
    midi_mappings_.clear();
    session_ = {};
    node_presets_.clear();
    state_preset_mappings_.clear();
    sticky_notes_.clear();
    mod_assignments_.clear();
    load_diagnostics.clear();
    quantize_clock_node_.clear();
    metronome_ = {};
    meta_ = {};

    // Schema version — hard-reject if from the future
    auto sv_it = root.find("schema_version");
    schema_version = (sv_it != root.end() && sv_it->is_number_integer()) ? static_cast<int>(sv_it->get<int64_t>()) : 1;
    if (schema_version > GRAPH_SCHEMA_VERSION) {
        std::fprintf(stderr, "[vivid] Graph: schema_version %d > %d — refusing to load.\n",
                     schema_version, GRAPH_SCHEMA_VERSION);
        return false;
    }

    auto vv_it = root.find("vivid_version");
    if (vv_it != root.end() && vv_it->is_string())
        vivid_version = vv_it->get<std::string>();
    else
        vivid_version.clear();
    if (root.contains("filters")) {
        std::fprintf(stderr,
                     "[vivid] Graph: schema %d uses removed graph-level filters; this format is no longer supported.\n",
                     schema_version);
        return false;
    }

    auto meta_it = root.find("meta");
    if (meta_it != root.end() && meta_it->is_object()) {
        const auto& meta = *meta_it;
        auto set_string = [&](const char* key, std::string& dest) {
            auto it = meta.find(key);
            if (it != meta.end() && it->is_string())
                dest = it->get<std::string>();
        };
        set_string("id", meta_.id);
        set_string("title", meta_.title);
        set_string("description", meta_.description);
        set_string("difficulty", meta_.difficulty);
        meta_.tags = json_str_array(meta.value("tags", nlohmann::json()));
        if (meta.contains("domains"))
            meta_.domains = json_str_array(meta["domains"]);
        else
            meta_.domains = json_str_array(meta.value("envs", nlohmann::json()));
        meta_.requires_packages = json_str_array(meta.value("requires_packages", nlohmann::json()));
        auto fr_it = meta.find("featured_rank");
        if (fr_it != meta.end() && fr_it->is_number_integer())
            meta_.featured_rank = fr_it->get<int>();
        auto em_it = meta.find("estimated_minutes");
        if (em_it != meta.end() && em_it->is_number_integer())
            meta_.estimated_minutes = em_it->get<int>();
        set_string("content_kind", meta_.content_kind);
        set_string("category", meta_.category);
        set_string("family", meta_.family);
        set_string("role", meta_.role);
        set_string("playability", meta_.playability);
        auto pc_it = meta.find("preview_controls");
        if (pc_it != meta.end() && pc_it->is_array()) {
            for (const auto& pc : *pc_it) {
                if (!pc.is_object()) continue;
                GraphPreviewControl ctrl;
                auto node_it = pc.find("node");
                auto param_it = pc.find("param");
                auto label_it = pc.find("label");
                if (node_it != pc.end() && node_it->is_string())
                    ctrl.node = node_it->get<std::string>();
                if (param_it != pc.end() && param_it->is_string())
                    ctrl.param = param_it->get<std::string>();
                if (label_it != pc.end() && label_it->is_string())
                    ctrl.label = label_it->get<std::string>();
                if (!ctrl.node.empty() && !ctrl.param.empty())
                    meta_.preview_controls.push_back(std::move(ctrl));
            }
        }
    }

    // Parse nodes
    auto nodes_it = root.find("nodes");
    if (nodes_it != root.end() && nodes_it->is_object()) {
        for (auto& [key, val] : nodes_it->items()) {
            NodeDef node;
            node.id = key;

            auto type_it = val.find("type");
            if (type_it == val.end() || !type_it->is_string()) {
                std::fprintf(stderr, "[vivid] Graph: node '%s' missing type\n", node.id.c_str());
                return false;
            }

            parse_node_fields(val, node);
            if (node.type == "WGSLFilter") {
                std::fprintf(stderr,
                             "[vivid] Graph: node '%s' uses removed type WGSLFilter; this format is no longer supported.\n",
                             node.id.c_str());
                return false;
            }

            if (find_node(node.id)) {
                std::fprintf(stderr, "[vivid] Graph: duplicate node id '%s', skipping\n", node.id.c_str());
                continue;
            }
            nodes_.push_back(std::move(node));
        }
    }

    // Parse connections
    auto conns_it = root.find("connections");
    if (conns_it != root.end() && conns_it->is_array()) {
        for (auto& val : *conns_it) {
            auto from_it = val.find("from");
            auto to_it   = val.find("to");
            if (from_it == val.end() || !from_it->is_string() || to_it == val.end() || !to_it->is_string()) {
                std::fprintf(stderr, "[vivid] Graph: connection 'from'/'to' must be strings, skipping\n");
                continue;
            }

            ConnectionDef conn;
            if (!split_address(from_it->get<std::string>().c_str(), conn.from_node, conn.from_port) ||
                !split_address(to_it->get<std::string>().c_str(),   conn.to_node,   conn.to_port)) {
                std::fprintf(stderr, "[vivid] Graph: invalid connection address\n");
                continue;
            }
            // Remap fields (new format)
            auto fmin_it = val.find("from_min");
            auto fmax_it = val.find("from_max");
            auto tmin_it = val.find("to_min");
            auto tmax_it = val.find("to_max");
            auto clamp_it = val.find("clamp");
            if (fmin_it != val.end() || fmax_it != val.end() || tmin_it != val.end() || tmax_it != val.end()) {
                if (fmin_it != val.end() && fmin_it->is_number())
                    conn.from_min = static_cast<float>(fmin_it->get<double>());
                if (fmax_it != val.end() && fmax_it->is_number())
                    conn.from_max = static_cast<float>(fmax_it->get<double>());
                if (tmin_it != val.end() && tmin_it->is_number())
                    conn.to_min = static_cast<float>(tmin_it->get<double>());
                if (tmax_it != val.end() && tmax_it->is_number())
                    conn.to_max = static_cast<float>(tmax_it->get<double>());
                if (clamp_it != val.end() && clamp_it->is_boolean())
                    conn.clamp = clamp_it->get<bool>();
            }
            auto curve_it = val.find("curve");
            if (curve_it != val.end() && curve_it->is_number_unsigned())
                conn.curve = static_cast<uint8_t>(curve_it->get<unsigned>());
            auto bridge_it = val.find("bridge");
            if (bridge_it != val.end() && bridge_it->is_string())
                conn.bridge = bridge_it->get<std::string>();
            // Deduplicate: skip if an identical connection already exists
            bool dup = false;
            for (const auto& c : connections_) {
                if (c.from_node == conn.from_node && c.from_port == conn.from_port &&
                    c.to_node   == conn.to_node   && c.to_port   == conn.to_port) {
                    dup = true; break;
                }
            }
            if (dup) {
                std::fprintf(stderr, "[vivid] Graph: duplicate connection %s/%s -> %s/%s, skipping\n",
                             conn.from_node.c_str(), conn.from_port.c_str(),
                             conn.to_node.c_str(),   conn.to_port.c_str());
                continue;
            }
            connections_.push_back(std::move(conn));
        }
    }

    // Parse MIDI mappings
    auto midi_it = root.find("midi_mappings");
    if (midi_it != root.end() && midi_it->is_array()) {
        for (auto& mval : *midi_it) {
            MidiMappingDef mm;
            auto node_it = mval.find("node");
            auto param_it = mval.find("param");
            auto cc_it = mval.find("cc");
            if (node_it == mval.end() || param_it == mval.end() || cc_it == mval.end()) continue;
            mm.node_id = node_it->get<std::string>();
            mm.param_name = param_it->get<std::string>();
            mm.cc_number = static_cast<int>(cc_it->get<int64_t>());
            auto chan_it = mval.find("channel");
            if (chan_it != mval.end() && chan_it->is_number_integer())
                mm.channel = static_cast<int>(chan_it->get<int64_t>());
            if (mm.cc_number < 0 || mm.cc_number > 127) {
                std::fprintf(stderr, "[vivid] Graph: MIDI cc %d out of range [0,127], skipping\n", mm.cc_number);
                continue;
            }
            if (mm.channel < 0 || mm.channel > 16) {
                std::fprintf(stderr, "[vivid] Graph: MIDI channel %d out of range [0,16], skipping\n", mm.channel);
                continue;
            }
            auto rmin_it = mval.find("range_min");
            if (rmin_it != mval.end() && rmin_it->is_number())
                mm.range_min = static_cast<float>(rmin_it->get<double>());
            auto rmax_it = mval.find("range_max");
            if (rmax_it != mval.end() && rmax_it->is_number())
                mm.range_max = static_cast<float>(rmax_it->get<double>());
            midi_mappings_.push_back(std::move(mm));
        }
    }

    // Parse viewport
    auto vp_it = root.find("viewport");
    if (vp_it != root.end() && vp_it->is_object()) {
        auto vpx = vp_it->find("pan_x");
        auto vpy = vp_it->find("pan_y");
        auto vpz = vp_it->find("zoom");
        if (vpx != vp_it->end() && vpx->is_number() && vpy != vp_it->end() && vpy->is_number() && vpz != vp_it->end() && vpz->is_number()) {
            viewport_pan_x = static_cast<float>(vpx->get<double>());
            viewport_pan_y = static_cast<float>(vpy->get<double>());
            viewport_zoom  = static_cast<float>(vpz->get<double>());
        }
    }

    // Legacy variation data warning
    if (root.contains("variations")) {
        std::fprintf(stderr, "[session] legacy variation data ignored (pre-alpha schema break)\n");
    }

    // Parse session
    auto sess_it = root.find("session");
    if (sess_it != root.end() && sess_it->is_object()) {
        std::unordered_set<std::string> seen_track_ids;
        std::unordered_set<std::string> owned_nodes_global; // enforce single-track ownership
        // parse tracks
        auto tracks_it = sess_it->find("tracks");
        if (tracks_it != sess_it->end() && tracks_it->is_array()) {
            for (const auto& tval : *tracks_it) {
                auto tid_it = tval.find("id");
                if (tid_it == tval.end() || !tid_it->is_string()) continue;
                std::string track_id = tid_it->get<std::string>();
                if (seen_track_ids.count(track_id)) {
                    std::fprintf(stderr, "[session] duplicate track id '%s', skipping\n", track_id.c_str());
                    continue;
                }
                seen_track_ids.insert(track_id);
                SessionTrackDef track;
                track.id = track_id;
                auto tname = tval.find("name");
                if (tname != tval.end() && tname->is_string())
                    track.name = tname->get<std::string>();
                auto owned = tval.find("owned_nodes");
                if (owned != tval.end() && owned->is_array()) {
                    for (const auto& n : *owned) {
                        if (!n.is_string()) continue;
                        std::string nid = n.get<std::string>();
                        if (owned_nodes_global.count(nid)) {
                            std::fprintf(stderr, "[session] node '%s' owned by multiple tracks, skipping duplicate\n", nid.c_str());
                            continue;
                        }
                        owned_nodes_global.insert(nid);
                        track.owned_node_ids.push_back(nid);
                    }
                }
                auto dt_it = tval.find("default_transition");
                if (dt_it != tval.end() && dt_it->is_object()) {
                    auto mode = dt_it->find("mode");
                    if (mode != dt_it->end() && mode->is_string())
                        track.default_transition.fade = (mode->get<std::string>() == "fade");
                    auto dur = dt_it->find("duration_bars");
                    if (dur != dt_it->end() && dur->is_number())
                        track.default_transition.duration_bars = static_cast<float>(dur->get<double>());
                }
                std::unordered_set<std::string> seen_clip_ids;
                auto clips_it = tval.find("clips");
                if (clips_it != tval.end() && clips_it->is_array()) {
                    for (const auto& cval : *clips_it) {
                        auto cid_it = cval.find("id");
                        if (cid_it == cval.end() || !cid_it->is_string()) continue;
                        std::string clip_id = cid_it->get<std::string>();
                        if (seen_clip_ids.count(clip_id)) {
                            std::fprintf(stderr, "[session] duplicate clip id '%s' in track '%s', skipping\n",
                                         clip_id.c_str(), track_id.c_str());
                            continue;
                        }
                        seen_clip_ids.insert(clip_id);
                        SessionClipDef clip;
                        clip.id = clip_id;
                        auto cname = cval.find("name");
                        if (cname != cval.end() && cname->is_string())
                            clip.name = cname->get<std::string>();
                        auto ct_it = cval.find("transition_override");
                        if (ct_it != cval.end() && ct_it->is_object()) {
                            SessionTransitionDef transition;
                            auto mode = ct_it->find("mode");
                            if (mode != ct_it->end() && mode->is_string())
                                transition.fade = (mode->get<std::string>() == "fade");
                            auto dur = ct_it->find("duration_bars");
                            if (dur != ct_it->end() && dur->is_number())
                                transition.duration_bars = static_cast<float>(dur->get<double>());
                            clip.transition_override = transition;
                        }
                        auto cparams = cval.find("params");
                        if (cparams != cval.end() && cparams->is_object()) {
                            for (auto& [nid, nval] : cparams->items()) {
                                if (!nval.is_object()) continue;
                                for (auto& [pkey, pv] : nval.items()) {
                                    if (pv.is_number())
                                        clip.params[nid][pkey] = static_cast<float>(pv.get<double>());
                                    else if (pv.is_string())
                                        clip.string_params[nid][pkey] = pv.get<std::string>();
                                }
                            }
                        }
                        track.clips.push_back(std::move(clip));
                    }
                }
                session_.tracks.push_back(std::move(track));
            }
        }
        // parse scenes
        std::unordered_set<std::string> seen_scene_ids;
        auto scenes_it = sess_it->find("scenes");
        if (scenes_it != sess_it->end() && scenes_it->is_array()) {
            for (const auto& sval : *scenes_it) {
                auto sid_it = sval.find("id");
                if (sid_it == sval.end() || !sid_it->is_string()) continue;
                std::string scene_id = sid_it->get<std::string>();
                if (seen_scene_ids.count(scene_id)) {
                    std::fprintf(stderr, "[session] duplicate scene id '%s', skipping\n", scene_id.c_str());
                    continue;
                }
                seen_scene_ids.insert(scene_id);
                SessionSceneDef scene;
                scene.id = scene_id;
                auto sname = sval.find("name");
                if (sname != sval.end() && sname->is_string())
                    scene.name = sname->get<std::string>();
                auto asn_it = sval.find("assignments");
                if (asn_it != sval.end() && asn_it->is_object()) {
                    for (auto& [tid, cval] : asn_it->items()) {
                        if (!find_track(tid)) {
                            std::fprintf(stderr, "[session] scene '%s' assignment references unknown track '%s', skipping\n",
                                         scene_id.c_str(), tid.c_str());
                            continue;
                        }
                        if (cval.is_null()) {
                            scene.leave_unchanged.insert(tid);
                        } else if (cval.is_string()) {
                            std::string clip_id = cval.get<std::string>();
                            if (clip_id.empty()) {
                                std::fprintf(stderr, "[session] scene '%s' assignment for track '%s' has empty clip id, skipping\n",
                                             scene_id.c_str(), tid.c_str());
                                continue;
                            }
                            if (!find_clip(tid, clip_id)) {
                                std::fprintf(stderr, "[session] scene '%s' assignment references missing clip '%s' for track '%s', skipping\n",
                                             scene_id.c_str(), clip_id.c_str(), tid.c_str());
                                continue;
                            }
                            scene.assignments[tid] = std::move(clip_id);
                        }
                    }
                }
                session_.scenes.push_back(std::move(scene));
            }
        }
        // parse cue paths
        std::unordered_set<std::string> seen_cue_path_ids;
        auto cue_paths_it = sess_it->find("cue_paths");
        if (cue_paths_it != sess_it->end() && cue_paths_it->is_array()) {
            for (const auto& pval : *cue_paths_it) {
                auto pid_it = pval.find("id");
                if (pid_it == pval.end() || !pid_it->is_string()) continue;
                std::string path_id = pid_it->get<std::string>();
                if (seen_cue_path_ids.count(path_id)) {
                    std::fprintf(stderr, "[session] duplicate cue path id '%s', skipping\n",
                                 path_id.c_str());
                    continue;
                }
                seen_cue_path_ids.insert(path_id);
                SessionCuePathDef path;
                path.id = path_id;
                auto pname = pval.find("name");
                if (pname != pval.end() && pname->is_string())
                    path.name = pname->get<std::string>();

                std::unordered_set<std::string> seen_step_ids;
                auto steps_it = pval.find("steps");
                if (steps_it != pval.end() && steps_it->is_array()) {
                    for (const auto& step_val : *steps_it) {
                        auto sid_it = step_val.find("id");
                        auto scid_it = step_val.find("scene_id");
                        if (sid_it == step_val.end() || !sid_it->is_string() ||
                            scid_it == step_val.end() || !scid_it->is_string())
                            continue;
                        std::string step_id = sid_it->get<std::string>();
                        if (seen_step_ids.count(step_id)) {
                            std::fprintf(stderr,
                                         "[session] duplicate cue step id '%s' in path '%s', skipping\n",
                                         step_id.c_str(), path_id.c_str());
                            continue;
                        }
                        std::string scene_id = scid_it->get<std::string>();
                        if (!find_scene(scene_id)) {
                            std::fprintf(stderr,
                                         "[session] cue path '%s' step '%s' references missing scene '%s', skipping\n",
                                         path_id.c_str(), step_id.c_str(), scene_id.c_str());
                            continue;
                        }
                        seen_step_ids.insert(step_id);
                        SessionCueStepDef step;
                        step.id = std::move(step_id);
                        step.scene_id = std::move(scene_id);
                        auto mode_it = step_val.find("advance_mode");
                        if (mode_it != step_val.end() && mode_it->is_string()) {
                            std::string mode = mode_it->get<std::string>();
                            if (mode == "manual" || mode == "after_bars" ||
                                mode == "on_scene_launch")
                                step.advance_mode = std::move(mode);
                        }
                        auto bars_it = step_val.find("bars");
                        if (bars_it != step_val.end() && bars_it->is_number_integer())
                            step.bars = std::max(0, bars_it->get<int>());
                        if (step.advance_mode == "after_bars" && step.bars <= 0)
                            step.bars = 1;
                        path.steps.push_back(std::move(step));
                    }
                }
                session_.cue_paths.push_back(std::move(path));
            }
        }
        // parse active_clips
        auto ac_it = sess_it->find("active_clips");
        if (ac_it != sess_it->end() && ac_it->is_object()) {
            for (auto& [tid, cval] : ac_it->items()) {
                if (!cval.is_string()) continue;
                std::string clip_id = cval.get<std::string>();
                if (find_track(tid) && find_clip(tid, clip_id))
                    session_.active_clips[tid] = std::move(clip_id);
            }
        }
    }

    // Parse quantize_clock
    auto qc_it = root.find("quantize_clock");
    if (qc_it != root.end() && qc_it->is_string())
        quantize_clock_node_ = qc_it->get<std::string>();

    // Parse graph metronome
    auto met_it = root.find("metronome");
    if (met_it != root.end() && met_it->is_object()) {
        const auto& met = *met_it;
        auto bpm_it = met.find("bpm");
        auto bpb_it = met.find("beats_per_bar");
        if (bpm_it != met.end() && bpm_it->is_number())
            metronome_.bpm = static_cast<float>(bpm_it->get<double>());
        if (bpb_it != met.end() && bpb_it->is_number_integer())
            metronome_.beats_per_bar = static_cast<int>(bpb_it->get<int64_t>());
        metronome_.bpm = std::max(1.0f, std::min(300.0f, metronome_.bpm));
        metronome_.beats_per_bar = std::max(1, std::min(16, metronome_.beats_per_bar));
    }

    // Parse per-operator presets
    auto presets_it = root.find("presets");
    if (presets_it != root.end() && presets_it->is_object()) {
        for (auto& [node_id, prval] : presets_it->items()) {
            if (prval.is_array()) {
                auto& presets = node_presets_[node_id];
                for (auto& pentry : prval) {
                    OperatorPreset op;
                    auto pn = pentry.find("name");
                    if (pn != pentry.end() && pn->is_string())
                        op.name = pn->get<std::string>();
                    auto pp = pentry.find("params");
                    if (pp != pentry.end() && pp->is_object()) {
                        for (auto& [ppkey, ppv] : pp->items()) {
                            if (ppv.is_number())
                                op.params[ppkey] = static_cast<float>(ppv.get<double>());
                            else if (ppv.is_string())
                                op.string_params[ppkey] = ppv.get<std::string>();
                        }
                    }
                    presets.push_back(std::move(op));
                }
            }
        }
    }

    // Parse state_preset_mappings
    auto spm_it = root.find("state_preset_mappings");
    if (spm_it != root.end() && spm_it->is_array()) {
        for (auto& sentry : *spm_it) {
            StatePresetMapping spm;
            auto sn = sentry.find("node");
            if (sn != sentry.end() && sn->is_string())
                spm.state_machine_node = sn->get<std::string>();
            auto states_it = sentry.find("states");
            if (states_it != sentry.end() && states_it->is_array()) {
                for (auto& state_obj : *states_it) {
                    std::unordered_map<std::string, std::string> bindings;
                    if (state_obj.is_object()) {
                        for (auto& [bkey, bval] : state_obj.items()) {
                            if (bval.is_string())
                                bindings[bkey] = bval.get<std::string>();
                        }
                    }
                    spm.state_presets.push_back(std::move(bindings));
                }
            }
            state_preset_mappings_.push_back(std::move(spm));
        }
    }

    // Parse sticky notes
    auto sticky_it = root.find("sticky_notes");
    if (sticky_it != root.end() && sticky_it->is_array()) {
        for (auto& snval : *sticky_it) {
            StickyNoteDef sn;
            auto sn_id = snval.find("id");
            if (sn_id != snval.end() && sn_id->is_string())
                sn.id = sn_id->get<std::string>();
            auto sn_text = snval.find("text");
            if (sn_text != snval.end() && sn_text->is_string())
                sn.text = sn_text->get<std::string>();
            auto sn_x = snval.find("x");
            if (sn_x != snval.end() && sn_x->is_number())
                sn.x = static_cast<float>(sn_x->get<double>());
            auto sn_y = snval.find("y");
            if (sn_y != snval.end() && sn_y->is_number())
                sn.y = static_cast<float>(sn_y->get<double>());
            auto sn_w = snval.find("width");
            if (sn_w != snval.end() && sn_w->is_number())
                sn.width = static_cast<float>(sn_w->get<double>());
            auto sn_h = snval.find("height");
            if (sn_h != snval.end() && sn_h->is_number())
                sn.height = static_cast<float>(sn_h->get<double>());
            auto sn_color = snval.find("color");
            if (sn_color != snval.end() && sn_color->is_number_integer())
                sn.color = static_cast<int>(sn_color->get<int64_t>());
            sticky_notes_.push_back(std::move(sn));
        }
    }

    // Parse modulation assignments
    auto ma_it = root.find("mod_assignments");
    if (ma_it != root.end() && ma_it->is_object()) {
        for (const auto& [node_id, arr] : ma_it->items()) {
            if (!arr.is_array()) continue;
            for (const auto& aval : arr) {
                ModAssignmentDef a;
                auto src = aval.find("source");
                auto dst = aval.find("destination");
                if (src == aval.end() || !src->is_string() ||
                    dst == aval.end() || !dst->is_string())
                    continue;
                a.source = src->get<std::string>();
                a.destination = dst->get<std::string>();
                if (auto it = aval.find("amount"); it != aval.end() && it->is_number())
                    a.amount = static_cast<float>(it->get<double>());
                if (auto it = aval.find("polarity"); it != aval.end() && it->is_string())
                    a.polarity = it->get<std::string>();
                if (a.polarity.empty()) a.polarity = "unipolar";
                if (auto it = aval.find("curve"); it != aval.end() && it->is_string())
                    a.curve = it->get<std::string>();
                if (a.curve.empty()) a.curve = "linear";
                mod_assignments_[node_id].push_back(std::move(a));
            }
        }
    }

    return true;
}

// --- Mutation ---

bool Graph::add_node(const std::string& id, const std::string& type,
                     const std::unordered_map<std::string, float>& params,
                     const std::unordered_map<std::string, std::string>& string_params) {
    if (find_node(id)) return false;  // duplicate id
    NodeDef node;
    node.id = id;
    node.type = type;
    node.params = params;
    node.string_params = string_params;
    nodes_.push_back(std::move(node));
    return true;
}

bool Graph::remove_node(const std::string& id) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [&](const NodeDef& n) { return n.id == id; });
    if (it == nodes_.end()) return false;
    nodes_.erase(it);
    // Remove all connections involving this node
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [&](const ConnectionDef& c) {
                return c.from_node == id || c.to_node == id;
            }),
        connections_.end());
    // Remove all MIDI mappings referencing this node
    midi_mappings_.erase(
        std::remove_if(midi_mappings_.begin(), midi_mappings_.end(),
            [&](const MidiMappingDef& m) { return m.node_id == id; }),
        midi_mappings_.end());
    // Remove per-operator presets for this node
    node_presets_.erase(id);
    // Remove this node from session track ownership and clip ParamSets
    for (auto& track : session_.tracks) {
        auto it = std::find(track.owned_node_ids.begin(), track.owned_node_ids.end(), id);
        if (it != track.owned_node_ids.end())
            track.owned_node_ids.erase(it);
        for (auto& clip : track.clips) {
            clip.params.erase(id);
            clip.string_params.erase(id);
        }
    }
    // Clean up state-preset mappings referencing this node
    // Remove mappings where this node IS the state machine
    state_preset_mappings_.erase(
        std::remove_if(state_preset_mappings_.begin(), state_preset_mappings_.end(),
            [&](const StatePresetMapping& m) { return m.state_machine_node == id; }),
        state_preset_mappings_.end());
    // For remaining mappings, erase this node from each state's target map
    for (auto& m : state_preset_mappings_) {
        for (auto& bindings : m.state_presets) {
            bindings.erase(id);
        }
    }
    return true;
}

bool Graph::add_connection(const std::string& from_node, const std::string& from_port,
                           const std::string& to_node, const std::string& to_port) {
    // Check for duplicate
    for (const auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port)
            return false;
    }
    connections_.push_back({from_node, from_port, to_node, to_port});
    return true;
}

bool Graph::remove_connection(const std::string& from_node, const std::string& from_port,
                              const std::string& to_node, const std::string& to_port) {
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [&](const ConnectionDef& c) {
            return c.from_node == from_node && c.from_port == from_port &&
                   c.to_node == to_node && c.to_port == to_port;
        });
    if (it == connections_.end()) return false;
    connections_.erase(it);
    return true;
}

bool Graph::set_connection_remap(const std::string& from_node, const std::string& from_port,
                                  const std::string& to_node, const std::string& to_port,
                                  float from_min, float from_max, float to_min, float to_max,
                                  bool clamp, uint8_t curve) {
    for (auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port) {
            c.from_min = from_min;
            c.from_max = from_max;
            c.to_min   = to_min;
            c.to_max   = to_max;
            c.clamp    = clamp;
            c.curve    = curve;
            return true;
        }
    }
    return false;
}

bool Graph::set_connection_bridge(const std::string& from_node, const std::string& from_port,
                                   const std::string& to_node, const std::string& to_port,
                                   const std::string& bridge) {
    for (auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port) {
            c.bridge = bridge;
            return true;
        }
    }
    return false;
}

const NodeDef* Graph::find_node(const std::string& id) const {
    for (const auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

NodeDef* Graph::find_node(const std::string& id) {
    for (auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

// --- MIDI Mapping Mutation ---

bool Graph::add_midi_mapping(const std::string& node_id, const std::string& param,
                             int cc, int channel, float range_min, float range_max) {
    // Replace existing mapping for the same node/param
    for (auto& m : midi_mappings_) {
        if (m.node_id == node_id && m.param_name == param) {
            m.cc_number = cc;
            m.channel = channel;
            m.range_min = range_min;
            m.range_max = range_max;
            return true;
        }
    }
    midi_mappings_.push_back({node_id, param, cc, channel, range_min, range_max});
    return true;
}

bool Graph::remove_midi_mapping(const std::string& node_id, const std::string& param) {
    auto it = std::find_if(midi_mappings_.begin(), midi_mappings_.end(),
        [&](const MidiMappingDef& m) {
            return m.node_id == node_id && m.param_name == param;
        });
    if (it == midi_mappings_.end()) return false;
    midi_mappings_.erase(it);
    return true;
}

bool Graph::update_midi_mapping(const std::string& node_id, const std::string& param,
                                float range_min, float range_max) {
    for (auto& m : midi_mappings_) {
        if (m.node_id == node_id && m.param_name == param) {
            m.range_min = range_min;
            m.range_max = range_max;
            return true;
        }
    }
    return false;
}

const MidiMappingDef* Graph::find_midi_mapping(const std::string& node_id,
                                               const std::string& param) const {
    for (const auto& m : midi_mappings_) {
        if (m.node_id == node_id && m.param_name == param) return &m;
    }
    return nullptr;
}

// --- Session ID generator ---

std::string Graph::gen_session_id(std::string_view prefix) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist;
    char buf[32];
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::snprintf(buf, sizeof(buf), "%.*s_%08x",
                      static_cast<int>(prefix.size()), prefix.data(), dist(rng));
        std::string id(buf);
        // Check for collisions across all session IDs
        bool used = false;
        for (const auto& t : session_.tracks) {
            if (t.id == id) { used = true; break; }
            for (const auto& c : t.clips)
                if (c.id == id) { used = true; break; }
            if (used) break;
        }
        if (!used) for (const auto& s : session_.scenes)
            if (s.id == id) { used = true; break; }
        if (!used) for (const auto& p : session_.cue_paths) {
            if (p.id == id) { used = true; break; }
            for (const auto& step : p.steps)
                if (step.id == id) { used = true; break; }
            if (used) break;
        }
        if (!used) return id;
    }
    return std::string(buf); // fallback
}

// --- Session Track CRUD ---

std::string Graph::create_track(std::string name) {
    SessionTrackDef track;
    track.id = gen_session_id("tr");
    track.name = std::move(name);
    session_.tracks.push_back(std::move(track));
    return session_.tracks.back().id;
}

bool Graph::rename_track(const std::string& track_id, std::string new_name) {
    auto* t = find_track(track_id);
    if (!t) return false;
    t->name = std::move(new_name);
    return true;
}

bool Graph::remove_track(const std::string& track_id) {
    auto it = std::find_if(session_.tracks.begin(), session_.tracks.end(),
        [&](const SessionTrackDef& t) { return t.id == track_id; });
    if (it == session_.tracks.end()) return false;
    session_.tracks.erase(it);
    // Remove this track from all scene assignments
    for (auto& scene : session_.scenes) {
        scene.assignments.erase(track_id);
        scene.leave_unchanged.erase(track_id);
    }
    return true;
}

bool Graph::move_track(const std::string& track_id, int to_index) {
    int n = static_cast<int>(session_.tracks.size());
    auto it = std::find_if(session_.tracks.begin(), session_.tracks.end(),
        [&](const SessionTrackDef& t) { return t.id == track_id; });
    if (it == session_.tracks.end()) return false;
    int from = static_cast<int>(it - session_.tracks.begin());
    if (to_index < 0 || to_index >= n || from == to_index) return to_index == from;
    SessionTrackDef tmp = std::move(*it);
    session_.tracks.erase(it);
    session_.tracks.insert(session_.tracks.begin() + to_index, std::move(tmp));
    return true;
}

bool Graph::assign_nodes_to_track(const std::string& track_id, std::vector<std::string> node_ids) {
    auto* t = find_track(track_id);
    if (!t) return false;
    // Remove each node from any other track first
    for (const auto& nid : node_ids) {
        for (auto& other : session_.tracks) {
            if (other.id == track_id) continue;
            auto oit = std::find(other.owned_node_ids.begin(), other.owned_node_ids.end(), nid);
            if (oit != other.owned_node_ids.end())
                other.owned_node_ids.erase(oit);
        }
        // Add if not already owned
        if (std::find(t->owned_node_ids.begin(), t->owned_node_ids.end(), nid) == t->owned_node_ids.end())
            t->owned_node_ids.push_back(nid);
    }
    return true;
}

bool Graph::unassign_nodes_from_track(const std::string& track_id, const std::vector<std::string>& node_ids) {
    auto* t = find_track(track_id);
    if (!t) return false;
    for (const auto& nid : node_ids) {
        auto it = std::find(t->owned_node_ids.begin(), t->owned_node_ids.end(), nid);
        if (it != t->owned_node_ids.end())
            t->owned_node_ids.erase(it);
    }
    return true;
}

// --- Session Clip CRUD ---

std::string Graph::save_clip(const std::string& track_id, std::string name,
    std::unordered_map<std::string, std::unordered_map<std::string, float>> params,
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> string_params) {
    auto* t = find_track(track_id);
    if (!t) return {};
    SessionClipDef clip;
    clip.id = gen_session_id("c");
    clip.name = std::move(name);
    clip.params = std::move(params);
    clip.string_params = std::move(string_params);
    t->clips.push_back(std::move(clip));
    return t->clips.back().id;
}

bool Graph::update_clip(const std::string& track_id, const std::string& clip_id,
    std::unordered_map<std::string, std::unordered_map<std::string, float>> params,
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> string_params) {
    auto* c = find_clip(track_id, clip_id);
    if (!c) return false;
    c->params = std::move(params);
    c->string_params = std::move(string_params);
    return true;
}

bool Graph::rename_clip(const std::string& track_id, const std::string& clip_id, std::string new_name) {
    auto* c = find_clip(track_id, clip_id);
    if (!c) return false;
    c->name = std::move(new_name);
    return true;
}

bool Graph::remove_clip(const std::string& track_id, const std::string& clip_id) {
    auto* t = find_track(track_id);
    if (!t) return false;
    auto it = std::find_if(t->clips.begin(), t->clips.end(),
        [&](const SessionClipDef& c) { return c.id == clip_id; });
    if (it == t->clips.end()) return false;
    t->clips.erase(it);
    // Erase this clip from all scene assignments (don't leave a dangling empty-string reference)
    for (auto& scene : session_.scenes) {
        auto it = scene.assignments.begin();
        while (it != scene.assignments.end()) {
            if (it->second == clip_id)
                it = scene.assignments.erase(it);
            else
                ++it;
        }
    }
    return true;
}

bool Graph::move_clip(const std::string& track_id, const std::string& clip_id, int to_index) {
    auto* t = find_track(track_id);
    if (!t) return false;
    int n = static_cast<int>(t->clips.size());
    auto it = std::find_if(t->clips.begin(), t->clips.end(),
        [&](const SessionClipDef& c) { return c.id == clip_id; });
    if (it == t->clips.end()) return false;
    int from = static_cast<int>(it - t->clips.begin());
    if (to_index < 0 || to_index >= n || from == to_index) return to_index == from;
    SessionClipDef tmp = std::move(*it);
    t->clips.erase(it);
    t->clips.insert(t->clips.begin() + to_index, std::move(tmp));
    return true;
}

// --- Session Scene CRUD ---

std::string Graph::save_scene(std::string name) {
    SessionSceneDef scene;
    scene.id = gen_session_id("sc");
    scene.name = std::move(name);
    session_.scenes.push_back(std::move(scene));
    return session_.scenes.back().id;
}

bool Graph::rename_scene(const std::string& scene_id, std::string new_name) {
    auto* s = find_scene(scene_id);
    if (!s) return false;
    s->name = std::move(new_name);
    return true;
}

bool Graph::remove_scene(const std::string& scene_id) {
    auto it = std::find_if(session_.scenes.begin(), session_.scenes.end(),
        [&](const SessionSceneDef& s) { return s.id == scene_id; });
    if (it == session_.scenes.end()) return false;
    session_.scenes.erase(it);
    for (auto& path : session_.cue_paths) {
        path.steps.erase(
            std::remove_if(path.steps.begin(), path.steps.end(),
                           [&](const SessionCueStepDef& step) {
                               return step.scene_id == scene_id;
                           }),
            path.steps.end());
    }
    return true;
}

bool Graph::move_scene(const std::string& scene_id, int to_index) {
    int n = static_cast<int>(session_.scenes.size());
    auto it = std::find_if(session_.scenes.begin(), session_.scenes.end(),
        [&](const SessionSceneDef& s) { return s.id == scene_id; });
    if (it == session_.scenes.end()) return false;
    int from = static_cast<int>(it - session_.scenes.begin());
    if (to_index < 0 || to_index >= n || from == to_index) return to_index == from;
    SessionSceneDef tmp = std::move(*it);
    session_.scenes.erase(it);
    session_.scenes.insert(session_.scenes.begin() + to_index, std::move(tmp));
    return true;
}

bool Graph::set_scene_assignment(const std::string& scene_id, const std::string& track_id,
                                  const std::string& clip_id) {
    if (clip_id.empty()) return false;
    auto* s = find_scene(scene_id);
    if (!s) return false;
    if (!find_track(track_id)) return false;
    if (!find_clip(track_id, clip_id)) return false;
    s->assignments[track_id] = clip_id;
    s->leave_unchanged.erase(track_id);
    return true;
}

bool Graph::set_scene_leave_unchanged(const std::string& scene_id, const std::string& track_id) {
    if (track_id.empty()) return false;
    auto* s = find_scene(scene_id);
    if (!s) return false;
    if (!find_track(track_id)) return false;
    s->assignments.erase(track_id);
    s->leave_unchanged.insert(track_id);
    return true;
}

bool Graph::clear_scene_assignment(const std::string& scene_id, const std::string& track_id) {
    auto* s = find_scene(scene_id);
    if (!s) return false;
    s->assignments.erase(track_id);
    s->leave_unchanged.erase(track_id);
    return true;
}

bool Graph::update_scene_assignments(const std::string& scene_id,
                                      const std::unordered_map<std::string, std::string>& assignments) {
    auto* s = find_scene(scene_id);
    if (!s) return false;
    s->assignments.clear();
    s->leave_unchanged.clear();
    for (const auto& [tid, cid] : assignments)
        s->assignments[tid] = cid;
    return true;
}

// --- Session Cue Path CRUD ---

std::string Graph::create_cue_path(std::string name) {
    SessionCuePathDef path;
    path.id = gen_session_id("qp");
    path.name = std::move(name);
    session_.cue_paths.push_back(std::move(path));
    return session_.cue_paths.back().id;
}

bool Graph::rename_cue_path(const std::string& path_id, std::string new_name) {
    auto* p = find_cue_path(path_id);
    if (!p) return false;
    p->name = std::move(new_name);
    return true;
}

bool Graph::remove_cue_path(const std::string& path_id) {
    auto it = std::find_if(session_.cue_paths.begin(), session_.cue_paths.end(),
        [&](const SessionCuePathDef& p) { return p.id == path_id; });
    if (it == session_.cue_paths.end()) return false;
    session_.cue_paths.erase(it);
    return true;
}

bool Graph::move_cue_path(const std::string& path_id, int to_index) {
    int n = static_cast<int>(session_.cue_paths.size());
    auto it = std::find_if(session_.cue_paths.begin(), session_.cue_paths.end(),
        [&](const SessionCuePathDef& p) { return p.id == path_id; });
    if (it == session_.cue_paths.end()) return false;
    int from = static_cast<int>(it - session_.cue_paths.begin());
    if (to_index < 0 || to_index >= n || from == to_index) return to_index == from;
    SessionCuePathDef tmp = std::move(*it);
    session_.cue_paths.erase(it);
    session_.cue_paths.insert(session_.cue_paths.begin() + to_index, std::move(tmp));
    return true;
}

std::string Graph::add_cue_step(const std::string& path_id, const std::string& scene_id, int index) {
    auto* p = find_cue_path(path_id);
    if (!p || !find_scene(scene_id)) return {};
    SessionCueStepDef step;
    step.id = gen_session_id("qs");
    step.scene_id = scene_id;
    if (index < 0 || index > static_cast<int>(p->steps.size()))
        index = static_cast<int>(p->steps.size());
    p->steps.insert(p->steps.begin() + index, std::move(step));
    return p->steps[index].id;
}

bool Graph::remove_cue_step(const std::string& path_id, const std::string& step_id) {
    auto* p = find_cue_path(path_id);
    if (!p) return false;
    auto it = std::find_if(p->steps.begin(), p->steps.end(),
        [&](const SessionCueStepDef& step) { return step.id == step_id; });
    if (it == p->steps.end()) return false;
    p->steps.erase(it);
    return true;
}

bool Graph::move_cue_step(const std::string& path_id, const std::string& step_id, int to_index) {
    auto* p = find_cue_path(path_id);
    if (!p) return false;
    int n = static_cast<int>(p->steps.size());
    auto it = std::find_if(p->steps.begin(), p->steps.end(),
        [&](const SessionCueStepDef& step) { return step.id == step_id; });
    if (it == p->steps.end()) return false;
    int from = static_cast<int>(it - p->steps.begin());
    if (to_index < 0 || to_index >= n || from == to_index) return to_index == from;
    SessionCueStepDef tmp = std::move(*it);
    p->steps.erase(it);
    p->steps.insert(p->steps.begin() + to_index, std::move(tmp));
    return true;
}

bool Graph::set_cue_step_advance(const std::string& path_id, const std::string& step_id,
                                 std::string advance_mode, int bars) {
    if (advance_mode != "manual" && advance_mode != "after_bars" &&
        advance_mode != "on_scene_launch")
        return false;
    auto* step = find_cue_step(path_id, step_id);
    if (!step) return false;
    step->advance_mode = std::move(advance_mode);
    step->bars = std::max(0, bars);
    if (step->advance_mode == "after_bars" && step->bars <= 0)
        step->bars = 1;
    return true;
}

// --- Session finder helpers ---

const SessionTrackDef* Graph::find_track(const std::string& id) const {
    for (const auto& t : session_.tracks) if (t.id == id) return &t;
    return nullptr;
}
SessionTrackDef* Graph::find_track(const std::string& id) {
    for (auto& t : session_.tracks) if (t.id == id) return &t;
    return nullptr;
}
const SessionClipDef* Graph::find_clip(const std::string& track_id, const std::string& clip_id) const {
    const auto* t = find_track(track_id);
    if (!t) return nullptr;
    for (const auto& c : t->clips) if (c.id == clip_id) return &c;
    return nullptr;
}
SessionClipDef* Graph::find_clip(const std::string& track_id, const std::string& clip_id) {
    auto* t = find_track(track_id);
    if (!t) return nullptr;
    for (auto& c : t->clips) if (c.id == clip_id) return &c;
    return nullptr;
}
const SessionSceneDef* Graph::find_scene(const std::string& id) const {
    for (const auto& s : session_.scenes) if (s.id == id) return &s;
    return nullptr;
}
SessionSceneDef* Graph::find_scene(const std::string& id) {
    for (auto& s : session_.scenes) if (s.id == id) return &s;
    return nullptr;
}

const SessionCuePathDef* Graph::find_cue_path(const std::string& id) const {
    for (const auto& p : session_.cue_paths) if (p.id == id) return &p;
    return nullptr;
}

SessionCuePathDef* Graph::find_cue_path(const std::string& id) {
    for (auto& p : session_.cue_paths) if (p.id == id) return &p;
    return nullptr;
}

const SessionCueStepDef* Graph::find_cue_step(const std::string& path_id,
                                              const std::string& step_id) const {
    const auto* p = find_cue_path(path_id);
    if (!p) return nullptr;
    for (const auto& step : p->steps) if (step.id == step_id) return &step;
    return nullptr;
}

SessionCueStepDef* Graph::find_cue_step(const std::string& path_id,
                                        const std::string& step_id) {
    auto* p = find_cue_path(path_id);
    if (!p) return nullptr;
    for (auto& step : p->steps) if (step.id == step_id) return &step;
    return nullptr;
}

// --- Per-Operator Preset CRUD ---

void Graph::save_preset(const std::string& node_id, const OperatorPreset& preset) {
    auto& presets = node_presets_[node_id];
    for (auto& p : presets) {
        if (p.name == preset.name) {
            p = preset;
            return;
        }
    }
    presets.push_back(preset);
}

bool Graph::remove_preset(const std::string& node_id, const std::string& name) {
    auto it = node_presets_.find(node_id);
    if (it == node_presets_.end()) return false;
    auto& presets = it->second;
    auto pit = std::find_if(presets.begin(), presets.end(),
        [&](const OperatorPreset& p) { return p.name == name; });
    if (pit == presets.end()) return false;
    presets.erase(pit);
    if (presets.empty()) node_presets_.erase(it);
    return true;
}

bool Graph::rename_preset(const std::string& node_id, const std::string& old_name,
                           const std::string& new_name) {
    auto* p = find_preset(node_id, old_name);
    if (!p) return false;
    if (find_preset(node_id, new_name)) return false;  // name conflict
    p->name = new_name;
    return true;
}

const OperatorPreset* Graph::find_preset(const std::string& node_id, const std::string& name) const {
    auto it = node_presets_.find(node_id);
    if (it == node_presets_.end()) return nullptr;
    for (const auto& p : it->second) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

OperatorPreset* Graph::find_preset(const std::string& node_id, const std::string& name) {
    auto it = node_presets_.find(node_id);
    if (it == node_presets_.end()) return nullptr;
    for (auto& p : it->second) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<std::string> Graph::list_presets(const std::string& node_id) const {
    std::vector<std::string> names;
    auto it = node_presets_.find(node_id);
    if (it != node_presets_.end()) {
        for (const auto& p : it->second) names.push_back(p.name);
    }
    return names;
}

// --- State-Preset Mapping CRUD ---

void Graph::set_state_preset(const std::string& sm_node, int state_idx,
                              const std::string& target_node, const std::string& preset_name) {
    // Find or create mapping for this state machine
    StatePresetMapping* spm = nullptr;
    for (auto& m : state_preset_mappings_) {
        if (m.state_machine_node == sm_node) { spm = &m; break; }
    }
    if (!spm) {
        state_preset_mappings_.push_back({sm_node, {}});
        spm = &state_preset_mappings_.back();
    }
    // Grow state_presets vector if needed
    if (state_idx >= static_cast<int>(spm->state_presets.size())) {
        spm->state_presets.resize(state_idx + 1);
    }
    spm->state_presets[state_idx][target_node] = preset_name;
}

bool Graph::remove_state_preset(const std::string& sm_node, int state_idx,
                                 const std::string& target_node) {
    for (auto& m : state_preset_mappings_) {
        if (m.state_machine_node != sm_node) continue;
        if (state_idx >= static_cast<int>(m.state_presets.size())) return false;
        auto it = m.state_presets[state_idx].find(target_node);
        if (it == m.state_presets[state_idx].end()) return false;
        m.state_presets[state_idx].erase(it);
        return true;
    }
    return false;
}

void Graph::clear_state_presets(const std::string& sm_node) {
    state_preset_mappings_.erase(
        std::remove_if(state_preset_mappings_.begin(), state_preset_mappings_.end(),
            [&](const StatePresetMapping& m) { return m.state_machine_node == sm_node; }),
        state_preset_mappings_.end());
}

void Graph::ensure_state_mapping(const std::string& sm_node) {
    for (const auto& m : state_preset_mappings_)
        if (m.state_machine_node == sm_node) return;
    state_preset_mappings_.push_back({sm_node, {}});
}

const StatePresetMapping* Graph::find_state_mapping(const std::string& sm_node) const {
    for (const auto& m : state_preset_mappings_) {
        if (m.state_machine_node == sm_node) return &m;
    }
    return nullptr;
}

// --- Sticky Note CRUD ---

void Graph::add_sticky_note(StickyNoteDef note) {
    // Replace if already exists
    for (auto& sn : sticky_notes_) {
        if (sn.id == note.id) {
            sn = std::move(note);
            return;
        }
    }
    sticky_notes_.push_back(std::move(note));
}

bool Graph::remove_sticky_note(const std::string& id) {
    auto it = std::find_if(sticky_notes_.begin(), sticky_notes_.end(),
        [&](const StickyNoteDef& sn) { return sn.id == id; });
    if (it == sticky_notes_.end()) return false;
    sticky_notes_.erase(it);
    return true;
}

const StickyNoteDef* Graph::find_sticky_note(const std::string& id) const {
    for (const auto& sn : sticky_notes_) {
        if (sn.id == id) return &sn;
    }
    return nullptr;
}

StickyNoteDef* Graph::find_sticky_note(const std::string& id) {
    for (auto& sn : sticky_notes_) {
        if (sn.id == id) return &sn;
    }
    return nullptr;
}

// --- Modulation assignment CRUD ---

const std::vector<ModAssignmentDef>* Graph::find_mod_assignments(const std::string& node_id) const {
    auto it = mod_assignments_.find(node_id);
    return it != mod_assignments_.end() ? &it->second : nullptr;
}

bool Graph::add_mod_assignment(const std::string& node_id, ModAssignmentDef a) {
    if (a.polarity.empty()) a.polarity = "unipolar";
    if (a.curve.empty()) a.curve = "linear";
    auto& vec = mod_assignments_[node_id];
    // Reject duplicates (same source + destination)
    for (const auto& existing : vec) {
        if (existing.source == a.source && existing.destination == a.destination)
            return false;
    }
    vec.push_back(std::move(a));
    return true;
}

bool Graph::remove_mod_assignment(const std::string& node_id,
                                  const std::string& source, const std::string& destination) {
    auto it = mod_assignments_.find(node_id);
    if (it == mod_assignments_.end()) return false;
    auto& vec = it->second;
    auto ait = std::find_if(vec.begin(), vec.end(), [&](const ModAssignmentDef& a) {
        return a.source == source && a.destination == destination;
    });
    if (ait == vec.end()) return false;
    vec.erase(ait);
    if (vec.empty()) mod_assignments_.erase(it);
    return true;
}

bool Graph::update_mod_assignment(const std::string& node_id,
                                  const std::string& source, const std::string& destination,
                                  float amount, const std::string& polarity, const std::string& curve) {
    auto it = mod_assignments_.find(node_id);
    if (it == mod_assignments_.end()) return false;
    for (auto& a : it->second) {
        if (a.source == source && a.destination == destination) {
            a.amount = amount;
            a.polarity = polarity.empty() ? "unipolar" : polarity;
            a.curve = curve.empty() ? "linear" : curve;
            return true;
        }
    }
    return false;
}

// --- Serialization ---

// Produce the shortest decimal that round-trips to the same float32 value.
// Uses Dragonbox algorithm to avoid double-promotion artifacts like 0.6f → 0.6000000238418579.
static double clean_float(float f) {
    char buf[64];
    char* end = jkj::dragonbox::to_chars(f, buf);
    *end = '\0';
    return std::strtod(buf, nullptr);
}

static void serialize_node_fields(nlohmann::ordered_json& node_obj, const NodeDef& node) {
    node_obj["type"] = node.type;

    if (!node.pkg_name.empty()) {
        node_obj["pkg"] = nlohmann::ordered_json{{"name", node.pkg_name}, {"version", node.pkg_version}};
    }

    if (!node.params.empty() || !node.string_params.empty()) {
        nlohmann::ordered_json params_obj = nlohmann::ordered_json::object();
        for (const auto& [pname, pval] : node.params) {
            params_obj[pname] = clean_float(pval);
        }
        for (const auto& [pname, pval] : node.string_params) {
            params_obj[pname] = pval;
        }
        node_obj["params"] = std::move(params_obj);
    }

    if (node.has_layout()) {
        node_obj["layout"] = nlohmann::ordered_json{{"x", clean_float(node.layout_x)}, {"y", clean_float(node.layout_y)}};
    }

    if (node.tex_width > 0 && node.tex_height > 0) {
        node_obj["resolution"] = nlohmann::ordered_json::array({static_cast<int64_t>(node.tex_width), static_cast<int64_t>(node.tex_height)});
    }

    if (!node.param_lock_flags.empty()) {
        nlohmann::ordered_json locks_obj = nlohmann::ordered_json::object();
        for (const auto& [pname, flags] : node.param_lock_flags)
            locks_obj[pname] = static_cast<int64_t>(flags);
        node_obj["locks"] = std::move(locks_obj);
    }

    if (node.bypassed) {
        node_obj["bypassed"] = true;
    }

    // Subgraph module membership (only written for flattened graphs)
    if (!node.subgraph_owner.empty()) {
        node_obj["subgraph_owner"] = node.subgraph_owner;
        node_obj["subgraph_type"]  = node.subgraph_type;
    }
}

static nlohmann::ordered_json build_graph_json_doc(const Graph& graph) {
    nlohmann::ordered_json root;

    // Schema metadata
    root["schema_version"] = GRAPH_SCHEMA_VERSION;
    root["vivid_version"] = VIVID_CORE_VERSION;

    if (!graph.meta().empty()) {
        nlohmann::ordered_json meta = nlohmann::ordered_json::object();
        const auto& gm = graph.meta();
        if (!gm.id.empty()) meta["id"] = gm.id;
        if (!gm.title.empty()) meta["title"] = gm.title;
        if (!gm.description.empty()) meta["description"] = gm.description;
        if (!gm.tags.empty()) meta["tags"] = gm.tags;
        if (!gm.difficulty.empty()) meta["difficulty"] = gm.difficulty;
        if (!gm.domains.empty()) meta["domains"] = gm.domains;
        if (!gm.requires_packages.empty()) meta["requires_packages"] = gm.requires_packages;
        if (gm.featured_rank >= 0) meta["featured_rank"] = gm.featured_rank;
        if (gm.estimated_minutes >= 0) meta["estimated_minutes"] = gm.estimated_minutes;
        if (!gm.content_kind.empty()) meta["content_kind"] = gm.content_kind;
        if (!gm.category.empty()) meta["category"] = gm.category;
        if (!gm.family.empty()) meta["family"] = gm.family;
        if (!gm.role.empty()) meta["role"] = gm.role;
        if (!gm.playability.empty()) meta["playability"] = gm.playability;
        if (!gm.preview_controls.empty()) {
            nlohmann::ordered_json preview = nlohmann::ordered_json::array();
            for (const auto& ctrl : gm.preview_controls) {
                nlohmann::ordered_json item = nlohmann::ordered_json::object();
                item["node"] = ctrl.node;
                item["param"] = ctrl.param;
                if (!ctrl.label.empty()) item["label"] = ctrl.label;
                preview.push_back(std::move(item));
            }
            meta["preview_controls"] = std::move(preview);
        }
        root["meta"] = std::move(meta);
    }

    // Nodes
    nlohmann::ordered_json nodes_obj = nlohmann::ordered_json::object();
    for (const auto& node : graph.nodes()) {
        nlohmann::ordered_json node_obj = nlohmann::ordered_json::object();
        serialize_node_fields(node_obj, node);
        nodes_obj[node.id] = std::move(node_obj);
    }
    root["nodes"] = std::move(nodes_obj);

    // Connections
    nlohmann::ordered_json conns_arr = nlohmann::ordered_json::array();
    for (const auto& conn : graph.connections()) {
        nlohmann::ordered_json conn_obj = nlohmann::ordered_json::object();
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        conn_obj["from"] = from_addr;
        conn_obj["to"] = to_addr;
        if (conn.has_remap()) {
            conn_obj["from_min"] = clean_float(conn.from_min);
            conn_obj["from_max"] = clean_float(conn.from_max);
            conn_obj["to_min"]   = clean_float(conn.to_min);
            conn_obj["to_max"]   = clean_float(conn.to_max);
            if (conn.clamp)
                conn_obj["clamp"] = true;
            if (conn.curve != 0)
                conn_obj["curve"] = conn.curve;
        }
        if (conn.has_bridge())
            conn_obj["bridge"] = conn.bridge;
        conns_arr.push_back(std::move(conn_obj));
    }
    root["connections"] = std::move(conns_arr);

    // MIDI mappings
    if (!graph.midi_mappings().empty()) {
        nlohmann::ordered_json midi_arr = nlohmann::ordered_json::array();
        for (const auto& mm : graph.midi_mappings()) {
            nlohmann::ordered_json mm_obj = nlohmann::ordered_json::object();
            mm_obj["node"] = mm.node_id;
            mm_obj["param"] = mm.param_name;
            mm_obj["cc"] = mm.cc_number;
            if (mm.channel != 0)
                mm_obj["channel"] = mm.channel;
            mm_obj["range_min"] = clean_float(mm.range_min);
            mm_obj["range_max"] = clean_float(mm.range_max);
            midi_arr.push_back(std::move(mm_obj));
        }
        root["midi_mappings"] = std::move(midi_arr);
    }

    if (!graph.quantize_clock_node().empty())
        root["quantize_clock"] = graph.quantize_clock_node();
    if (graph.metronome().bpm != 120.0f || graph.metronome().beats_per_bar != 4) {
        nlohmann::ordered_json met_obj = nlohmann::ordered_json::object();
        met_obj["bpm"] = clean_float(graph.metronome().bpm);
        met_obj["beats_per_bar"] = graph.metronome().beats_per_bar;
        root["metronome"] = std::move(met_obj);
    }

    // Per-operator presets
    if (!graph.node_presets().empty()) {
        nlohmann::ordered_json presets_obj = nlohmann::ordered_json::object();
        for (const auto& [node_id, presets] : graph.node_presets()) {
            nlohmann::ordered_json pr_arr = nlohmann::ordered_json::array();
            for (const auto& p : presets) {
                nlohmann::ordered_json pr_obj = nlohmann::ordered_json::object();
                pr_obj["name"] = p.name;
                nlohmann::ordered_json pp_obj = nlohmann::ordered_json::object();
                for (const auto& [pname, pval] : p.params) {
                    pp_obj[pname] = clean_float(pval);
                }
                for (const auto& [pname, pval] : p.string_params) {
                    pp_obj[pname] = pval;
                }
                pr_obj["params"] = std::move(pp_obj);
                pr_arr.push_back(std::move(pr_obj));
            }
            presets_obj[node_id] = std::move(pr_arr);
        }
        root["presets"] = std::move(presets_obj);
    }

    // State-preset mappings
    if (!graph.state_preset_mappings().empty()) {
        nlohmann::ordered_json spm_arr = nlohmann::ordered_json::array();
        for (const auto& spm : graph.state_preset_mappings()) {
            nlohmann::ordered_json spm_obj = nlohmann::ordered_json::object();
            spm_obj["node"] = spm.state_machine_node;
            nlohmann::ordered_json states_arr = nlohmann::ordered_json::array();
            for (const auto& bindings : spm.state_presets) {
                nlohmann::ordered_json b_obj = nlohmann::ordered_json::object();
                for (const auto& [target_node, preset_name] : bindings) {
                    b_obj[target_node] = preset_name;
                }
                states_arr.push_back(std::move(b_obj));
            }
            spm_obj["states"] = std::move(states_arr);
            spm_arr.push_back(std::move(spm_obj));
        }
        root["state_preset_mappings"] = std::move(spm_arr);
    }

    // Viewport
    if (graph.has_viewport()) {
        nlohmann::ordered_json vp_obj = nlohmann::ordered_json::object();
        vp_obj["pan_x"] = clean_float(graph.viewport_pan_x);
        vp_obj["pan_y"] = clean_float(graph.viewport_pan_y);
        vp_obj["zoom"]  = clean_float(graph.viewport_zoom);
        root["viewport"] = std::move(vp_obj);
    }

    // Sticky notes
    if (!graph.sticky_notes().empty()) {
        nlohmann::ordered_json sn_arr = nlohmann::ordered_json::array();
        for (const auto& sn : graph.sticky_notes()) {
            nlohmann::ordered_json sn_obj = nlohmann::ordered_json::object();
            sn_obj["id"] = sn.id;
            sn_obj["text"] = sn.text;
            sn_obj["x"] = clean_float(sn.x);
            sn_obj["y"] = clean_float(sn.y);
            sn_obj["width"] = clean_float(sn.width);
            sn_obj["height"] = clean_float(sn.height);
            sn_obj["color"] = sn.color;
            sn_arr.push_back(std::move(sn_obj));
        }
        root["sticky_notes"] = std::move(sn_arr);
    }

    // Modulation assignments
    if (!graph.mod_assignments().empty()) {
        nlohmann::ordered_json ma_obj = nlohmann::ordered_json::object();
        for (const auto& [node_id, assignments] : graph.mod_assignments()) {
            nlohmann::ordered_json arr = nlohmann::ordered_json::array();
            for (const auto& a : assignments) {
                nlohmann::ordered_json a_obj = nlohmann::ordered_json::object();
                a_obj["source"] = a.source;
                a_obj["destination"] = a.destination;
                a_obj["amount"] = clean_float(a.amount);
                if (a.polarity != "unipolar")
                    a_obj["polarity"] = a.polarity;
                if (a.curve != "linear")
                    a_obj["curve"] = a.curve;
                arr.push_back(std::move(a_obj));
            }
            ma_obj[node_id] = std::move(arr);
        }
        root["mod_assignments"] = std::move(ma_obj);
    }

    // Session
    const auto& sess = graph.session();
    if (!sess.tracks.empty() || !sess.scenes.empty() || !sess.cue_paths.empty() ||
        !sess.active_clips.empty()) {
        nlohmann::ordered_json sess_obj = nlohmann::ordered_json::object();
        // tracks
        nlohmann::ordered_json tracks_arr = nlohmann::ordered_json::array();
        for (const auto& track : sess.tracks) {
            nlohmann::ordered_json t_obj = nlohmann::ordered_json::object();
            t_obj["id"] = track.id;
            t_obj["name"] = track.name;
            t_obj["owned_nodes"] = track.owned_node_ids;
            if (track.default_transition.fade) {
                nlohmann::ordered_json dt_obj = nlohmann::ordered_json::object();
                dt_obj["mode"] = "fade";
                dt_obj["duration_bars"] = clean_float(track.default_transition.duration_bars);
                t_obj["default_transition"] = std::move(dt_obj);
            }
            nlohmann::ordered_json clips_arr = nlohmann::ordered_json::array();
            for (const auto& clip : track.clips) {
                nlohmann::ordered_json c_obj = nlohmann::ordered_json::object();
                c_obj["id"] = clip.id;
                c_obj["name"] = clip.name;
                if (clip.transition_override) {
                    const auto& transition = *clip.transition_override;
                    nlohmann::ordered_json ct_obj = nlohmann::ordered_json::object();
                    ct_obj["mode"] = transition.fade ? "fade" : "cut";
                    if (transition.fade)
                        ct_obj["duration_bars"] = clean_float(transition.duration_bars);
                    c_obj["transition_override"] = std::move(ct_obj);
                }
                if (!clip.params.empty() || !clip.string_params.empty()) {
                    nlohmann::ordered_json p_obj = nlohmann::ordered_json::object();
                    for (const auto& [nid, pm] : clip.params) {
                        nlohmann::ordered_json n_obj = nlohmann::ordered_json::object();
                        for (const auto& [pn, pv] : pm)
                            n_obj[pn] = clean_float(pv);
                        auto sit = clip.string_params.find(nid);
                        if (sit != clip.string_params.end())
                            for (const auto& [pn, pv] : sit->second)
                                n_obj[pn] = pv;
                        p_obj[nid] = std::move(n_obj);
                    }
                    for (const auto& [nid, spm] : clip.string_params) {
                        if (clip.params.count(nid)) continue;
                        nlohmann::ordered_json n_obj = nlohmann::ordered_json::object();
                        for (const auto& [pn, pv] : spm)
                            n_obj[pn] = pv;
                        p_obj[nid] = std::move(n_obj);
                    }
                    c_obj["params"] = std::move(p_obj);
                }
                clips_arr.push_back(std::move(c_obj));
            }
            t_obj["clips"] = std::move(clips_arr);
            tracks_arr.push_back(std::move(t_obj));
        }
        sess_obj["tracks"] = std::move(tracks_arr);
        // scenes
        nlohmann::ordered_json scenes_arr = nlohmann::ordered_json::array();
        for (const auto& scene : sess.scenes) {
            nlohmann::ordered_json s_obj = nlohmann::ordered_json::object();
            s_obj["id"] = scene.id;
            s_obj["name"] = scene.name;
            nlohmann::ordered_json asn_obj = nlohmann::ordered_json::object();
            for (const auto& [tid, cid] : scene.assignments)
                asn_obj[tid] = cid;
            for (const auto& tid : scene.leave_unchanged)
                asn_obj[tid] = nullptr;
            s_obj["assignments"] = std::move(asn_obj);
            scenes_arr.push_back(std::move(s_obj));
        }
        sess_obj["scenes"] = std::move(scenes_arr);
        // cue paths
        nlohmann::ordered_json cue_paths_arr = nlohmann::ordered_json::array();
        for (const auto& path : sess.cue_paths) {
            nlohmann::ordered_json p_obj = nlohmann::ordered_json::object();
            p_obj["id"] = path.id;
            p_obj["name"] = path.name;
            nlohmann::ordered_json steps_arr = nlohmann::ordered_json::array();
            for (const auto& step : path.steps) {
                nlohmann::ordered_json st_obj = nlohmann::ordered_json::object();
                st_obj["id"] = step.id;
                st_obj["scene_id"] = step.scene_id;
                if (step.advance_mode != "manual")
                    st_obj["advance_mode"] = step.advance_mode;
                if (step.advance_mode == "after_bars")
                    st_obj["bars"] = step.bars;
                steps_arr.push_back(std::move(st_obj));
            }
            p_obj["steps"] = std::move(steps_arr);
            cue_paths_arr.push_back(std::move(p_obj));
        }
        if (!cue_paths_arr.empty())
            sess_obj["cue_paths"] = std::move(cue_paths_arr);
        if (!sess.active_clips.empty()) {
            nlohmann::ordered_json ac_obj = nlohmann::ordered_json::object();
            for (const auto& [tid, cid] : sess.active_clips)
                ac_obj[tid] = cid;
            sess_obj["active_clips"] = std::move(ac_obj);
        }
        root["session"] = std::move(sess_obj);
    }

    return root;
}

bool Graph::save(const char* path) const {
    auto doc = build_graph_json_doc(*this);
    std::ofstream ofs(path);
    if (!ofs) {
        std::fprintf(stderr, "[vivid] Graph: failed to write %s: could not open file\n", path);
        return false;
    }
    ofs << doc.dump(4) << '\n';
    std::fprintf(stderr, "[vivid] Graph saved: %s (%zu nodes, %zu connections)\n",
        path, nodes_.size(), connections_.size());
    return true;
}

bool Graph::save_to_string(std::string& out_json) const {
    auto doc = build_graph_json_doc(*this);
    out_json = doc.dump(4) + "\n";
    return true;
}

} // namespace vivid
