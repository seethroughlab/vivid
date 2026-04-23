#include "runtime/control/control_server_internal.h"
#include <ixwebsocket/IXGetFreePort.h>
#include <chrono>

namespace vivid {

namespace {

struct ControlServerActiveSampleRequest {
    std::string node_id;
    std::string type;
    std::string kind;
    std::string active_cadence;
    double duration_seconds = 0.0;
    int interval_ms = 250;
    bool include_lanes = true;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    std::chrono::steady_clock::time_point next_sample;
    int sample_count = 0;
    nlohmann::json samples = nlohmann::json::array();
    std::promise<std::string> promise;
};

} // namespace

// ---------------------------------------------------------------------------
// Pimpl
// ---------------------------------------------------------------------------

struct ControlServer::Impl {
    struct PendingRequest {
        std::string method;
        std::string body;
        std::promise<std::string> promise;
    };

    ix::HttpServer server;
    int bound_port = 0;
    std::mutex queue_mutex;
    std::deque<PendingRequest> queue;
    std::deque<ControlServerActiveSampleRequest> active_samples;
    std::atomic<bool> running{false};
    vivid::UndoManager undo_history{200};

    Impl(int port) : server(port, "127.0.0.1") {}
};

// ---------------------------------------------------------------------------
// ControlServer lifecycle
// ---------------------------------------------------------------------------

ControlServer::ControlServer()
    : operator_source_docs_(std::make_unique<OperatorSourceDocs>())
    , source_index_(std::make_unique<SourceIndex>()) {}
ControlServer::~ControlServer() { stop(); }

namespace {

const CompiledNode* control_find_node_state(const RuntimeCore& core,
                                            const std::string& node_id) {
    const auto* cg = core.compiled_graph();
    if (!cg) return nullptr;
    return cg->find_node(node_id);
}

void append_sample_snapshot(ControlServerActiveSampleRequest& sample_req,
                            RuntimeCore& core,
                            std::chrono::steady_clock::time_point now) {
    const CompiledNode* ns = control_find_node_state(core, sample_req.node_id);
    if (!ns) return;

    nlohmann::json sample = nlohmann::json::object();
    sample["time_seconds"] = std::chrono::duration<double>(now - sample_req.start).count();
    sample["outputs"] = sample_node_outputs_snapshot(*ns, sample_req.include_lanes);
    if (ns->audio) {
        sample["audio_debug"] = make_audio_node_debug_json(*ns);
    }
    sample_req.samples.push_back(std::move(sample));
    sample_req.sample_count++;
}

std::string finish_sample_request(ControlServerActiveSampleRequest& sample_req) {
    nlohmann::json result = nlohmann::json::object();
    result["node_id"] = sample_req.node_id;
    result["type"] = sample_req.type;
    result["kind"] = sample_req.kind;
    result["active_cadence"] = sample_req.active_cadence;
    result["duration_seconds"] = sample_req.duration_seconds;
    result["interval_ms"] = sample_req.interval_ms;
    result["include_lanes"] = sample_req.include_lanes;
    result["sample_count"] = sample_req.sample_count;
    result["samples"] = std::move(sample_req.samples);
    return json_ok(std::move(result));
}

bool begin_sample_request(const std::string& body,
                          RuntimeCore& core,
                          ControlServerActiveSampleRequest& out,
                          std::string& immediate_response) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(body);
    } catch (...) {
        immediate_response = json_err("invalid JSON body");
        return false;
    }

    if (!root.contains("node_id") || !root["node_id"].is_string()) {
        immediate_response = json_err("missing 'node_id'");
        return false;
    }

    const std::string node_id = root["node_id"].get<std::string>();
    const CompiledNode* initial = control_find_node_state(core, node_id);
    if (!initial) {
        immediate_response = json_err("node not found");
        return false;
    }
    if (!initial->loader || !initial->loader->descriptor()) {
        immediate_response = json_err("node has no live descriptor");
        return false;
    }

