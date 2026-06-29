#include "cli/control_server.h"

#include <httplib.h>

#include "audio/vst3_host.h"
#include "ui/node_graph.h"
#include "gpu/visual_graph.h"
#include "mapping.h"
#include "transport.h"
#include "persist.h"
#include "midi/midi_clip.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <vector>

using nlohmann::json;

namespace vivid {
namespace {

json ok(json extra = json::object()) { extra["ok"] = true; return extra; }
json err(const std::string& m) { return json{ {"ok", false}, {"error", m} }; }

bool parse_vop(const std::string& s, VOp& out) {
    if (s == "Plasma") out = VOp::Plasma;
    else if (s == "Video") out = VOp::Video;
    else if (s == "Feedback") out = VOp::Feedback;
    else if (s == "Blur") out = VOp::Blur;
    else if (s == "Output") out = VOp::Output;
    else return false;
    return true;
}
const char* vop_name(VOp op) {
    switch (op) { case VOp::Plasma: return "Plasma"; case VOp::Video: return "Video";
        case VOp::Feedback: return "Feedback"; case VOp::Blur: return "Blur"; default: return "Output"; }
}
int op_index_by_id(VisualGraph* vg, int id) {
    if (!vg) return -1;
    auto& ns = vg->nodes();
    for (int i = 0; i < static_cast<int>(ns.size()); ++i) if (ns[i].id == id) return i;
    return -1;
}
int kind_index(const std::string& k) {
    if (k == "level") return 0; if (k == "transient") return 1; if (k == "low") return 2;
    if (k == "mid") return 3;   if (k == "high") return 4;      return -1;
}
// "master.<kind>" | "track_<n>.<kind>" -> char_id (master=kind, track t = 100 + t*8 + kind)
int char_id_from_source(const std::string& src) {
    const auto dot = src.find('.');
    if (dot == std::string::npos) return -1;
    const std::string head = src.substr(0, dot);
    const int ki = kind_index(src.substr(dot + 1));
    if (ki < 0) return -1;
    if (head == "master") return ki;
    if (head.rfind("track_", 0) == 0) return 100 + std::atoi(head.c_str() + 6) * 8 + ki;
    return -1;
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
            catch (...) { res.set_content(err("invalid JSON body").dump(), "application/json"); return; }
        }
        Pending p; p.method = method; p.body = std::move(body);
        auto fut = p.reply.get_future();
        { std::lock_guard<std::mutex> lk(mtx_); queue_.push_back(std::move(p)); }
        if (fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready)
            res.set_content(fut.get().dump(), "application/json");
        else
            res.set_content(err("timeout (main loop not draining)").dump(), "application/json");
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
        if (it == handlers_.end()) reply = err("unknown method: " + p.method);
        else {
            try { reply = it->second(ctx, p.body); }
            catch (const std::exception& e) { reply = err(e.what()); }
            catch (...) { reply = err("handler exception"); }
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
        return r;
    };
    handlers_["get_session"] = [](const ControlCtx& c, const json&) {
        if (!c.session || !c.graph) return err("no session");
        json r = ok();
        r["session"] = session_to_json(c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h);
        return r;
    };
    handlers_["list_tracks"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err("no session");
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
        if (!c.session) return err("no session");
        Session* s = c.session;
        const int track = b.value("track", 0), device = b.value("device", 0);
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
        if (!c.graph) return err("no graph");
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
        if (!c.graph) return err("no graph");
        json arr = json::array();
        for (const auto& m : c.graph->mappings())
            arr.push_back({ {"src", m.source}, {"dst", m.dest}, {"amount", m.amount},
                            {"curve", m.curve}, {"invert", m.invert}, {"lo", m.out_lo}, {"hi", m.out_hi} });
        json r = ok(); r["mappings"] = arr; return r;
    };

