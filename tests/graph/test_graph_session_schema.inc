    // =====================================================================
    // Test S1: Session schema — empty session round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S1: Session empty round-trip ===\n");
        vivid::Graph g;
        g.add_node("osc", "Osc");

        std::string path = (g_tmp->path / "session_empty.json").string();
        check(g.save(path.c_str()), "save with empty session succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "reload succeeds");
        check(g2.session().tracks.empty(), "no tracks after reload");
        check(g2.session().scenes.empty(), "no scenes after reload");
        std::remove(path.c_str());
    }

    // =====================================================================
    // Test S2: Track CRUD — create, rename, remove, move
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S2: Track CRUD ===\n");
        vivid::Graph g;
        g.add_node("osc", "Osc");
        g.add_node("filt", "Filter");

        // create_track
        std::string t1 = g.create_track("Bass");
        check(!t1.empty(), "create_track returns non-empty id");
        std::string t2 = g.create_track("Lead");
        check(!t2.empty(), "second track id non-empty");
        check(t1 != t2, "track ids are distinct");
        check(g.session().tracks.size() == 2, "2 tracks after creation");
        check(g.session().tracks[0].name == "Bass", "first track name = Bass");
        check(g.session().tracks[1].name == "Lead", "second track name = Lead");

        // find_track
        const vivid::Graph& cg = g;
        check(cg.find_track(t1) != nullptr, "find_track const finds t1");
        check(cg.find_track("nope") == nullptr, "find_track const returns null for unknown");
        check(g.find_track(t2) != nullptr, "find_track mutable finds t2");

        // rename_track
        check(g.rename_track(t1, "Drums"), "rename_track succeeds");
        check(g.find_track(t1)->name == "Drums", "track renamed to Drums");

        // assign_nodes_to_track
        check(g.assign_nodes_to_track(t1, {"osc", "filt"}), "assign nodes to track");
        check(g.find_track(t1)->owned_node_ids.size() == 2, "track owns 2 nodes");

        // unassign_nodes_from_track
        check(g.unassign_nodes_from_track(t1, {"filt"}), "unassign filt from track");
        check(g.find_track(t1)->owned_node_ids.size() == 1, "track owns 1 node after unassign");

        // move_track
        check(g.move_track(t1, 1), "move_track t1 to index 1");
        check(g.session().tracks[1].id == t1, "t1 now at index 1");
        check(g.session().tracks[0].id == t2, "t2 now at index 0");

        // remove_track
        check(g.remove_track(t1), "remove_track t1 succeeds");
        check(g.session().tracks.size() == 1, "1 track after removal");
        check(g.find_track(t1) == nullptr, "removed track not found");
    }

    // =====================================================================
    // Test S3: Clip CRUD — save, update, rename, remove, move
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S3: Clip CRUD ===\n");
        vivid::Graph g;
        g.add_node("osc", "Osc", {{"freq", 440.0f}});
        std::string tid = g.create_track("Track A");

        // save_clip
        vivid::SessionClipDef clip;
        clip.name = "Intro";
        clip.params["osc"] = {{"freq", 880.0f}};
        std::string cid = g.save_clip(tid, clip.name, clip.params, clip.string_params);
        check(!cid.empty(), "save_clip returns non-empty id");
        check(g.find_track(tid)->clips.size() == 1, "1 clip after save");

        // save second clip
        vivid::SessionClipDef clip2;
        clip2.name = "Drop";
        clip2.params["osc"] = {{"freq", 220.0f}};
        std::string cid2 = g.save_clip(tid, clip2.name, clip2.params, clip2.string_params);
        check(cid != cid2, "clip ids are distinct");
        check(g.find_track(tid)->clips.size() == 2, "2 clips after second save");

        // find_clip
        const vivid::Graph& cg = g;
        check(cg.find_clip(tid, cid) != nullptr, "find_clip const finds clip");
        check(cg.find_clip(tid, "nope") == nullptr, "find_clip const returns null for unknown");
        check(g.find_clip(tid, cid2) != nullptr, "find_clip mutable finds clip2");

        // rename_clip
        check(g.rename_clip(tid, cid, "Verse"), "rename_clip succeeds");
        check(g.find_clip(tid, cid)->name == "Verse", "clip renamed to Verse");

        // update_clip params
        std::unordered_map<std::string, std::unordered_map<std::string, float>> new_params;
        new_params["osc"] = {{"freq", 660.0f}};
        check(g.update_clip(tid, cid, new_params, {}), "update_clip succeeds");
        check(g.find_clip(tid, cid)->params.at("osc").at("freq") == 660.0f, "clip param updated");

        // move_clip
        check(g.move_clip(tid, cid, 1), "move_clip to index 1");
        check(g.find_track(tid)->clips[1].id == cid, "clip at index 1 after move");

        // remove_clip
        check(g.remove_clip(tid, cid), "remove_clip succeeds");
        check(g.find_track(tid)->clips.size() == 1, "1 clip after remove");
        check(g.find_clip(tid, cid) == nullptr, "removed clip not found");
    }

    // =====================================================================
    // Test S4: Scene CRUD — save, rename, assignments, leave_unchanged, remove
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S4: Scene CRUD ===\n");
        vivid::Graph g;
        std::string t1 = g.create_track("Track A");
        std::string t2 = g.create_track("Track B");

        // Save two clips for track t1
        std::string c1 = g.save_clip(t1, "Clip 1", {}, {});
        std::string c2 = g.save_clip(t1, "Clip 2", {}, {});
        std::string c3 = g.save_clip(t2, "Clip 3", {}, {});

        // save_scene
        std::string sid = g.save_scene("Scene A");
        check(!sid.empty(), "save_scene returns non-empty id");
        check(g.session().scenes.size() == 1, "1 scene after save");
        check(g.session().scenes[0].name == "Scene A", "scene name = Scene A");

        std::string sid2 = g.save_scene("Scene B");
        check(sid != sid2, "scene ids are distinct");
        check(g.session().scenes.size() == 2, "2 scenes after second save");

        // find_scene
        const vivid::Graph& cg = g;
        check(cg.find_scene(sid) != nullptr, "find_scene const finds scene");
        check(cg.find_scene("nope") == nullptr, "find_scene const returns null for unknown");

        // set_scene_assignment
        check(g.set_scene_assignment(sid, t1, c1), "set_scene_assignment track->clip");
        check(g.set_scene_assignment(sid, t2, c3), "set_scene_assignment second track->clip");
        const auto* sc = g.find_scene(sid);
        check(sc->assignments.count(t1) == 1, "scene has assignment for t1");
        check(sc->assignments.at(t1) == c1, "t1 assigned to c1");
        check(sc->assignments.at(t2) == c3, "t2 assigned to c3");

        // set_scene_leave_unchanged
        check(g.set_scene_leave_unchanged(sid, t1), "set leave_unchanged");
        sc = g.find_scene(sid);
        check(sc->leave_unchanged.count(t1) == 1, "t1 in leave_unchanged");
        check(g.clear_scene_assignment(sid, t1), "clear removes leave_unchanged");
        sc = g.find_scene(sid);
        check(sc->leave_unchanged.count(t1) == 0, "t1 removed from leave_unchanged");

        // clear_scene_assignment
        check(g.clear_scene_assignment(sid, t1), "clear_scene_assignment succeeds");
        sc = g.find_scene(sid);
        check(sc->assignments.count(t1) == 0, "t1 assignment cleared");
        check(sc->assignments.count(t2) == 1, "t2 assignment still present");

        // rename_scene
        check(g.rename_scene(sid, "Intro Scene"), "rename_scene succeeds");
        check(g.find_scene(sid)->name == "Intro Scene", "scene renamed");

        // move_scene
        check(g.move_scene(sid, 1), "move_scene to index 1");
        check(g.session().scenes[1].id == sid, "scene at index 1 after move");

        // remove_scene
        check(g.remove_scene(sid), "remove_scene succeeds");
        check(g.session().scenes.size() == 1, "1 scene after remove");
        check(g.find_scene(sid) == nullptr, "removed scene not found");
    }

    // =====================================================================
    // Test S5: Session JSON round-trip (full schema)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S5: Session JSON round-trip ===\n");
        vivid::Graph g1;
        g1.add_node("osc", "Osc", {{"freq", 440.0f}});
        g1.add_node("filt", "Filter", {{"cutoff", 1000.0f}});

        std::string tid = g1.create_track("Bass");
        g1.assign_nodes_to_track(tid, {"osc", "filt"});

        std::unordered_map<std::string, std::unordered_map<std::string, float>> p1, p2;
        p1["osc"] = {{"freq", 880.0f}};
        p2["filt"] = {{"cutoff", 5000.0f}};
        std::string cid1 = g1.save_clip(tid, "Bright", p1, {});
        std::string cid2 = g1.save_clip(tid, "Dark", p2, {});

        std::string sid = g1.save_scene("Main Scene");
        g1.set_scene_assignment(sid, tid, cid1);

        g1.set_quantize_clock_node("clk1");
        vivid::GraphMetronomeDef metro;
        metro.bpm = 120.0f;
        metro.beats_per_bar = 4;
        g1.set_metronome(metro);

        std::string path = (g_tmp->path / "session_roundtrip.json").string();
        check(g1.save(path.c_str()), "save with session data succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "reload with session data succeeds");

        // Tracks
        check(g2.session().tracks.size() == 1, "1 track after round-trip");
        const auto* t = g2.find_track(tid);
        check(t != nullptr, "track found by stable id");
        if (t) {
            check(t->name == "Bass", "track name = Bass");
            check(t->owned_node_ids.size() == 2, "track owns 2 nodes");
        }

        // Clips
        const auto* c1 = g2.find_clip(tid, cid1);
        check(c1 != nullptr, "Bright clip found by stable id");
        if (c1) {
            check(c1->name == "Bright", "clip name = Bright");
            check(c1->params.count("osc") == 1, "Bright has osc params");
            check(c1->params.at("osc").at("freq") == 880.0f, "Bright osc freq = 880");
        }

        const auto* c2 = g2.find_clip(tid, cid2);
        check(c2 != nullptr, "Dark clip found");
        if (c2) {
            check(c2->name == "Dark", "clip name = Dark");
        }

        // Scenes
        check(g2.session().scenes.size() == 1, "1 scene after round-trip");
        const auto* sc = g2.find_scene(sid);
        check(sc != nullptr, "scene found by stable id");
        if (sc) {
            check(sc->name == "Main Scene", "scene name preserved");
            check(sc->assignments.count(tid) == 1, "scene assignment preserved");
            check(sc->assignments.at(tid) == cid1, "scene assignment value preserved");
        }

        // Metronome / quantize clock
        check(g2.quantize_clock_node() == "clk1", "quantize_clock_node preserved");
        check_float(g2.metronome().bpm, 120.0f, "metronome bpm preserved");
        check(g2.metronome().beats_per_bar == 4, "metronome beats_per_bar preserved");

        std::remove(path.c_str());
    }

    // =====================================================================
    // Test S6: Legacy "variations" key in JSON — silent ignore on load
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S6: Legacy variations key silently ignored ===\n");
        std::string path = write_temp("legacy_variations", R"({
            "nodes": {
                "osc": { "type": "Osc", "params": { "freq": 440.0 } }
            },
            "connections": [],
            "variations": [
                { "name": "Old", "params": { "osc": { "freq": 880.0 } } }
            ],
            "active_variation": 0
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load with legacy variations key succeeds");
        check(g.nodes().size() == 1, "node loaded correctly");
        check(g.session().tracks.empty(), "no tracks — legacy variations ignored");
        check(g.session().scenes.empty(), "no scenes — legacy variations ignored");
        std::remove(path.c_str());
    }

    // =====================================================================
    // Test S7: remove_node cleans up session track ownership
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S7: remove_node cleans up track ownership ===\n");
        vivid::Graph g;
        g.add_node("osc", "Osc");
        g.add_node("filt", "Filter");

        std::string tid = g.create_track("Track");
        g.assign_nodes_to_track(tid, {"osc", "filt"});
        check(g.find_track(tid)->owned_node_ids.size() == 2, "2 nodes owned before remove");

        g.remove_node("osc");
        check(g.find_track(tid)->owned_node_ids.size() == 1, "1 node owned after remove_node");
        const auto& owned = g.find_track(tid)->owned_node_ids;
        check(std::find(owned.begin(), owned.end(), "osc") == owned.end(), "osc removed from track ownership");
        check(std::find(owned.begin(), owned.end(), "filt") != owned.end(), "filt still in track ownership");
    }

    // =====================================================================
    // Test S8: Session stable IDs are unique per prefix
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S8: Stable IDs are unique ===\n");
        vivid::Graph g;
        std::unordered_set<std::string> ids;
        for (int i = 0; i < 20; ++i) {
            std::string tid = g.create_track("T" + std::to_string(i));
            check(!tid.empty(), "track id non-empty");
            check(ids.count(tid) == 0, "track id is unique");
            ids.insert(tid);
        }
        check(g.session().tracks.size() == 20, "20 tracks created with unique ids");
    }

    // =====================================================================
    // Test S9: save_clip / save_scene always create new (no name dedup)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S9: save_clip/save_scene always create new ===\n");
        vivid::Graph g;
        std::string tid = g.create_track("T");

        std::string c1 = g.save_clip(tid, "Clip A", {}, {});
        std::string c2 = g.save_clip(tid, "Clip A", {}, {});  // same name → new id
        check(c1 != c2, "same-name clips get distinct IDs");
        check(g.find_track(tid)->clips.size() == 2, "two clips with same name coexist");

        std::string s1 = g.save_scene("Scene A");
        std::string s2 = g.save_scene("Scene A");  // same name → new id
        check(s1 != s2, "same-name scenes get distinct IDs");
        check(g.session().scenes.size() == 2, "two scenes with same name coexist");
    }

    // =====================================================================
    // Test S10: transition_override supports inherit, explicit cut, and fade
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S10: transition_override inherit/cut/fade ===\n");
        vivid::Graph g;
        std::string tid = g.create_track("T");
        g.find_track(tid)->default_transition.fade = true;
        g.find_track(tid)->default_transition.duration_bars = 4.0f;
        std::string inherit_cid = g.save_clip(tid, "Inherit Clip", {}, {});

        std::string path = (g_tmp->path / "transition_inherit.json").string();
        check(g.save(path.c_str()), "save succeeds");

        std::ifstream f(path);
        std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(json.find("\"default_transition\"") != std::string::npos,
              "fade track default transition is written");
        check(json.find("\"transition_override\"") == std::string::npos,
              "missing clip override is omitted from JSON");

        vivid::Graph g_reload;
        check(g_reload.load(path.c_str()), "reload inherit transition graph");
        const auto* inherit_clip = g_reload.find_clip(tid, inherit_cid);
        check(inherit_clip != nullptr, "inherit clip found after reload");
        if (inherit_clip)
            check(!inherit_clip->transition_override.has_value(),
                  "missing transition_override reloads as inherit/nullopt");
        std::remove(path.c_str());

        // Explicit cut override is distinct from inheriting the track transition.
        vivid::Graph g_cut;
        std::string cut_tid = g_cut.create_track("T");
        g_cut.find_track(cut_tid)->default_transition.fade = true;
        g_cut.find_track(cut_tid)->default_transition.duration_bars = 4.0f;
        std::string cut_cid = g_cut.save_clip(cut_tid, "Cut Clip", {}, {});
        g_cut.find_clip(cut_tid, cut_cid)->transition_override = vivid::SessionTransitionDef{};

        std::string cut_path = (g_tmp->path / "transition_explicit_cut.json").string();
        check(g_cut.save(cut_path.c_str()), "save with explicit cut transition succeeds");
        std::ifstream cut_f(cut_path);
        std::string cut_json((std::istreambuf_iterator<char>(cut_f)), std::istreambuf_iterator<char>());
        check(cut_json.find("\"transition_override\"") != std::string::npos,
              "explicit cut transition_override is written");
        check(cut_json.find("\"cut\"") != std::string::npos, "mode=cut in JSON");

        vivid::Graph g_cut_reload;
        check(g_cut_reload.load(cut_path.c_str()), "reload explicit cut transition graph");
        const auto* cut_clip = g_cut_reload.find_clip(cut_tid, cut_cid);
        check(cut_clip != nullptr, "explicit cut clip found after reload");
        if (cut_clip) {
            check(cut_clip->transition_override.has_value(),
                  "explicit cut reloads with transition_override value");
            check(!cut_clip->transition_override->fade, "explicit cut reloads fade=false");
        }
        std::remove(cut_path.c_str());

        // Fade override is written with duration.
        vivid::Graph g2;
        std::string tid2 = g2.create_track("T");
        vivid::SessionTransitionDef fade_override;
        fade_override.fade = true;
        fade_override.duration_bars = 2.0f;
        std::string cid2 = g2.save_clip(tid2, "Fade Clip", {}, {});
        g2.find_clip(tid2, cid2)->transition_override = fade_override;

        std::string path2 = (g_tmp->path / "transition_fade.json").string();
        check(g2.save(path2.c_str()), "save with fade transition succeeds");
        std::ifstream f2(path2);
        std::string json2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
        check(json2.find("\"transition_override\"") != std::string::npos,
              "transition_override present for fade clip");
        check(json2.find("\"fade\"") != std::string::npos, "mode=fade in JSON");
        check(json2.find("\"duration_bars\"") != std::string::npos, "fade duration in JSON");

        vivid::Graph g2_reload;
        check(g2_reload.load(path2.c_str()), "reload fade transition graph");
        const auto* fade_clip = g2_reload.find_clip(tid2, cid2);
        check(fade_clip != nullptr, "fade clip found after reload");
        if (fade_clip) {
            check(fade_clip->transition_override.has_value(),
                  "fade reloads with transition_override value");
            check(fade_clip->transition_override->fade, "fade reloads fade=true");
            check_float(fade_clip->transition_override->duration_bars, 2.0f,
                        "fade reloads duration_bars");
        }
        std::remove(path2.c_str());
    }

    // =====================================================================
    // Test S11: set_scene_assignment validates track and clip existence
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S11: set_scene_assignment validation ===\n");
        vivid::Graph g;
        std::string t1 = g.create_track("T1");
        std::string t2 = g.create_track("T2");
        std::string c1 = g.save_clip(t1, "C1", {}, {});
        std::string sid = g.save_scene("S");

        // Valid assignment
        check(g.set_scene_assignment(sid, t1, c1), "valid assignment succeeds");

        // Empty clip_id rejected
        check(!g.set_scene_assignment(sid, t1, ""), "empty clip_id rejected");

        // Unknown track rejected
        check(!g.set_scene_assignment(sid, "nope", c1), "unknown track rejected");

        // Clip from wrong track rejected
        check(!g.set_scene_assignment(sid, t2, c1), "clip from wrong track rejected");

        // Unknown scene rejected
        check(!g.set_scene_assignment("no_scene", t1, c1), "unknown scene rejected");

        // leave_unchanged validates scene and track too
        check(g.set_scene_leave_unchanged(sid, t2), "leave_unchanged valid known track succeeds");
        check(!g.set_scene_leave_unchanged(sid, ""), "leave_unchanged empty track rejected");
        check(!g.set_scene_leave_unchanged(sid, "nope"), "leave_unchanged unknown track rejected");
        check(!g.set_scene_leave_unchanged("no_scene", t1), "leave_unchanged unknown scene rejected");
    }

    // =====================================================================
    // Test S12: remove_node purges stale entries from clip ParamSets
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S12: remove_node cleans up clip params ===\n");
        vivid::Graph g;
        g.add_node("osc", "Osc", {{"freq", 440.0f}});
        g.add_node("filt", "Filter");
        std::string tid = g.create_track("T");
        g.assign_nodes_to_track(tid, {"osc", "filt"});

        std::unordered_map<std::string, std::unordered_map<std::string, float>> p;
        p["osc"] = {{"freq", 880.0f}};
        p["filt"] = {{"cutoff", 500.0f}};
        std::string cid = g.save_clip(tid, "Clip", p, {});

        check(g.find_clip(tid, cid)->params.count("osc") == 1, "osc in clip before remove");
        check(g.find_clip(tid, cid)->params.count("filt") == 1, "filt in clip before remove");

        g.remove_node("osc");

        check(g.find_clip(tid, cid)->params.count("osc") == 0, "osc purged from clip after remove");
        check(g.find_clip(tid, cid)->params.count("filt") == 1, "filt remains in clip after remove");
    }

    // =====================================================================
    // Test S13: load-time scene assignments validate track and clip references
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test S13: load-time scene assignment validation ===\n");
        std::string path = write_temp("session_invalid_scene_assignments", R"({
            "nodes": {},
            "connections": [],
            "session": {
                "tracks": [
                    {
                        "id": "t1",
                        "name": "Track 1",
                        "owned_nodes": [],
                        "clips": [
                            { "id": "c1", "name": "Clip 1" }
                        ]
                    },
                    {
                        "id": "t2",
                        "name": "Track 2",
                        "owned_nodes": [],
                        "clips": [
                            { "id": "c2", "name": "Clip 2" }
                        ]
                    }
                ],
                "scenes": [
                    {
                        "id": "s1",
                        "name": "Scene 1",
                        "assignments": {
                            "t1": "c2",
                            "t2": null,
                            "missing_track": null,
                            "also_missing": "c1",
                            "t3": ""
                        }
                    }
                ]
            }
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load graph with invalid scene references succeeds");
        const auto* scene = g.find_scene("s1");
        check(scene != nullptr, "scene loaded");
        if (scene) {
            check(scene->assignments.empty(), "invalid clip assignments dropped on load");
            check(scene->leave_unchanged.count("t2") == 1, "valid leave_unchanged retained");
            check(scene->leave_unchanged.count("missing_track") == 0,
                  "unknown leave_unchanged track dropped");
            check(scene->leave_unchanged.size() == 1,
                  "only known leave_unchanged track retained");
        }
        std::remove(path.c_str());
    }