    double duration_seconds = 8.0;
    int interval_ms = 250;
    bool include_lanes = true;

    if (root.contains("duration_seconds") && root["duration_seconds"].is_number()) {
        duration_seconds = root["duration_seconds"].get<double>();
    }
    if (root.contains("interval_ms") && root["interval_ms"].is_number()) {
        interval_ms = root["interval_ms"].get<int>();
    }
    if (root.contains("include_lanes") && root["include_lanes"].is_boolean()) {
        include_lanes = root["include_lanes"].get<bool>();
    }

    duration_seconds = std::clamp(duration_seconds, 0.0, 60.0);
    interval_ms = std::clamp(interval_ms, 10, 5000);

    const auto now = std::chrono::steady_clock::now();
    out.node_id = node_id;
    out.type = initial->type_name;
    out.kind = kind_str(initial->operator_kind);
    out.active_cadence =
        (initial->active_cadence == vivid::Cadence::Audio) ? "audio" : "frame";
    out.duration_seconds = duration_seconds;
    out.interval_ms = interval_ms;
    out.include_lanes = include_lanes;
    out.start = now;
    out.end = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(duration_seconds));
    out.next_sample = now;
    return true;
}

void service_active_samples(std::deque<ControlServerActiveSampleRequest>& active_samples,
                            RuntimeCore& core) {
    if (active_samples.empty()) return;

    const auto now = std::chrono::steady_clock::now();
    for (auto it = active_samples.begin(); it != active_samples.end();) {
        if (now >= it->next_sample) {
            append_sample_snapshot(*it, core, now);
            it->next_sample = now + std::chrono::milliseconds(it->interval_ms);
        }

        if (now >= it->end) {
            it->promise.set_value(finish_sample_request(*it));
            it = active_samples.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

void ControlServer::set_src_dir(const std::string& src_dir) {
    src_dir_ = src_dir;
    if (operator_source_docs_)
        operator_source_docs_->set_core_source_root(src_dir_);
    if (source_index_)
        source_index_->set_checkout_root(src_dir_);
}
void ControlServer::set_hot_reloader(HotReloader* hr) { hot_reloader_ = hr; }
void ControlServer::set_capture_coordinator(CaptureCoordinator* cc) { assert(!impl_); capture_coordinator_ = cc; }
void ControlServer::set_package_manager(PackageManager* pm) { assert(!impl_); package_manager_ = pm; }
void ControlServer::set_package_compiler(PackageCompiler* pc) { assert(!impl_); package_compiler_ = pc; }
void ControlServer::set_package_catalog(PackageCatalog* cat) { assert(!impl_); package_catalog_ = cat; }
void ControlServer::set_app_update_manager(AppUpdateManager* aum) { assert(!impl_); app_update_manager_ = aum; }
void ControlServer::set_settings(Settings* settings) { assert(!impl_); settings_ = settings; }
void ControlServer::set_audio_engine(AudioEngine* ae) { audio_engine_ = ae; }
void ControlServer::set_asset_library(AssetLibrary* lib) { asset_library_ = lib; }
void ControlServer::set_build_console(BuildConsole* console) { assert(!impl_); build_console_ = console; }
void ControlServer::set_gpu_context(GpuContext* ctx) { gpu_context_ = ctx; }
void ControlServer::set_crash_recovery_manager(CrashRecoveryManager* crm) { crash_recovery_manager_ = crm; }
void ControlServer::set_editor_window_manager(EditorWindowManager* m) { editor_window_manager_ = m; }
void ControlServer::set_bundled_source_dir(const std::string& bundled_source_dir) {
    bundled_source_dir_ = bundled_source_dir;
    if (source_index_)
        source_index_->set_bundled_root(bundled_source_dir_);
}

uint64_t ControlServer::mcp_last_ping_ms(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mcp_ping_mutex_);
    auto it = mcp_last_ping_ms_.find(name);
    return (it != mcp_last_ping_ms_.end()) ? it->second : 0;
}

int ControlServer::port() const {
    return impl_ ? impl_->bound_port : 0;
}

bool ControlServer::start(int port) {
    const int listen_port = (port == 0) ? ix::getFreePort() : port;
    impl_ = std::make_unique<Impl>(listen_port);

    impl_->server.setOnConnectionCallback(
        [this](ix::HttpRequestPtr request,
               std::shared_ptr<ix::ConnectionState>) -> ix::HttpResponsePtr
        {
            // Reject non-POST
            if (request->method != "POST") {
                return std::make_shared<ix::HttpResponse>(
                    405, "Method Not Allowed", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    R"({"ok":false,"error":"use POST"})");
            }

            // Reject if shutting down
            if (!impl_->running) {
                return std::make_shared<ix::HttpResponse>(
                    503, "Shutting Down", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    R"({"ok":false,"error":"server shutting down"})");
            }

            // Method name is the URI path without leading /
            std::string method = request->uri;
            if (!method.empty() && method[0] == '/')
                method = method.substr(1);

            // MCP heartbeat ping — immediate, no main-thread dispatch needed
            if (method == "mcp_ping") {
                std::string server_name;
                try {
                    auto pdoc = nlohmann::json::parse(request->body);
                    if (pdoc.contains("server") && pdoc["server"].is_string())
                        server_name = pdoc["server"].get<std::string>();
                } catch (...) {}
                if (!server_name.empty()) {
                    auto now_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    std::lock_guard<std::mutex> lk(mcp_ping_mutex_);
                    mcp_last_ping_ms_[server_name] = now_ms;
                }
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    R"({"ok":true})");
            }

            // Recording tap start/stop (immediate, no main-thread dispatch needed)
            if (capture_coordinator_ &&
                (method == "start_recording_tap" || method == "stop_recording_tap")) {
                std::string response_body = (method == "start_recording_tap")
                    ? capture_coordinator_->handle_start_recording_tap()
                    : capture_coordinator_->handle_stop_recording_tap();
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Recording start/stop — routed through coordinator's pending queue (needs main thread)
            if (capture_coordinator_ &&
                (method == "start_recording" || method == "stop_recording")) {
                std::future<std::string> future;
                if (method == "start_recording") {
                    std::string path = "/tmp/vivid_recording.mov";
                    double fps = 60.0;
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("path") && doc["path"].is_string()) {
                            std::string candidate = doc["path"].get<std::string>();
                            if (!is_safe_recording_path(candidate)) {
                                return std::make_shared<ix::HttpResponse>(
                                    200, "OK", ix::HttpErrorCode::Ok,
                                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                                    R"({"ok":false,"error":"invalid recording path"})");
                            }
                            path = candidate;
                        }
                        if (doc.contains("fps") && doc["fps"].is_number())
                            fps = doc["fps"].get<double>();
                    } catch (...) {}
                    future = capture_coordinator_->request_start_recording(path, fps);
                } else {
                    future = capture_coordinator_->request_stop_recording();
                }

                auto status = future.wait_for(std::chrono::seconds(kRecordingTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Capture methods bypass normal dispatch — route to CaptureCoordinator
            if (capture_coordinator_ &&
                (method == "capture_frame" || method == "capture_audio" ||
                 method == "capture_av" || method == "capture_interface")) {
                CaptureType ctype = CaptureType::Frame;
                float audio_dur = 1.0f;
                std::string node_id;
                std::string save_path;
                bool ensure_ui_visible = true;
                if (method == "capture_audio") ctype = CaptureType::Audio;
                else if (method == "capture_av") ctype = CaptureType::AV;

                // Parse optional duration from body
                if (ctype == CaptureType::Audio || ctype == CaptureType::AV || method == "capture_interface") {
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("duration") && doc["duration"].is_number())
                            audio_dur = doc["duration"].get<float>();
                        if (method == "capture_interface") {
                            if (doc.contains("node_id") && doc["node_id"].is_string())
                                node_id = doc["node_id"].get<std::string>();
                            if (doc.contains("save_path") && doc["save_path"].is_string()) {
                                std::string candidate = doc["save_path"].get<std::string>();
                                if (!candidate.empty() && !is_safe_capture_image_path(candidate)) {
                                    return std::make_shared<ix::HttpResponse>(
                                        200, "OK", ix::HttpErrorCode::Ok,
                                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                                        R"({"ok":false,"error":"invalid save_path"})");
                                }
                                save_path = candidate;
                            }
                            if (doc.contains("ensure_ui_visible") && doc["ensure_ui_visible"].is_boolean())
                                ensure_ui_visible = doc["ensure_ui_visible"].get<bool>();
                        }
                    } catch (...) {}
                }

                std::future<std::string> future;
                if (method == "capture_interface") {
                    future = capture_coordinator_->request_interface_capture(
                        node_id, save_path, ensure_ui_visible);
                } else {
                    future = capture_coordinator_->request_capture(ctype, audio_dur);
                }
                auto status = future.wait_for(std::chrono::seconds(method == "capture_interface" ? kInterfaceCaptureTimeoutSec : kCaptureTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Analysis endpoints — route through CaptureCoordinator
            if (capture_coordinator_ &&
                (method == "analyze_output" || method == "compare_outputs")) {

                // Parse request JSON
                AnalysisMode amode = AnalysisMode::Frame;
                float window_seconds = 1.0f;
                float window_a = 1.0f, window_b = 1.0f;
                bool include_payload = false;
                std::string node_id;
                bool parse_ok = true;

                try {
                    auto doc = nlohmann::json::parse(request->body);
                    if (doc.contains("mode") && doc["mode"].is_string()) {
                        std::string mode_str = doc["mode"].get<std::string>();
                        if (mode_str == "audio") amode = AnalysisMode::Audio;
                        else if (mode_str == "av") amode = AnalysisMode::AV;
                        else if (mode_str == "frame") amode = AnalysisMode::Frame;
                        else parse_ok = false;
                    }

                    if (doc.contains("window_seconds") && doc["window_seconds"].is_number())
                        window_seconds = doc["window_seconds"].get<float>();

                    if (doc.contains("include_payload") && doc["include_payload"].is_boolean())
                        include_payload = doc["include_payload"].get<bool>();

                    if (doc.contains("node_id") && doc["node_id"].is_string())
                        node_id = doc["node_id"].get<std::string>();

                    // For compare_outputs, parse a/b sub-objects
                    if (method == "compare_outputs") {
                        if (doc.contains("a") && doc["a"].is_object()) {
                            const auto& a_v = doc["a"];
                            if (a_v.contains("window_seconds") && a_v["window_seconds"].is_number())
                                window_a = a_v["window_seconds"].get<float>();
                        }
                        if (doc.contains("b") && doc["b"].is_object()) {
                            const auto& b_v = doc["b"];
                            if (b_v.contains("window_seconds") && b_v["window_seconds"].is_number())
                                window_b = b_v["window_seconds"].get<float>();
                        }
                    }
                } catch (...) {}

                if (!parse_ok) {
                    return std::make_shared<ix::HttpResponse>(
                        200, "OK", ix::HttpErrorCode::Ok,
                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                        R"json({"ok":false,"error":"invalid mode (expected 'frame', 'audio', or 'av')"})json");
                }

                std::future<std::string> future;
                if (method == "analyze_output")
                    future = capture_coordinator_->request_analyze(amode, window_seconds, include_payload, node_id);
                else
                    future = capture_coordinator_->request_compare(amode, window_a, window_b, include_payload, node_id);

                auto status = future.wait_for(std::chrono::seconds(kAnalysisTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Package catalog — thread-safe, no main-thread dispatch needed
            if (method == "package_catalog" && package_catalog_) {
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    handle_package_catalog(package_catalog_));
            }

            // Check package updates using catalog metadata + installed package versions
            if (method == "check_package_updates" && package_catalog_ && package_manager_) {
                std::string core_version = "0.1.0";
                bool include_all_installed = false;
                if (!request->body.empty()) {
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("core_version") && doc["core_version"].is_string())
                            core_version = doc["core_version"].get<std::string>();
                        if (doc.contains("include_all_installed") && doc["include_all_installed"].is_boolean())
                            include_all_installed = doc["include_all_installed"].get<bool>();
                    } catch (...) {}
                }

                nlohmann::json root = {
                    {"core_version", core_version},
                    {"include_all_installed", include_all_installed},
                };

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    handle_check_package_updates(package_catalog_, package_manager_, root));
            }

            // Check core app updates using appcast metadata.
            if (method == "check_core_updates") {
                if (!app_update_manager_) {
                    return std::make_shared<ix::HttpResponse>(
                        200, "OK", ix::HttpErrorCode::Ok,
                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                        R"({"ok":false,"error":"core update manager unavailable"})");
                }
                nlohmann::json root = nlohmann::json::object();
                if (!request->body.empty()) {
                    try { root = nlohmann::json::parse(request->body); }
                    catch (...) { root = nlohmann::json::object(); }
                }
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    handle_check_core_updates(app_update_manager_, root));
            }

            // test_package needs longer timeout (compiles + runs tests)
            if (method == "test_package") {
                Impl::PendingRequest req;
                req.method = std::move(method);
                req.body = request->body;
                auto future = req.promise.get_future();
                {
                    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                    impl_->queue.push_back(std::move(req));
                }
                auto status = future.wait_for(std::chrono::seconds(kTestPackageTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Push request to queue, block until main thread processes it
            const bool is_sample_node_outputs = (method == "sample_node_outputs");

            Impl::PendingRequest req;
            req.method = std::move(method);
            req.body = request->body;
            auto future = req.promise.get_future();

            {
                std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                impl_->queue.push_back(std::move(req));
            }

            const int timeout_seconds =
                is_sample_node_outputs ? kSampleNodeOutputsTimeoutSec : kDefaultDispatchTimeoutSec;
            auto status = future.wait_for(std::chrono::seconds(timeout_seconds));
            std::string response_body;
            if (status == std::future_status::ready)
                response_body = future.get();
            else
                response_body = R"({"ok":false,"error":"timeout"})";

            return std::make_shared<ix::HttpResponse>(
                200, "OK", ix::HttpErrorCode::Ok,
                ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                response_body);
        }
    );

    auto res = impl_->server.listen();
    if (!res.first) {
        std::fprintf(stderr, "[vivid] Control server failed to listen: %s\n",
                     res.second.c_str());
        impl_.reset();
        return false;
    }

    impl_->server.start();
    impl_->bound_port = listen_port;
    impl_->running = true;
    std::fprintf(stderr,
        "[vivid] Control server listening on http://127.0.0.1:%d\n", listen_port);
    return true;
}

void ControlServer::stop() {
    if (!impl_ || !impl_->running) return;
    impl_->running = false;

    // Drain queue so blocked handler threads can unblock and finish
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        while (!impl_->queue.empty()) {
            impl_->queue.front().promise.set_value(
                R"({"ok":false,"error":"server shutting down"})");
            impl_->queue.pop_front();
        }
    }

    impl_->server.stop();
    std::fprintf(stderr, "[vivid] Control server stopped\n");
}

void ControlServer::process_requests(RuntimeAPI& api, Graph& graph,
                                     RuntimeCore& core,
                                     OperatorRegistry& registry,
                                     bool& has_gpu_ops, bool& has_audio) {
    if (!impl_) return;

    service_active_samples(impl_->active_samples, core);

    // Swap the queue out under lock, then process without holding it
    std::deque<Impl::PendingRequest> local;
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        if (impl_->queue.empty()) return;
        local.swap(impl_->queue);
    }

    for (auto& req : local) {
        if (req.method == "sample_node_outputs") {
            ControlServerActiveSampleRequest sample_req;
            std::string immediate_response;
            if (begin_sample_request(req.body,
                                     core,
                                     sample_req,
                                     immediate_response)) {
                sample_req.promise = std::move(req.promise);
                impl_->active_samples.push_back(std::move(sample_req));
                service_active_samples(impl_->active_samples, core);
            } else {
                req.promise.set_value(std::move(immediate_response));
            }
            continue;
        }

        // MCP undo/redo are handled here so they can use control-server history.
        if (req.method == "undo") {
            std::string snapshot_json;
            if (!impl_->undo_history.undo(snapshot_json)) {
                req.promise.set_value(json_err("nothing to undo"));
                continue;
            }
            auto r = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
            if (!r.ok) {
                std::string ignored;
                (void)impl_->undo_history.redo(ignored);
                impl_->undo_history.clear();
                std::string baseline_json;
                if (graph.save_to_string(baseline_json)) {
                    impl_->undo_history.push(std::move(baseline_json));
                }
                req.promise.set_value(json_err("undo failed: " + r.message));
                continue;
            }
            req.promise.set_value(command_result_to_json(r));
            continue;
        }
        if (req.method == "new_graph") {
            auto r = api.new_graph(has_gpu_ops, has_audio);
            impl_->undo_history.clear();
            req.promise.set_value(command_result_to_json(r));
            continue;
        }
        if (req.method == "new_project") {
            nlohmann::json np_root;
            bool np_valid = false;
            try { np_root = nlohmann::json::parse(req.body); np_valid = true; } catch (...) {}
            if (!np_valid || !np_root.contains("path") || !np_root["path"].is_string()) {
                req.promise.set_value(json_err("new_project requires 'path' parameter"));
                continue;
            }
            std::string path = np_root["path"].get<std::string>();
            auto r = api.new_project(path, has_gpu_ops, has_audio);
            impl_->undo_history.clear();
            req.promise.set_value(command_result_to_json(r));
            continue;
        }
        if (req.method == "redo") {
            std::string snapshot_json;
            if (!impl_->undo_history.redo(snapshot_json)) {
                req.promise.set_value(json_err("nothing to redo"));
                continue;
            }
            auto r = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
            if (!r.ok) {
                std::string ignored;
                (void)impl_->undo_history.undo(ignored);
                impl_->undo_history.clear();
                std::string baseline_json;
                if (graph.save_to_string(baseline_json)) {
                    impl_->undo_history.push(std::move(baseline_json));
                }
                req.promise.set_value(json_err("redo failed: " + r.message));
                continue;
            }
            req.promise.set_value(command_result_to_json(r));
            continue;
        }

        bool track_for_undo = is_undo_tracked_method(req.method);
        if (track_for_undo && impl_->undo_history.size() == 0) {
            std::string baseline_json;
            if (graph.save_to_string(baseline_json)) {
                impl_->undo_history.push(std::move(baseline_json));
            }
        }

        std::string response = dispatch(req.method, req.body,
                                        api, graph, core, registry,
                                        has_gpu_ops, has_audio,
                                        hot_reloader_,
                                        src_dir_,
                                        *operator_source_docs_,
                                        *source_index_,
                                        package_manager_,
                                        package_compiler_,
                                        settings_,
                                        audio_engine_,
                                        asset_library_,
                                        build_console_,
                                        gpu_context_,
                                        package_catalog_,
                                        this,
                                        crash_recovery_manager_,
                                        editor_window_manager_);

        if (track_for_undo && response_is_ok(response)) {
            if (req.method == "load_graph") {
                impl_->undo_history.clear();
            }
            std::string current_json;
            if (graph.save_to_string(current_json)) {
                impl_->undo_history.push(std::move(current_json));
            }
        }

        req.promise.set_value(std::move(response));
    }
}

} // namespace vivid
