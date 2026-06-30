#include "cli/control_server.h"
#include "cli/control_errors.h"
#include "cli/control_parse.h"

#include <httplib.h>

#include "audio/vst3_host.h"
#include "ui/node_graph.h"
#include "gpu/visual_graph.h"
#include "gpu/operator_scan.h"          // load_and_register_operator (live install)
#include "packages/package_manager.h"   // install_package
#include "app/app.h"                     // App: op_registry + op_loaders
#include "mapping.h"
#include "transport.h"
#include "persist.h"
#include "midi/midi_clip.h"

#include <chrono>
#include <cctype>
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

bool parse_vop(const std::string& s, VOp& out) {
    if (s == "Plasma") out = VOp::Plasma;
    else if (s == "Video") out = VOp::Video;
    else if (s == "Feedback") out = VOp::Feedback;
    else if (s == "Blur") out = VOp::Blur;
    else if (s == "Output") out = VOp::Output;
    else return false;
    return true;
}
// vop_name now comes from gpu/visual_graph.h (vivid::vop_name).
int op_index_by_id(VisualGraph* vg, int id) {
    if (!vg) return -1;
    auto& ns = vg->nodes();
    for (int i = 0; i < static_cast<int>(ns.size()); ++i) if (ns[i].id == id) return i;
    return -1;
}

