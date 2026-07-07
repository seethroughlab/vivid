#include "cli/control_server.h"
#include "cli/control_handlers.h"   // audit #7: per-family register_*_handlers()
#include "cli/control_errors.h"
#include "cli/control_parse.h"
#include "cli/control_json.h"

#include <httplib.h>

#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"
#include "ui/node_graph.h"
#include "gpu/visual_graph.h"
#include "gpu/operator_scan.h"          // load_and_register_operator (live install)
#include "packages/package_manager.h"   // install_package
#include "app/app.h"                     // App: op_registry + op_loaders
#include "app/project_io.h"              // folder-aware save/load + project-local operators
#include "mapping.h"
#include "transport.h"
#include "persist.h"
#include "midi/midi_clip.h"
#include "midi/note_json.h"
#include "version.h"                     // VIVID_VERSION (generated, P4.1)
#include "operator_api/types.h"          // VIVID_OPERATOR_ABI_VERSION
#include "app/runtime_health.h"          // collect_health (P4.3)

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using nlohmann::json;

namespace vivid {
namespace {

using control::ok;
using control::err;
using control::in_range;
using control::char_id_from_source;
namespace code = control::code;

// Index validation against the live session. On failure, fills `e` with a
// stable out_of_range error and returns false (so handlers report the truth
// instead of silently no-op'ing and falsely reporting success).
bool need_track(vivid::session::Session* s, int t, json& e) {
    const int n = vivid::session::session_track_count(s);
    if (in_range(t, n)) return true;
    e = err(code::kOutOfRange, "track " + std::to_string(t) + " out of range [0," + std::to_string(n) + ")");
    return false;
}
bool need_scene(vivid::session::Session* s, int sc, json& e) {
    const int n = vivid::session::session_scene_count(s);
    if (in_range(sc, n)) return true;
    e = err(code::kOutOfRange, "scene " + std::to_string(sc) + " out of range [0," + std::to_string(n) + ")");
    return false;
}

}  // namespace

ControlServer::ControlServer() { register_handlers(); }
ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(int port) {
    auto* svr = new httplib::Server();
    server_ = svr;
    port_ = port;
    svr->Post(R"(/([A-Za-z_][A-Za-z0-9_]*))", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string method = req.matches[1].str();
        json body = json::object();
        if (!req.body.empty()) {
            try { body = json::parse(req.body); }
            catch (...) { res.set_content(err(code::kBadJson, "invalid JSON body").dump(), "application/json"); return; }
        }
        Pending p; p.method = method; p.body = std::move(body);
        auto fut = p.reply.get_future();
        { std::lock_guard<std::mutex> lk(mtx_); queue_.push_back(std::move(p)); }
        if (fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready)
            res.set_content(fut.get().dump(), "application/json");
        else
            res.set_content(err(code::kTimeout, "main loop not draining").dump(), "application/json");
    });
    running_ = true;
    thread_ = std::thread([this, svr]() {
        if (!svr->listen("127.0.0.1", port_)) {
            std::fprintf(stderr, "[vivid] control server: bind 127.0.0.1:%d FAILED\n", port_);
            running_ = false;
        }
    });
    // Give listen() a moment to bind/fail.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (running_) std::fprintf(stderr, "[vivid] control server listening on 127.0.0.1:%d\n", port_);
    return running_;
}

void ControlServer::stop() {
    if (server_) {
        static_cast<httplib::Server*>(server_)->stop();
        if (thread_.joinable()) thread_.join();
        delete static_cast<httplib::Server*>(server_);
        server_ = nullptr;
    }
    running_ = false;
}

void ControlServer::process_pending(const ControlCtx& ctx) {
    std::deque<Pending> local;
    { std::lock_guard<std::mutex> lk(mtx_); local.swap(queue_); }
    for (auto& p : local) {
        json reply;
        auto it = handlers_.find(p.method);
        if (it == handlers_.end()) reply = err(code::kUnknownMethod, "unknown method: " + p.method);
        else {
            try { reply = it->second(ctx, p.body); }
            catch (const std::exception& e) { reply = err(code::kInternal, e.what()); }
            catch (...) { reply = err(code::kInternal, "handler exception"); }
        }
        p.reply.set_value(std::move(reply));
    }
}

