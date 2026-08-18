// The control-server plumbing only: the HTTP thread, the per-frame dispatch queue, and
// register_handlers() which now just wires the per-family handler files (audit #7). The handler
// bodies live in control_handlers_*.cpp; shared helpers in control_handlers_internal.h.
#include "cli/control_server.h"
#include "cli/control_handlers.h"   // register_*_handlers()
#include "cli/control_errors.h"     // err/code (the HTTP layer + dispatch report errors)
#include "cli/edit_methods.h"       // ADR-0017/G2: edit_method_info (which methods are undoable)
#include "app/app.h"                // ctx.app->edit_gateway
#include "app/edit_gateway.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using nlohmann::json;

namespace vivid {
namespace {
using control::err;
namespace code = control::code;

// Levenshtein edit distance — small strings (method names), so the simple two-row DP is plenty.
int edit_distance(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

// "Did you mean" suggestions for a mistyped control method: prefer keys that CONTAIN the query as a
// substring (e.g. `connect` -> connect_nodes / connect_mapping / disconnect_mapping), then rank the
// rest by edit distance, dropping anything too far to be a plausible typo. Up to `k` names.
std::vector<std::string> nearest_methods(const Handlers& handlers,
                                         const std::string& q, size_t k = 3) {
    std::vector<std::pair<int, std::string>> scored;
    const int max_dist = static_cast<int>(std::max<size_t>(3, q.size() / 2));
    for (const auto& kv : handlers) {
        const std::string& name = kv.first;
        int score;
        if (name.rfind(q, 0) == 0) score = 0;                    // name STARTS with q (best — `connect`)
        else if (name.find(q) != std::string::npos ||            // q is a substring of name (or vice versa)
                 q.find(name) != std::string::npos) score = 1;
        else {
            const int d = edit_distance(q, name);
            if (d > max_dist) continue;                          // too far to be a plausible typo
            score = 2 + d;
        }
        scored.emplace_back(score, name);
    }
    // ties broken by SHORTER name (closest to the query) then alpha, so `connect` surfaces
    // connect_nodes / connect_mapping ahead of connect_control_to_param.
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        if (a.second.size() != b.second.size()) return a.second.size() < b.second.size();
        return a.second < b.second;
    });
    std::vector<std::string> out;
    for (const auto& s : scored) { if (out.size() >= k) break; out.push_back(s.second); }
    return out;
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
        if (wake_) wake_();   // nudge a possibly-napping main loop to drain this now (not on its next coalesced tick)
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
        if (it == handlers_.end()) {
            std::string msg = "unknown method: " + p.method;
            const auto sugg = nearest_methods(handlers_, p.method);
            if (!sugg.empty()) {
                msg += " — did you mean: ";
                for (size_t i = 0; i < sugg.size(); ++i) { if (i) msg += ", "; msg += sugg[i]; }
                msg += "?";
            }
            reply = err(code::kUnknownMethod, msg);
        }
        else {
            try { reply = it->second(ctx, p.body); }
            catch (const std::exception& e) { reply = err(code::kInternal, e.what()); }
            catch (...) { reply = err(code::kInternal, "handler exception"); }
        }
        // ADR-0017/G2: capture the edit for undo — one place for every MCP mutation. Only on
        // success, and only for methods the edit-method table marks as document edits.
        if (ctx.app && ctx.app->edit_gateway && reply.is_object() && reply.value("ok", false))
            if (const EditMethodInfo* e = edit_method_info(p.method))
                ctx.app->edit_gateway->note_edit(e->label, e->coalesces ? p.method : std::string());
        p.reply.set_value(std::move(reply));
    }
}

void ControlServer::register_handlers() {
    register_introspection_handlers(handlers_);   // ---- status/version/health + discovery ----
    register_visuals_handlers(handlers_);   // ---- visuals construction ----
    register_visual_analysis_handlers(handlers_);   // ---- ADR-0024 Phase 6: visual perception ----
    register_visual_eval_handlers(handlers_);       // ---- reactive-visuals loop: multimodal visual judge ----

    register_mappings_handlers(handlers_);   // ---- mapping (the bridge) ----

    register_audio_handlers(handlers_);   // ---- audio authoring + clip pool + native ops + graph ----

    register_project_handlers(handlers_);   // ---- session author / persist + project workflow ----

    register_package_handlers(handlers_);   // ---- ADR-0024 Phase 7: operator-package authoring ----

    register_edit_handlers(handlers_);   // ---- ADR-0017 undo/redo ----

    register_video_export_handlers(handlers_);   // ---- realtime AV video export ----
    register_audio_export_handlers(handlers_);   // ---- ADR-0032: offline master-mix WAV bounce ----
    register_audio_io_handlers(handlers_);       // ---- ADR-0032 Phase A: output device enumerate/select ----
    register_av_export_handlers(handlers_);      // ---- ADR-0032 Phase C: deterministic offline AV export ----
}

}  // namespace vivid
