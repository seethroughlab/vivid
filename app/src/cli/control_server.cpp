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

#include <chrono>
#include <cstdio>
#include <string>

using nlohmann::json;

namespace vivid {
namespace {
using control::err;
namespace code = control::code;
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

    register_mappings_handlers(handlers_);   // ---- mapping (the bridge) ----

    register_audio_handlers(handlers_);   // ---- audio authoring + clip pool + native ops + graph ----

    register_project_handlers(handlers_);   // ---- session author / persist + project workflow ----

    register_edit_handlers(handlers_);   // ---- ADR-0017 undo/redo ----
}

}  // namespace vivid
