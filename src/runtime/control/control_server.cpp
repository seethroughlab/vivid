#include "runtime/control/control_server_internal.h"

namespace vivid {

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
    std::mutex queue_mutex;
    std::deque<PendingRequest> queue;
    std::atomic<bool> running{false};
    vivid::UndoManager undo_history{200};

    Impl(int port) : server(port, "127.0.0.1") {}
};

// ---------------------------------------------------------------------------
// ControlServer lifecycle
// ---------------------------------------------------------------------------

ControlServer::ControlServer()
    : operator_source_docs_(std::make_unique<OperatorSourceDocs>()) {}
ControlServer::~ControlServer() { stop(); }

void ControlServer::set_src_dir(const std::string& src_dir) {
    src_dir_ = src_dir;
    if (operator_source_docs_)
        operator_source_docs_->set_core_source_root(src_dir_);
}
void ControlServer::set_hot_reloader(HotReloader* hr) { hot_reloader_ = hr; }
void ControlServer::set_capture_coordinator(CaptureCoordinator* cc) { assert(!impl_); capture_coordinator_ = cc; }
void ControlServer::set_package_manager(PackageManager* pm) { assert(!impl_); package_manager_ = pm; }
void ControlServer::set_package_compiler(PackageCompiler* pc) { assert(!impl_); package_compiler_ = pc; }
void ControlServer::set_package_catalog(PackageCatalog* cat) { assert(!impl_); package_catalog_ = cat; }
void ControlServer::set_app_update_manager(AppUpdateManager* aum) { assert(!impl_); app_update_manager_ = aum; }
void ControlServer::set_settings(Settings* settings) { assert(!impl_); settings_ = settings; }
void ControlServer::set_audio_engine(AudioEngine* ae) { audio_engine_ = ae; }

uint64_t ControlServer::mcp_last_ping_ms(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mcp_ping_mutex_);
    auto it = mcp_last_ping_ms_.find(name);
    return (it != mcp_last_ping_ms_.end()) ? it->second : 0;
}

int ControlServer::port() const {
    return impl_ ? impl_->server.getPort() : 0;
}

bool ControlServer::start(int port) {
    impl_ = std::make_unique<Impl>(port);

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
                auto entries = package_catalog_->entries();
                nlohmann::json resp = nlohmann::json::object();
                resp["ok"] = true;
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& e : entries) {
                    nlohmann::json obj = nlohmann::json::object();
                    obj["name"] = e.name;
                    obj["description"] = e.description;
                    obj["version"] = e.version;
                    if (!e.vivid_core.empty()) obj["vivid_core"] = e.vivid_core;
                    obj["author"] = e.author;
                    obj["url"] = e.url;
                    if (!e.category.empty()) obj["category"] = e.category;
                    if (!e.description_short.empty()) obj["description_short"] = e.description_short;
                    if (!e.status.empty()) obj["status"] = e.status;
                    if (!e.status_note.empty()) obj["status_note"] = e.status_note;
                    if (!e.preview_image_url.empty()) obj["preview_image_url"] = e.preview_image_url;
                    if (!e.repo_url.empty()) obj["repo_url"] = e.repo_url;
                    if (!e.homepage_url.empty()) obj["homepage_url"] = e.homepage_url;
                    if (!e.install_url.empty()) obj["install_url"] = e.install_url;
                    obj["installed"] = e.installed;
                    if (e.installed) obj["installed_version"] = e.installed_version;
                    arr.push_back(std::move(obj));
                }
                resp["packages"] = std::move(arr);
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    resp.dump());
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

                auto entries = package_catalog_->entries();
                nlohmann::json resp = nlohmann::json::object();
                resp["ok"] = true;
                resp["core_version"] = core_version;

                nlohmann::json updates = nlohmann::json::array();
                int64_t update_count = 0;
                int64_t incompatible_count = 0;
                for (const auto& e : entries) {
                    if (!e.installed) continue;

                    PackageInfo installed;
                    installed.name = e.name;
                    installed.version = e.installed_version;
                    auto assessment = PackageManager::assess_update(
                        installed, e.version, e.vivid_core, core_version);

                    if (!include_all_installed && !assessment.update_available) continue;

                    nlohmann::json obj = nlohmann::json::object();
                    obj["name"] = assessment.package_name;
                    obj["installed_version"] = assessment.installed_version;
                    obj["remote_version"] = assessment.remote_version;
                    if (!assessment.remote_vivid_core.empty())
                        obj["vivid_core"] = assessment.remote_vivid_core;
                    obj["update_available"] = assessment.update_available;
                    obj["compatible"] = assessment.compatible;
                    obj["constraint_valid"] = assessment.constraint_valid;
                    obj["classification"] = update_class_str(assessment.classification);
                    obj["message"] = assessment.message;

                    if (assessment.update_available) update_count++;
                    if (assessment.classification == PackageUpdateClass::IncompatibleUpdate)
                        incompatible_count++;

                    updates.push_back(std::move(obj));
                }
                resp["updates_available"] = update_count;
                resp["incompatible_updates"] = incompatible_count;
                resp["packages"] = std::move(updates);

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    resp.dump());
            }

            // Check core app updates using appcast metadata.
            if (method == "check_core_updates") {
                if (!app_update_manager_) {
                    return std::make_shared<ix::HttpResponse>(
                        200, "OK", ix::HttpErrorCode::Ok,
                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                        R"({"ok":false,"error":"core update manager unavailable"})");
                }
                bool force_refresh = false;
                if (!request->body.empty()) {
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("force_refresh") && doc["force_refresh"].is_boolean())
                            force_refresh = doc["force_refresh"].get<bool>();
                    } catch (...) {}
                }
                if (force_refresh) app_update_manager_->refresh();
                if (app_update_manager_->fetch_state() == AppUpdateFetchState::Idle)
                    app_update_manager_->refresh();
                for (int i = 0; i < 200; ++i) {
                    auto st = app_update_manager_->fetch_state();
                    if (st != AppUpdateFetchState::Fetching) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                nlohmann::json resp = nlohmann::json::object();
                resp["ok"] = true;

                const auto st = app_update_manager_->fetch_state();
                switch (st) {
                    case AppUpdateFetchState::Idle:     resp["state"] = "idle"; break;
                    case AppUpdateFetchState::Fetching: resp["state"] = "fetching"; break;
                    case AppUpdateFetchState::Ready:    resp["state"] = "ready"; break;
                    case AppUpdateFetchState::Error:    resp["state"] = "error"; break;
                }

                auto info = app_update_manager_->latest();
                resp["update_available"] = info.update_available;
                resp["current_version"] = info.current_version;
                resp["latest_version"] = info.latest_version;
                resp["download_url"] = info.download_url;
                resp["release_notes_url"] = info.release_notes_url;
                resp["title"] = info.title;
                resp["publication_date"] = info.publication_date;
                resp["minimum_system_version"] = info.minimum_system_version;
                resp["appcast_url"] = AppUpdateManager::appcast_url();
                if (st == AppUpdateFetchState::Error) {
                    resp["error"] = app_update_manager_->fetch_error();
                }

                std::string response_body = resp.dump();
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
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
    impl_->running = true;
    std::fprintf(stderr,
        "[vivid] Control server listening on http://127.0.0.1:%d\n", port);
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

    // Swap the queue out under lock, then process without holding it
    std::deque<Impl::PendingRequest> local;
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        if (impl_->queue.empty()) return;
        local.swap(impl_->queue);
    }

    for (auto& req : local) {
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
                                        package_manager_,
                                        package_compiler_,
                                        settings_,
                                        audio_engine_);

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
