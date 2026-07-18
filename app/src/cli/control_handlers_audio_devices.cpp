#include "cli/control_handlers_audio_domains.h"
#include "cli/audio_analysis_tools.h"
#include "cli/control_handlers_internal.h"

#include "audio/plugin_catalog.h"
#include "audio/vst3_host.h"

#include <string>

namespace vivid {

void register_audio_device_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    handlers_["add_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = b.value("name", std::string());
        // A ".vst3" path (from list_plugins) loads that bundle directly as an effect.
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".vst3") == 0) {
            const bool okk = P::session_add_effect(c.session, track, name.c_str());
            return okk ? ok() : err(code::kInternal, "add failed (not an effect, or load error)");
        }
        // Any installed plugin, by name (the five hard-coded labels are gone).
        const bool okk = P::session_add_effect_by_name(c.session, track, name.c_str());
        return okk ? ok() : err(code::kNotFound, "no installed plugin named '" + name + "'");
    };
    // Every installed plugin (VST3 today), for the browser. A path from here works as
    // an "instrument" for add_track or a "name" for add_effect. CLAP/AU hosts TBD.
    handlers_["list_plugins"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            // `class` is what the background probe found (instrument / effect / note-effect), so an
            // agent can pick the right one instead of guessing from the name. "unknown" = not probed
            // yet (the first run classifies in the background).
            arr.push_back({ {"name", p.name}, {"path", p.path},
                            {"format", P::plugin_format_name(p.format)},
                            {"class", P::plugin_class_name(p.cls)},
                            {"vendor", p.vendor},
                            {"probed", p.probed} });
        }
        json r = ok(); r["plugins"] = arr; return r;
    };
    // CLAP hosting: assign a `.clap` bundle as a track's instrument, or append one as an effect.
    // Loading is ASYNC — a slow plugin ctor (e.g. Surge scanning its wavetable dir) runs on a
    // background worker so it never wedges the control-server drain. These return immediately with
    // `loading:true`; poll `plugin_load_status` (or watch get_audio_graph) until the node appears.
    handlers_["set_track_clap_instrument"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string path = b.value("path", std::string());
        if (!P::session_request_track_clap_instrument(c.session, track, path.c_str()))
            return err(code::kBadArg, "could not queue CLAP instrument: '" + path + "'");
        json r = ok(); r["loading"] = !path.empty(); return r;   // "" clears inline (not loading)
    };
    handlers_["add_track_clap_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string path = b.value("path", std::string());
        if (!P::session_request_track_clap_effect(c.session, track, path.c_str()))
            return err(code::kBadArg, "could not queue CLAP effect: '" + path + "'");
        json r = ok(); r["loading"] = true; return r;   // effect index is known once applied (see get_audio_graph)
    };
    // Poll the async CLAP loader: {pending: <in-flight loads>, error: "<last failure or ''>"}.
    handlers_["plugin_load_status"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json r = ok();
        r["pending"] = P::session_plugin_loads_pending(c.session);
        r["error"] = P::session_last_plugin_load_error(c.session);
        return r;
    };
    // Generic preset browse/load for a track's instrument (no per-plugin code). list_presets
    // scans + returns [{name,id}]; the agent picks by name (sonic-intent guidance) and calls
    // load_preset with the id. CLAP today via the plugin's preset-discovery + preset-load exts.
    handlers_["list_presets"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string filter = b.value("filter", std::string());   // narrow by name substring
        const int n = P::session_track_preset_scan(c.session, track, filter.c_str());
        json arr = json::array();
        for (int i = 0; i < n; ++i) {
            json p = { {"name", P::session_track_preset_name(c.session, track, i)},
                       {"id",   P::session_track_preset_id(c.session, track, i)} };
            const char* cat = P::session_track_preset_category(c.session, track, i);
            if (cat && *cat) p["category"] = cat;
            const int tn = P::session_track_preset_tag_count(c.session, track, i);
            if (tn > 0) { json tags = json::array();
                for (int k = 0; k < tn; ++k) tags.push_back(P::session_track_preset_tag(c.session, track, i, k));
                p["tags"] = std::move(tags); }
            p["loadable"] = P::session_track_preset_loadable(c.session, track, i) != 0;  // false = browse-only
            arr.push_back(std::move(p));
        }
        json r = ok(); r["count"] = n; r["presets"] = arr; return r;
    };
    handlers_["load_preset"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string id = b.value("id", b.value("preset", std::string()));
        if (id.empty()) return err(code::kBadArg, "need a preset id (from list_presets)");
        if (!P::session_track_preset_load(c.session, track, id.c_str()))
            return err(code::kBadArg, "preset load failed (no instrument loaded, or unknown/invalid preset id): '" + id + "'");
        return ok();
    };
    handlers_["remove_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), effect = b.value("effect", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!in_range(effect, P::session_effect_count(c.session, track)))
            return err(code::kOutOfRange, "effect index " + std::to_string(effect) + " out of range");
        P::session_remove_effect(c.session, track, effect);
        return ok();
    };
    // Everything that can be added as an EFFECT: native audio operators + every installed plugin
    // the probe classified as an effect. (It used to return five hard-coded VST3 names.)
    handlers_["list_effects"] = [](const ControlCtx& c, const json&) {
        json arr = json::array();
        for (int k = 0, n = c.session ? P::session_available_audio_op_count(c.session, 0) : 0; k < n; ++k)
            arr.push_back({ {"name", P::session_available_audio_op_name(c.session, 0, k)}, {"format", "native"} });
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            if (p.cls == P::kClassInstrument || p.cls == P::kClassFailed || p.cls == P::kClassCrashed) continue;
            arr.push_back({ {"name", p.name}, {"path", p.path},
                            {"format", P::plugin_format_name(p.format)},
                            {"class", P::plugin_class_name(p.cls)} });
        }
        json r = ok(); r["effects"] = arr; return r;
    };
    // --- Native audio operators (AO-1). index -1 = instrument slot, >=0 = effect. ---
    handlers_["add_audio_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int idx = P::session_add_audio_effect(c.session, track, op.c_str());
        if (idx < 0) return err(code::kBadArg, "not a valid audio effect operator: '" + op + "'");
        json r = ok(); r["index"] = idx; return r;
    };
    handlers_["remove_audio_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_remove_audio_effect(c.session, track, index);
        return ok();
    };
    // A bare native-instrument track (no VST3 handle) — the home for a native audio node graph.
    // This is the only programmatic way to create a graph-capable track (add_track's instrument
    // path builds a VST3/plugin track; slice_to_midi needs an audio clip). Optional "instrument"
    // sets the native instrument op in the same call so the track is graph-ok immediately.
    handlers_["add_graph_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string name = b.value("name", std::string());
        const int idx = P::session_add_graph_track(c.session, name.c_str());
        if (idx < 0) return err(code::kInternal, "add_graph_track failed (kMaxTracks reached?)");
        const std::string inst = b.value("instrument", std::string());
        if (!inst.empty() && !P::session_set_track_audio_instrument(c.session, idx, inst.c_str()))
            return err(code::kBadArg, "track created but instrument invalid: '" + inst + "'");
        json r = ok(); r["track"] = idx; return r;
    };
    handlers_["set_track_audio_instrument"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        if (!P::session_set_track_audio_instrument(c.session, track, op.c_str()))
            return err(code::kBadArg, "not a valid audio instrument operator: '" + op + "'");
        return ok();
    };
    handlers_["set_audio_op_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", -1), param = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_op_param_set(c.session, track, index, param, b.value("value", 0.f));
        return ok();
    };
    handlers_["set_audio_op_param_by_name"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = lower_copy_audio(b.value("name", b.value("param", std::string())));
        if (name.empty()) return err(code::kBadArg, "need name");
        const int pc = P::session_audio_op_param_count(c.session, track, index);
        int hit = -1;
        for (int p = 0; p < pc; ++p) {
            if (lower_copy_audio(P::session_audio_op_param_name(c.session, track, index, p)) == name) {
                if (hit >= 0) return err(code::kBadArg, "ambiguous param name '" + name + "'");
                hit = p;
            }
        }
        if (hit < 0) return err(code::kNotFound, "no native audio param named '" + name + "'");
        P::session_audio_op_param_set(c.session, track, index, hit, b.value("value", 0.f));
        json r = ok(); r["param"] = hit; r["name"] = P::session_audio_op_param_name(c.session, track, index, hit); return r;
    };

}

}  // namespace vivid