void ControlServer::register_handlers() {
    using vivid::session::Session;
    namespace P = vivid::session;

    // ---------------- introspection ----------------
    handlers_["status"] = [](const ControlCtx& c, const json&) {
        json r = ok();
        if (c.session) { r["tracks"] = P::session_track_count(c.session); r["scenes"] = P::session_scene_count(c.session); }
        if (c.transport) { r["bpm"] = c.transport->bpm.load(std::memory_order_relaxed);
                           r["beats"] = c.transport->beats.load(std::memory_order_relaxed);
                           r["playing"] = c.transport->is_playing(); }
        r["ops"] = c.graph ? c.graph->op_count() : 0;
        if (c.vgraph && c.vgraph->registry()) r["op_types"] = c.vgraph->registry()->type_names();  // spawnable ops
        return r;
    };
    // Version surface for agents/clients + the version-guard check: the app version,
    // the operator ABI a loaded dylib must match, and the session schema version a
    // saved file is gated against. One place to read all three compatibility numbers.
    handlers_["get_version"] = [](const ControlCtx&, const json&) {
        json r = ok();
        r["app_version"]    = VIVID_VERSION;
        r["operator_abi"]   = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
        r["session_schema"] = kSessionSchemaVersion;
#ifdef NDEBUG
        r["build_type"] = "release";
#else
        r["build_type"] = "debug";
#endif
        return r;
    };
    // Runtime health: a rolled-up snapshot (severity ok|warning|error) of the engine —
    // gpu device/error state, operator/graph counts + any missing ops, loaded packages,
    // control liveness. Cheap; meant for agents/monitors to poll.
    handlers_["get_health"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        json r = ok();
        r["health"] = to_json(collect_health(*c.app));
        return r;
    };
    // Full operator catalog for agent discovery: every registered op (built-in AND
    // loaded dylib) with its display_name/summary/keywords + param + port schema.
    handlers_["list_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.vgraph || !c.vgraph->registry()) return err(code::kNoVgraph, "no visuals graph");
        auto* reg = c.vgraph->registry();
        json arr = json::array();
        for (const auto& name : reg->type_names()) {
            const VividOperatorDescriptor* d = reg->descriptor_for(name);
            if (!d) continue;
            // Rich shared schema (params + ports + semantic metadata) so agents pick/wire by intent.
            json jo = control_json::operator_to_json(*d);
            jo["gpu"] = d->has_process_gpu != 0;   // back-compat flag
            arr.push_back(jo);
        }
        json r = ok(); r["operators"] = arr; return r;
    };
    // Install an operator package from a directory (its vivid-package.json + sources):
    // compile each operator to a .dylib (managed dir) and register it LIVE — the new
    // op is immediately spawnable, no restart. (Compile blocks the frame briefly; a
    // background compile is P2.4.) New ops appear in list_operators afterwards.
    handlers_["install_operator_package"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.vgraph || !c.vgraph->registry()) return err(code::kNoVgraph, "no visuals graph");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "install requires \"path\" (a package directory)");
        PackageInstallResult ir = install_package(path);
        if (!ir.ok) return err(code::kBadArg, ir.error);
        int registered = 0;
        json ops = json::array();
        for (const auto& ci : ir.compiles) {
            json jo = { {"name", ci.op_name}, {"compiled", ci.success} };
            if (!ci.success) {
                jo["error"] = ci.error_output;
            } else {
                const std::string regname = load_and_register_operator(
                    ci.dylib_path, c.app->op_registry, c.app->op_loaders);
                jo["registered"] = !regname.empty();
                if (!regname.empty()) { jo["op"] = regname; ++registered; }
                else jo["note"] = "compiled but not registered (name already in use)";
            }
            ops.push_back(jo);
        }
        json r = ok(); r["package"] = ir.name; r["registered"] = registered; r["operators"] = ops;
        return r;
    };
    handlers_["get_session"] = [](const ControlCtx& c, const json&) {
        if (!c.session || !c.graph) return err(code::kNoSession, "no session");
        json r = ok();
        r["session"] = session_to_json(c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h);
        return r;
    };
    handlers_["list_tracks"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        Session* s = c.session;
        json arr = json::array();
        for (int t = 0; t < P::session_track_count(s); ++t) {
            json jt;
            jt["index"] = t;
            jt["id"] = P::session_track_id(s, t);   // stable id — use in mapping sources "track_<id>.<kind>"
            jt["name"] = P::session_track_name(s, t);
            jt["gain"] = P::session_track_gain(s, t);
            jt["is_audio"] = P::session_track_is_audio(s, t);
            jt["active_clip"] = P::session_active_clip(s, t);
            jt["queued_clip"] = P::session_queued_clip(s, t);
            jt["level"] = P::session_track_level(s, t);
            jt["transient"] = P::session_track_transient(s, t);
            jt["bands"] = { P::session_track_band(s, t, 0), P::session_track_band(s, t, 1), P::session_track_band(s, t, 2) };
            json devs = json::array();
            if (!P::session_track_is_audio(s, t)) devs.push_back({ {"device", 0}, {"kind", "instrument"}, {"name", P::session_track_name(s, t)} });
            for (int e = 0; e < P::session_effect_count(s, t); ++e)
                devs.push_back({ {"device", e + 1}, {"kind", "fx"}, {"name", P::session_effect_name(s, t, e)} });
            jt["devices"] = devs;
            arr.push_back(jt);
        }
        json r = ok(); r["tracks"] = arr; return r;
    };
    handlers_["list_params"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        Session* s = c.session;
        const int track = b.value("track", 0), device = b.value("device", 0);
        json e; if (!need_track(s, track, e)) return e;
        const int limit = b.value("limit", 64);
        std::string filter = b.value("filter", std::string());
        for (auto& ch : filter) ch = static_cast<char>(std::tolower((unsigned char)ch));
        const int pc = P::session_param_count(s, track, device);
        json arr = json::array();
        for (int i = 0; i < pc && static_cast<int>(arr.size()) < limit; ++i) {
            std::string nm = P::session_param_name(s, track, device, i);
            if (!filter.empty()) {
                std::string low = nm; for (auto& ch : low) ch = static_cast<char>(std::tolower((unsigned char)ch));
                if (low.find(filter) == std::string::npos) continue;
            }
            arr.push_back({ {"index", i}, {"id", P::session_param_id(s, track, device, i)},
                            {"name", nm}, {"value", P::session_param_value(s, track, device, i)} });
        }
        json r = ok(); r["count"] = pc; r["params"] = arr; return r;
    };
    handlers_["get_graph"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        auto& g = *c.graph;
        json nodes = json::array();
        for (int i = 0; i < g.op_count(); ++i) {
            int op = 0, in = -1, id = 0; float x = 0, y = 0; g.get_op(i, op, in, id, x, y);
            json params = json::array();
            for (int l = 0; l < g.op_param_count_at(i); ++l)
                params.push_back({ {"name", g.op_param_label_at(i, l)}, {"base", g.op_param_base_at(i, l)},
                                   {"value", g.op_param_value_at(i, l)}, {"wired", g.op_param_wired_at(i, l)} });
            nodes.push_back({ {"id", id}, {"op", g.op_kind_name(i)}, {"input", in},
                              {"x", x}, {"y", y}, {"params", params} });
        }
        json dnodes = json::array();
        for (int i = 0; i < g.node_count(); ++i) {
            float x = 0, y = 0; int cid = 0; std::string title; g.get_node(i, x, y, cid, title);
            dnodes.push_back({ {"char_id", cid}, {"title", title} });
        }
        json r = ok();
        r["nodes"] = nodes;
        r["data_nodes"] = dnodes;
        if (c.vgraph) { r["active_output"] = c.vgraph->active_output_id(); r["generator"] = vop_name(c.vgraph->generator()); }
        return r;
    };
    // Auto-arrange the op nodes into a tidy layered layout (the "Re-layout" button).
    handlers_["layout_graph"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        c.graph->layout_nodes();
        json r = ok(); r["nodes"] = c.graph->op_count(); return r;
    };
    handlers_["get_mappings"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        json arr = json::array();
        for (const auto& m : c.graph->mappings())
            arr.push_back({ {"src", m.source}, {"dst", m.dest}, {"amount", m.amount},
                            {"curve", m.curve}, {"invert", m.invert}, {"lo", m.out_lo}, {"hi", m.out_hi} });
        json r = ok(); r["mappings"] = arr; return r;
    };

    // Mapping affordances: the valid bridge SOURCES an agent can wire to a dest (previously only
    // documented in a docstring). Every audio characteristic — master + per live track (by stable
    // id) — as a ready-to-use source string, its 0..1 range, and a description. Dests are the
    // params from list_operators / list_audio_ops (build "node:<id>.<param>" or "param:t:d:i").
    handlers_["list_mapping_sources"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        static const char* kKinds[] = { "level", "transient", "low", "mid", "high" };
        static const char* kDescs[] = { "overall loudness", "attack/onset energy",
                                        "low-band energy", "mid-band energy", "high-band energy" };
        json sources = json::array();
        auto emit = [&](const std::string& prefix, const std::string& label) {
            for (int k = 0; k < 5; ++k)
                sources.push_back({ {"source", prefix + "." + kKinds[k]}, {"kind", kKinds[k]},
                                    {"label", label + " " + kKinds[k]}, {"range", {0.0, 1.0}},
                                    {"description", kDescs[k]} });
        };
        emit("master", "master");
        for (int t = 0; t < P::session_track_count(c.session); ++t)
            emit("track_" + std::to_string(P::session_track_id(c.session, t)),
                 P::session_track_name(c.session, t));
        json r = ok(); r["sources"] = sources; return r;
    };

    register_visuals_handlers(handlers_);   // ---- visuals construction ----

    register_mappings_handlers(handlers_);   // ---- mapping (the bridge) ----

    // ---------------- audio authoring ----------------
    handlers_["set_bpm"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        const double bpm = b.value("bpm", 120.0);
        if (!(bpm > 0.0) || bpm > 1000.0) return err(code::kBadArg, "bpm out of range (0, 1000]");
        c.transport->bpm.store(bpm, std::memory_order_relaxed);
        return ok();
    };
    // Transport play/stop. set_playing{playing} sets it; toggle_play flips; reset_transport
    // returns to the top (bar 1). Pausing freezes the clock so clips stop advancing.
    handlers_["set_playing"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        c.transport->set_playing(b.value("playing", true));
        json r = ok(); r["playing"] = c.transport->is_playing(); return r;
    };
    handlers_["toggle_play"] = [](const ControlCtx& c, const json&) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        json r = ok(); r["playing"] = c.transport->toggle_playing(); return r;
    };
    handlers_["reset_transport"] = [](const ControlCtx& c, const json&) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        c.transport->reset();
        json r = ok(); r["beats"] = 0.0; return r;
    };
    handlers_["launch_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_launch_clip(c.session, track, scene);
        return ok();
    };
    handlers_["launch_scene"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int scene = b.value("scene", 0);
        json e; if (!need_scene(c.session, scene, e)) return e;
        P::session_launch_scene(c.session, scene);
        return ok();
    };
    handlers_["set_track_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_gain(c.session, track, b.value("gain", 0.8f));
        return ok();
    };
    handlers_["arm_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1);   // -1 disarms
        if (track >= 0) { json e; if (!need_track(c.session, track, e)) return e; }
        P::session_set_armed_track(c.session, track);
        json r = ok(); r["armed"] = P::session_armed_track(c.session); return r;
    };
    handlers_["note_on"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        if (P::session_armed_track(c.session) < 0) return err(code::kBadArg, "no armed track");
        const int pitch = b.value("pitch", -1);
        if (pitch < 0 || pitch > 127) return err(code::kBadArg, "pitch out of range [0,127]");
        P::session_note_on(c.session, pitch, b.value("vel", 0.8f));
        return ok();
    };
    handlers_["note_off"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int pitch = b.value("pitch", -1);
        if (pitch < 0 || pitch > 127) return err(code::kBadArg, "pitch out of range [0,127]");
        P::session_note_off(c.session, pitch);
        return ok();
    };
    handlers_["record"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const bool on = b.value("on", true);
        if (on && P::session_armed_track(c.session) < 0) return err(code::kBadArg, "no armed track");
        P::session_set_recording(c.session, on, b.value("count_in", 0.0));
        json r = ok(); r["recording"] = P::session_is_recording(c.session); return r;
    };
    handlers_["set_clip_loop"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_clip_loop(c.session, track, scene, b.value("loop_start", 0.0), b.value("loop_end", 0.0));
        return ok();
    };
    handlers_["metronome"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_set_metronome(c.session, b.value("on", true) ? 1 : 0);
        json r = ok(); r["metronome"] = P::session_get_metronome(c.session); return r;
    };
    handlers_["set_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), device = b.value("device", 0), index = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const int pc = P::session_param_count(c.session, track, device);
        if (pc == 0) return err(code::kOutOfRange, "device " + std::to_string(device) + " has no params (or out of range)");
        if (!in_range(index, pc)) return err(code::kOutOfRange, "param index " + std::to_string(index) + " out of range [0," + std::to_string(pc) + ")");
        P::session_set_param(c.session, track, device, P::session_param_id(c.session, track, device, index), b.value("value", 0.f));
        return ok();
    };
    handlers_["set_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        std::vector<P::ClipNote> notes;
        if (b.contains("notes"))
            for (const auto& jn : b["notes"]) {
                P::ClipNote cn{ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f), {} };
                P::expr_from_json(jn, cn);   // optional per-note bend/pressure/timbre curves
                notes.push_back(std::move(cn));
            }
        P::session_set_clip(c.session, track, scene,
                            notes.data(), static_cast<int>(notes.size()), b.value("length", 4.0));
        json r = ok(); r["notes"] = static_cast<int>(notes.size()); return r;
    };
    // Read a MIDI clip back (the read half that read-modify-write authoring tools need).
    handlers_["get_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::ClipNote buf[1024];
        const int n = P::session_get_clip(c.session, track, scene, buf, 1024);
        json notes = json::array();
        for (int i = 0; i < n; ++i) {
            json jn = { {"p", buf[i].pitch}, {"s", buf[i].start}, {"d", buf[i].dur}, {"v", buf[i].vel} };
            P::expr_to_json(buf[i], jn);
            notes.push_back(jn);
        }
        json r = ok(); r["notes"] = notes; r["length"] = P::session_clip_length(c.session, track, scene); return r;
    };
    // ---------------- audio-clip warp / shaping (A2) ----------------
    handlers_["audio_set_warp"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const std::string m = b.value("mode", std::string("complex"));
        const int mode = (m == "beats") ? 1 : (m == "repitch") ? 2 : 0;
        P::session_set_audio_warp(c.session, track, scene, b.value("enabled", true) ? 1 : 0, mode);
        json r = ok(); r["warp"] = P::session_get_audio_warp(c.session, track, scene); return r;
    };
    handlers_["audio_set_pitch"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_pitch(c.session, track, scene, b.value("semitones", 0.0f));
        json r = ok(); r["semitones"] = P::session_get_audio_pitch(c.session, track, scene); return r;
    };
    handlers_["audio_set_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_gain(c.session, track, scene, b.value("gain", 1.0f));
        return ok();
    };
    handlers_["audio_set_reverse"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_reverse(c.session, track, scene, b.value("on", true) ? 1 : 0);
        return ok();
    };
    handlers_["audio_auto_warp"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const int npts = P::session_audio_auto_warp(c.session, track, scene, b.value("sensitivity", 0.5f));
        json r = ok(); r["markers"] = npts; return r;
    };
    // ---------------- clip pool (loose clips that live outside the grid) ----------------
    // The pool is UI-thread-only storage; these handlers run on the UI thread (like all others).
    handlers_["list_pool"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json arr = json::array();
        for (int i = 0, n = P::session_pool_count(c.session); i < n; ++i)
            arr.push_back({ {"index", i}, {"name", P::session_pool_name(c.session, i)},
                            {"length", P::session_pool_length(c.session, i)},
                            {"kind", P::session_pool_is_audio(c.session, i) ? "audio" : "midi"} });
        json r = ok(); r["pool"] = arr; return r;
    };
    // Move a grid clip into the pool (MIDI or audio): the source cell is cleared (the clip
    // leaves the session). Returns the new pool index.
    handlers_["pool_stash"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        std::string name = b.value("name", std::string());
        if (name.empty()) { char nm[28]; std::snprintf(nm, sizeof nm, "%.12s %c", P::session_track_name(c.session, track), 'A' + scene); name = nm; }
        if (P::session_track_is_audio(c.session, track)) {
            const int idx = P::session_pool_stash_audio(c.session, track, scene, name.c_str());
            if (idx < 0) return err(code::kBadArg, "clip is empty");
            json r = ok(); r["index"] = idx; r["kind"] = "audio"; return r;
        }
        P::ClipNote buf[1024];
        const int n = P::session_get_clip(c.session, track, scene, buf, 1024);
        if (n <= 0) return err(code::kBadArg, "clip is empty");
        const double len = P::session_clip_length(c.session, track, scene);
        const int idx = P::session_pool_add(c.session, buf, n, len, name.c_str());
        P::session_set_clip(c.session, track, scene, nullptr, 0, len);   // take it out of the grid
        json r = ok(); r["index"] = idx; r["kind"] = "midi"; return r;
    };
    // Place a pool clip into a grid cell, overwriting it. Types must match: an audio clip
    // goes on an audio track, a MIDI clip on an instrument track.
    handlers_["pool_place"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1), track = b.value("track", 0), scene = b.value("scene", 0);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const bool poolAudio = P::session_pool_is_audio(c.session, index);
        const bool trackAudio = P::session_track_is_audio(c.session, track);
        if (poolAudio != trackAudio)
            return err(code::kBadArg, poolAudio ? "audio clip needs an audio track" : "MIDI clip needs an instrument track");
        if (poolAudio) {
            if (!P::session_pool_place_audio(c.session, index, track, scene)) return err(code::kInternal, "place failed");
            json r = ok(); r["kind"] = "audio"; return r;
        }
        P::ClipNote buf[1024];
        const int n = P::session_pool_get(c.session, index, buf, 1024);
        P::session_set_clip(c.session, track, scene, buf, n, P::session_pool_length(c.session, index));
        json r = ok(); r["notes"] = n; r["kind"] = "midi"; return r;
    };
    handlers_["pool_remove"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        P::session_pool_remove(c.session, index);
        return ok();
    };
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
        for (int k = 0; k < P::session_available_effect_count(); ++k)
            if (name == P::session_available_effect_name(k)) {
                const bool okk = P::session_add_effect_by_index(c.session, track, k);
                return okk ? ok() : err(code::kInternal, "add failed");
            }
        return err(code::kNotFound, "unknown effect '" + name + "'");
    };
    // Every installed plugin (VST3 today), for the browser. A path from here works as
    // an "instrument" for add_track or a "name" for add_effect. CLAP/AU hosts TBD.
    handlers_["list_plugins"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            arr.push_back({ {"name", p.name}, {"path", p.path}, {"format", "vst3"} });
        }
        json r = ok(); r["plugins"] = arr; return r;
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
    handlers_["list_effects"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int k = 0; k < P::session_available_effect_count(); ++k) arr.push_back(P::session_available_effect_name(k));
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
    handlers_["audio_graph_remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!P::session_audio_graph_remove_node(c.session, track, node))
            return err(code::kBadArg, "node not removable (unknown, or an instrument/output)");
        return ok();
    };
    handlers_["audio_graph_connect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!P::session_audio_graph_connect(c.session, track, from, to))
            return err(code::kBadArg, "edge rejected (duplicate, self-loop, unknown node, or would create a cycle)");
        return ok();
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
        static const char* kKind[] = { "instrument", "effect", "output" };
        json r = ok();
        r["graph_ok"]   = P::session_track_audio_graph_ok(c.session, track) != 0;
        r["output_id"]  = P::session_track_audio_graph_output_id(c.session, track);
        json nodes = json::array();
        for (int i = 0; i < P::session_track_audio_graph_node_count(c.session, track); ++i) {
            const int k = P::session_track_audio_graph_node_kind(c.session, track, i);
            nodes.push_back({ {"id",   P::session_track_audio_graph_node_id(c.session, track, i)},
                              {"kind", (k >= 0 && k < 3) ? kKind[k] : "unknown"},
                              {"type", P::session_track_audio_graph_node_type(c.session, track, i)} });
        }
        r["nodes"] = nodes;
        json edges = json::array();
        for (int i = 0; i < P::session_track_audio_graph_edge_count(c.session, track); ++i)
            edges.push_back({ {"from", P::session_track_audio_graph_edge_from(c.session, track, i)},
                              {"to",   P::session_track_audio_graph_edge_to(c.session, track, i)} });
        r["edges"] = edges;
        return r;
    };

    // The catalog of NATIVE audio operators (the audio peer of list_operators, which is
    // visual): instruments (sources, no audio input) + effects (audio in->out). Names are
    // stable registry keys for set_track_audio_instrument / add_audio_effect.
    handlers_["list_audio_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        auto* reg = (c.vgraph && c.vgraph->registry()) ? c.vgraph->registry() : nullptr;
        // Each entry carries the full schema (params + semantic metadata), like list_operators —
        // so an agent can pick an op AND know its params without a second call.
        auto emit = [&](int want_source, const char* kind) {
            json arr = json::array();
            for (int i = 0; i < P::session_available_audio_op_count(c.session, want_source); ++i) {
                const char* nm = P::session_available_audio_op_name(c.session, want_source, i);
                const VividOperatorDescriptor* d = reg ? reg->descriptor_for(nm ? nm : "") : nullptr;
                if (d) arr.push_back(control_json::operator_to_json(*d, kind));
                else   arr.push_back({ {"name", nm ? nm : ""}, {"kind", kind} });
            }
            return arr;
        };
        json r = ok();
        r["instruments"] = emit(1, "instrument");
        r["effects"]     = emit(0, "audio_effect");
        return r;
    };

    // The instrument catalog offered when creating a track (a label or a .vst3 path on add).
    handlers_["list_instruments"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int k = 0; k < P::session_available_instrument_count(); ++k) arr.push_back(P::session_available_instrument_name(k));
        json r = ok(); r["instruments"] = arr; return r;
    };
    // Create a track. kind "instrument" (default) needs an "instrument" (catalog label or a
    // .vst3 path); kind "audio" makes a sampler track. Returns the new track index.
    handlers_["add_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string kind = b.value("kind", std::string("instrument"));
        int idx = -1;
        if (kind == "audio") {
            idx = P::session_add_audio_track(c.session);
        } else {
            const std::string inst = b.value("instrument", std::string());
            if (inst.empty()) return err(code::kBadArg, "instrument track needs \"instrument\" (a catalog label or .vst3 path)");
            idx = P::session_add_instrument_track(c.session, inst.c_str());
            if (idx < 0) return err(code::kNotFound, "no instrument matched '" + inst + "' (or kMaxTracks reached)");
        }
        if (idx < 0) return err(code::kInternal, "add_track failed (kMaxTracks reached?)");
        json r = ok(); r["track"] = idx; return r;
    };
    // Delete a track. Also drops audio->visual mappings whose source encodes the removed
    // track's stable id; surviving mappings do not need index renumbering.
    handlers_["remove_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const int rid = P::session_track_id(c.session, track);   // capture the stable id before removal
        if (!P::session_remove_track(c.session, track)) return err(code::kInternal, "remove_track failed");
        int dropped = 0;
        if (c.graph) dropped = c.graph->drop_track_sources(rid);   // drop this track's mappings (id-based; survivors untouched)
        json r = ok(); r["removed"] = track; r["mappings_dropped"] = dropped; return r;
    };

    // ---------------- session author / persist ----------------
    handlers_["save_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        return save_session(path, c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h)
                   ? ok() : err(code::kIoError, "write failed");
    };
    handlers_["load_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err(code::kNoSession, "no session");
        int ww = *c.win_w, wh = *c.win_h;   // don't resize the window via MCP
        if (b.contains("session")) {        // inline JSON
            return session_from_json(b["session"], c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h)
                       ? ok() : err(code::kBadArg, "load failed");
        }
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path or session");
        return load_session(path, c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h)
                   ? ok() : err(code::kIoError, "read failed");
    };

    // ---------------- project workflow (thin layer over session JSON) ----------------
    // Fresh start: empty every clip, reset the visuals to the default chain, drop mappings +
    // data nodes, and clear the current-project pointer. Keeps the loaded instruments + tracks.
    handlers_["new_project"] = [](const ControlCtx& c, const json&) {
        if (!c.session || !c.graph) return err(code::kNoSession, "no session");
        const int nt = P::session_track_count(c.session), ns = P::session_scene_count(c.session);
        for (int t = 0; t < nt; ++t)
            for (int sc = 0; sc < ns; ++sc)
                P::session_set_clip(c.session, t, sc, nullptr, 0, 4.0);
        c.graph->reset_nodes();                        // data nodes + mappings
        if (c.vgraph) { c.vgraph->reset_to_default(); c.vgraph->set_asset_dir(""); }  // Plasma->Feedback->Blur->Output
        if (c.app) {
            c.app->project.current_project_path.clear();
            c.app->project.media_root.clear();
            c.app->project.missing_media.clear();
        }
        json r = ok(); r["tracks"] = nt; return r;
    };
    handlers_["get_project_status"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        json r = ok();
        r["path"] = c.app->project.current_project_path;
        r["media_root"] = c.app->project.media_root;
        r["recent"] = c.app->project.recent_project_paths;
        r["missing_media"] = c.app->project.missing_media;
        r["videos"] = static_cast<int>(c.app->video_paths.size());
        return r;
    };
    handlers_["save_project"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.session || !c.graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", c.app->project.current_project_path);
        if (path.empty()) return err(code::kBadArg, "need path for first save");
        auto sr = project_io::save(*c.app, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h, path);
        if (!sr.ok) return err(code::kIoError, sr.error);
        json r = ok(); r["path"] = c.app->project.current_project_path; r["session_file"] = sr.session_file; return r;
    };
    handlers_["load_project"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.session || !c.graph) return err(code::kNoSession, "no session");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        int ww = *c.win_w, wh = *c.win_h;
        auto lr = project_io::load(*c.app, *c.graph, ww, wh, *c.split_x, *c.dock_h, path);
        if (!lr.ok) return err(code::kIoError, lr.error);
        json r = ok(); r["path"] = c.app->project.current_project_path;
        if (lr.had_package) {   // report project-local operator compile/register outcomes
            json pkg = { {"name", lr.package_name}, {"registered", lr.registered} };
            json ops = json::array();
            for (const auto& o : lr.ops) {
                json jo = { {"name", o.name}, {"compiled", o.compiled}, {"registered", o.registered} };
                if (!o.note.empty())  jo["note"] = o.note;
                if (!o.error.empty()) jo["error"] = o.error;
                ops.push_back(jo);
            }
            pkg["operators"] = ops;
            r["project_package"] = pkg;
        }
        return r;
    };
    handlers_["set_media_root"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        c.app->set_media_root(b.value("path", std::string()));
        json r = ok(); r["media_root"] = c.app->project.media_root; r["videos"] = static_cast<int>(c.app->video_paths.size());
        r["missing_media"] = c.app->project.missing_media; return r;
    };
}

}  // namespace vivid