    // ---------------- visuals construction ----------------
    handlers_["add_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err("no vgraph");
        VOp op; if (!parse_vop(b.value("op", std::string()), op)) return err("bad op (Plasma|Video|Feedback|Blur|Output)");
        const int idx = c.vgraph->add_node(op);
        json r = ok(); r["id"] = c.vgraph->nodes()[idx].id; r["index"] = idx; return r;
    };
    handlers_["remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err("no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err("no node with that id");
        c.vgraph->remove_node(idx);
        return ok();
    };
    handlers_["connect_nodes"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err("no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err("no node_id");
        const int in_id = b.value("input_id", -1);
        const int in_idx = (in_id < 0) ? -1 : op_index_by_id(c.vgraph, in_id);
        c.vgraph->set_input(idx, in_idx);
        return ok();
    };
    handlers_["set_generator"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err("no vgraph");
        VOp op; if (!parse_vop(b.value("op", std::string()), op)) return err("bad op");
        c.vgraph->set_generator(op);
        return ok();
    };
    handlers_["set_active_output"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err("no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err("no node with that id");
        c.vgraph->set_active_output(idx);
        return ok();
    };
    handlers_["set_node_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err("no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err("no node_id");
        const std::string name = b.value("name", std::string());
        int local = -1;
        for (int l = 0; l < c.graph->op_param_count_at(idx); ++l)
            if (name == c.graph->op_param_label_at(idx, l)) { local = l; break; }
        if (local < 0) return err("no param '" + name + "' on that node");
        c.graph->set_op_param_base_at(idx, local, b.value("value", 0.f));
        return ok();
    };
    handlers_["add_data_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err("no graph");
        const std::string src = b.value("source", std::string());
        const int cid = char_id_from_source(src);
        if (cid < 0) return err("bad source (e.g. master.transient, track_2.low)");
        c.graph->add_data_node(src, cid);
        return ok();
    };

    // ---------------- mapping (the bridge) ----------------
    handlers_["connect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err("no graph");
        const std::string src = b.value("src", std::string()), dst = b.value("dst", std::string());
        if (src.empty() || dst.empty()) return err("need src and dst");
        c.graph->add_mapping(src, dst, b.value("amount", 1.0f), b.value("curve", 0.0f),
                             b.value("invert", false), b.value("lo", 0.0f), b.value("hi", 1.0f));
        return ok();
    };
    handlers_["disconnect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err("no graph");
        c.graph->disconnect_dest(b.value("dst", std::string()));
        return ok();
    };

    // ---------------- audio authoring ----------------
    handlers_["set_bpm"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err("no transport");
        c.transport->bpm.store(b.value("bpm", 120.0), std::memory_order_relaxed);
        return ok();
    };
    handlers_["launch_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        P::session_launch_clip(c.session, b.value("track", 0), b.value("scene", 0));
        return ok();
    };
    handlers_["launch_scene"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        P::session_launch_scene(c.session, b.value("scene", 0));
        return ok();
    };
    handlers_["set_track_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        P::session_set_track_gain(c.session, b.value("track", 0), b.value("gain", 0.8f));
        return ok();
    };
    handlers_["set_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        const int track = b.value("track", 0), device = b.value("device", 0), index = b.value("param", 0);
        if (index < 0 || index >= P::session_param_count(c.session, track, device)) return err("param index out of range");
        P::session_set_param(c.session, track, device, P::session_param_id(c.session, track, device, index), b.value("value", 0.f));
        return ok();
    };
    handlers_["set_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        std::vector<P::ClipNote> notes;
        if (b.contains("notes"))
            for (const auto& jn : b["notes"])
                notes.push_back({ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f) });
        P::session_set_clip(c.session, b.value("track", 0), b.value("scene", 0),
                            notes.data(), static_cast<int>(notes.size()), b.value("length", 4.0));
        json r = ok(); r["notes"] = static_cast<int>(notes.size()); return r;
    };
    handlers_["add_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        const std::string name = b.value("name", std::string());
        for (int k = 0; k < P::session_available_effect_count(); ++k)
            if (name == P::session_available_effect_name(k)) {
                const bool okk = P::session_add_effect_by_index(c.session, b.value("track", 0), k);
                return okk ? ok() : err("add failed");
            }
        return err("unknown effect '" + name + "'");
    };
    handlers_["remove_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err("no session");
        P::session_remove_effect(c.session, b.value("track", 0), b.value("effect", 0));
        return ok();
    };
    handlers_["list_effects"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int k = 0; k < P::session_available_effect_count(); ++k) arr.push_back(P::session_available_effect_name(k));
        json r = ok(); r["effects"] = arr; return r;
    };

    // ---------------- session author / persist ----------------
    handlers_["save_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err("no session");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err("need path");
        return save_session(path, c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h) ? ok() : err("write failed");
    };
    handlers_["load_session"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err("no session");
        int ww = *c.win_w, wh = *c.win_h;   // don't resize the window via MCP
        if (b.contains("session")) {        // inline JSON
            return session_from_json(b["session"], c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h) ? ok() : err("load failed");
        }
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err("need path or session");
        return load_session(path, c.session, *c.graph, ww, wh, *c.split_x, *c.dock_h) ? ok() : err("read failed");
    };
}

}  // namespace vivid