// Index validation against the live session. On failure, fills `e` with a
// stable out_of_range error and returns false (so handlers report the truth
// instead of silently no-op'ing and falsely reporting success).
bool need_track(vivid_poc::Session* s, int t, json& e) {
    const int n = vivid_poc::session_track_count(s);
    if (in_range(t, n)) return true;
    e = err(code::kOutOfRange, "track " + std::to_string(t) + " out of range [0," + std::to_string(n) + ")");
    return false;
}
bool need_scene(vivid_poc::Session* s, int sc, json& e) {
    const int n = vivid_poc::session_scene_count(s);
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
    using vivid_poc::Session;
    namespace P = vivid_poc;

    // ---------------- introspection ----------------
    handlers_["status"] = [](const ControlCtx& c, const json&) {
        json r = ok();
        if (c.session) { r["tracks"] = P::session_track_count(c.session); r["scenes"] = P::session_scene_count(c.session); }
        if (c.transport) { r["bpm"] = c.transport->bpm.load(std::memory_order_relaxed);
                           r["beats"] = c.transport->beats.load(std::memory_order_relaxed); }
        r["ops"] = c.graph ? c.graph->op_count() : 0;
        if (c.vgraph && c.vgraph->registry()) r["op_types"] = c.vgraph->registry()->type_names();  // spawnable ops
        return r;
    };
    // Full operator catalog for agent discovery: every registered op (built-in AND
    // loaded dylib) with its display_name/summary/keywords + param + port schema.
    handlers_["list_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.vgraph || !c.vgraph->registry()) return err(code::kNoVgraph, "no visuals graph");
        auto ptype = [](uint32_t t) -> const char* {
            switch (t) { case VIVID_PARAM_INT: return "int"; case VIVID_PARAM_BOOL: return "bool";
                         case VIVID_PARAM_FILE: return "file"; case VIVID_PARAM_TEXT: return "text";
                         default: return "float"; }
        };
        auto* reg = c.vgraph->registry();
        json arr = json::array();
        for (const auto& name : reg->type_names()) {
            const VividOperatorDescriptor* d = reg->descriptor_for(name);
            if (!d) continue;
            json jo;
            jo["name"] = name;
            if (d->display_name && *d->display_name) jo["display_name"] = d->display_name;
            if (d->summary && *d->summary)           jo["summary"] = d->summary;
            json kws = json::array();
            for (uint32_t i = 0; i < d->keyword_count; ++i)
                if (d->keywords && d->keywords[i]) kws.push_back(d->keywords[i]);
            jo["keywords"] = kws;
            jo["gpu"] = d->has_process_gpu != 0;
            json params = json::array();
            for (uint32_t i = 0; i < d->param_count; ++i) {
                const VividParamDescriptor& p = d->params[i];
                json jp = { {"name", p.name ? p.name : ""}, {"type", ptype(p.type)},
                            {"default", p.default_value}, {"min", p.min_value}, {"max", p.max_value} };
                if (p.description && *p.description) jp["description"] = p.description;
                if (p.group && *p.group)             jp["group"] = p.group;
                params.push_back(jp);
            }
            jo["params"] = params;
            json ports = json::array();
            for (uint32_t i = 0; i < d->port_count; ++i)
                ports.push_back({ {"name", d->ports[i].name ? d->ports[i].name : ""},
                                  {"dir", d->ports[i].direction == VIVID_PORT_OUTPUT ? "out" : "in"} });
            jo["ports"] = ports;
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
            nodes.push_back({ {"id", id}, {"op", g.op_kind_name(i)}, {"input", in}, {"params", params} });
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
    handlers_["get_mappings"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        json arr = json::array();
        for (const auto& m : c.graph->mappings())
            arr.push_back({ {"src", m.source}, {"dst", m.dest}, {"amount", m.amount},
                            {"curve", m.curve}, {"invert", m.invert}, {"lo", m.out_lo}, {"hi", m.out_hi} });
        json r = ok(); r["mappings"] = arr; return r;
    };

    // ---------------- visuals construction ----------------
    handlers_["add_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const std::string op = b.value("op", std::string());
        OpRegistry* reg = c.vgraph->registry();
        if (!reg || !reg->has(op)) {
            std::string types; for (const auto& t : (reg ? reg->type_names() : std::vector<std::string>{})) { if (!types.empty()) types += ", "; types += t; }
            return err(code::kBadArg, "unknown op '" + op + "' (valid: " + types + ")");
        }
        const int idx = c.vgraph->add_node(op);   // operator-driven: any registered op type
        json r = ok(); r["id"] = c.vgraph->nodes()[idx].id; r["index"] = idx; return r;
    };
    handlers_["remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that id");
        c.vgraph->remove_node(idx);
        return ok();
    };
    handlers_["connect_nodes"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const int in_id = b.value("input_id", -1);
        const int in_idx = (in_id < 0) ? -1 : op_index_by_id(c.vgraph, in_id);
        if (in_id >= 0 && in_idx < 0) return err(code::kNotFound, "no node with that input_id");
        c.vgraph->set_input(idx, in_idx);
        return ok();
    };
    handlers_["set_generator"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        VOp op; if (!parse_vop(b.value("op", std::string()), op)) return err(code::kBadArg, "bad op");
        c.vgraph->set_generator(op);
        return ok();
    };
    handlers_["set_active_output"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that id");
        c.vgraph->set_active_output(idx);
        return ok();
    };
    handlers_["set_node_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const std::string name = b.value("name", std::string());
        int local = -1;
        for (int l = 0; l < c.graph->op_param_count_at(idx); ++l)
            if (name == c.graph->op_param_label_at(idx, l)) { local = l; break; }
        if (local < 0) return err(code::kNotFound, "no param '" + name + "' on that node");
        c.graph->set_op_param_base_at(idx, local, b.value("value", 0.f));
        return ok();
    };
    handlers_["add_data_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("source", std::string());
        const int cid = char_id_from_source(src);
        if (cid < 0) return err(code::kBadArg, "bad source (e.g. master.transient, track_2.low)");
        c.graph->add_data_node(src, cid);
        return ok();
    };

    // ---------------- mapping (the bridge) ----------------
    handlers_["connect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("src", std::string()), dst = b.value("dst", std::string());
        if (src.empty() || dst.empty()) return err(code::kBadArg, "need src and dst");
        c.graph->add_mapping(src, dst, b.value("amount", 1.0f), b.value("curve", 0.0f),
                             b.value("invert", false), b.value("lo", 0.0f), b.value("hi", 1.0f));
        return ok();
    };
    handlers_["disconnect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string dst = b.value("dst", std::string());
        if (dst.empty()) return err(code::kBadArg, "need dst");
        c.graph->disconnect_dest(dst);
        return ok();
    };

    // ---------------- audio authoring ----------------
    handlers_["set_bpm"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        const double bpm = b.value("bpm", 120.0);
        if (!(bpm > 0.0) || bpm > 1000.0) return err(code::kBadArg, "bpm out of range (0, 1000]");
        c.transport->bpm.store(bpm, std::memory_order_relaxed);
        return ok();
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
            for (const auto& jn : b["notes"])
                notes.push_back({ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f) });
        P::session_set_clip(c.session, track, scene,
                            notes.data(), static_cast<int>(notes.size()), b.value("length", 4.0));
        json r = ok(); r["notes"] = static_cast<int>(notes.size()); return r;
    };
    handlers_["add_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = b.value("name", std::string());
        for (int k = 0; k < P::session_available_effect_count(); ++k)
            if (name == P::session_available_effect_name(k)) {
                const bool okk = P::session_add_effect_by_index(c.session, track, k);
                return okk ? ok() : err(code::kInternal, "add failed");
            }
        return err(code::kNotFound, "unknown effect '" + name + "'");
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
}

}  // namespace vivid
