#pragma once

#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vivid {

enum class OperatorPrepareStage {
    Idle,
    Scanning,
    LoadingOperators,
    PreparingGraphOperators,
    ReloadingOperators,
    Ready,
};

inline const char* operator_prepare_stage_text(OperatorPrepareStage stage) {
    switch (stage) {
        case OperatorPrepareStage::Scanning: return "Scanning operators...";
        case OperatorPrepareStage::LoadingOperators: return "Loading operators...";
        case OperatorPrepareStage::PreparingGraphOperators: return "Preparing graph operators...";
        case OperatorPrepareStage::ReloadingOperators: return "Reloading operators...";
        case OperatorPrepareStage::Ready: return "Ready";
        case OperatorPrepareStage::Idle: break;
    }
    return "Preparing operators...";
}

struct OperatorReloadSpec {
    std::string type_name;
    std::string dylib_path;
};

struct OperatorPrepareRequest {
    enum class Kind {
        PrepareOperatorType,
        PrepareGraphOperators,
        ReloadPackageOperators,
    };

    Kind kind = Kind::PrepareOperatorType;
    OperatorRegistry* registry = nullptr;
    std::string type_name;
    std::shared_ptr<Graph> graph;
    std::vector<OperatorReloadSpec> reloads;
    bool graph_affecting = false;
};

struct OperatorPrepareResult {
    bool success = false;
    std::string user_message;
    std::string detailed_error;
    std::vector<std::string> prepared_types;
};

class OperatorPreparationService {
public:
    using TaskId = uint64_t;

    OperatorPreparationService() : worker_([this] { worker_loop(); }) {}

    ~OperatorPreparationService() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    TaskId submit(OperatorPrepareRequest request) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!request.registry) return 0;

        cleanup_finished_tasks_locked();

        const std::string key = request_key(request);
        if (!key.empty()) {
            auto it = dedupe_.find(key);
            if (it != dedupe_.end()) {
                if (auto existing = it->second.lock()) {
                    if (!existing->finished.load()) return existing->id;
                }
                dedupe_.erase(it);
            }
        }

        auto task = std::make_shared<TaskState>();
        task->id = next_task_id_++;
        task->request = std::move(request);
        task->stage = OperatorPrepareStage::Idle;
        task->key = key;
        task->graph_affecting = task->request.graph_affecting;
        tasks_[task->id] = task;
        queue_.push_back(task);
        if (!task->key.empty()) dedupe_[task->key] = task;
        if (task->graph_affecting) graph_affecting_in_flight_++;
        cv_.notify_one();
        return task->id;
    }

    OperatorPrepareResult wait(TaskId task_id) {
        std::shared_ptr<TaskState> task = find_task(task_id);
        if (!task) {
            OperatorPrepareResult result;
            result.success = false;
            result.user_message = "unknown operator preparation task";
            result.detailed_error = result.user_message;
            return result;
        }
        std::unique_lock<std::mutex> task_lock(task->result_mutex);
        task->result_cv.wait(task_lock, [&] { return task->finished.load(); });
        return task->result;
    }

    bool peek_completed(TaskId task_id, OperatorPrepareResult& out) const {
        auto task = find_task(task_id);
        if (!task) return false;
        std::lock_guard<std::mutex> task_lock(task->result_mutex);
        if (!task->finished.load()) return false;
        out = task->result;
        return true;
    }

    OperatorPrepareStage task_stage(TaskId task_id) const {
        auto task = find_task(task_id);
        if (!task) return OperatorPrepareStage::Idle;
        std::lock_guard<std::mutex> task_lock(task->result_mutex);
        return task->stage;
    }

    bool has_graph_affecting_task() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return graph_affecting_in_flight_ > 0;
    }

private:
    struct TaskState {
        TaskId id = 0;
        OperatorPrepareRequest request;
        OperatorPrepareResult result;
        OperatorPrepareStage stage = OperatorPrepareStage::Idle;
        std::string key;
        bool graph_affecting = false;
        std::atomic<bool> finished{false};
        mutable std::mutex result_mutex;
        std::condition_variable result_cv;
    };

    static std::string request_key(const OperatorPrepareRequest& request) {
        std::ostringstream oss;
        oss << static_cast<int>(request.kind) << ":" << request.registry << ":";
        switch (request.kind) {
            case OperatorPrepareRequest::Kind::PrepareOperatorType:
                oss << request.type_name;
                break;
            case OperatorPrepareRequest::Kind::PrepareGraphOperators: {
                if (!request.graph) return {};
                std::vector<std::string> types;
                types.reserve(request.graph->nodes().size());
                for (const auto& node : request.graph->nodes())
                    types.push_back(node.type);
                std::sort(types.begin(), types.end());
                types.erase(std::unique(types.begin(), types.end()), types.end());
                for (const auto& type : types)
                    oss << type << ";";
                break;
            }
            case OperatorPrepareRequest::Kind::ReloadPackageOperators: {
                std::vector<std::string> items;
                items.reserve(request.reloads.size());
                for (const auto& reload : request.reloads)
                    items.push_back(reload.type_name + "=" + reload.dylib_path);
                std::sort(items.begin(), items.end());
                for (const auto& item : items)
                    oss << item << ";";
                break;
            }
        }
        return oss.str();
    }

    std::shared_ptr<TaskState> find_task(TaskId task_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) return nullptr;
        return it->second;
    }

    void cleanup_finished_tasks_locked() {
        if (tasks_.size() < 64) return;
        for (auto it = tasks_.begin(); it != tasks_.end();) {
            auto task = it->second;
            if (!task->finished.load()) {
                ++it;
                continue;
            }
            it = tasks_.erase(it);
        }
    }

    static void set_stage(const std::shared_ptr<TaskState>& task, OperatorPrepareStage stage) {
        std::lock_guard<std::mutex> task_lock(task->result_mutex);
        task->stage = stage;
    }

    static void finish_task(const std::shared_ptr<TaskState>& task,
                            bool success,
                            std::string user_message,
                            std::string detailed_error,
                            std::vector<std::string> prepared_types = {}) {
        {
            std::lock_guard<std::mutex> task_lock(task->result_mutex);
            task->stage = OperatorPrepareStage::Ready;
            task->result.success = success;
            task->result.user_message = std::move(user_message);
            task->result.detailed_error = std::move(detailed_error);
            task->result.prepared_types = std::move(prepared_types);
            task->finished.store(true);
        }
        task->result_cv.notify_all();
    }

    static void execute_task(const std::shared_ptr<TaskState>& task) {
        auto& request = task->request;
        if (!request.registry) {
            finish_task(task, false, "missing operator registry",
                        "operator preparation request missing registry");
            return;
        }

        switch (request.kind) {
            case OperatorPrepareRequest::Kind::PrepareOperatorType: {
                set_stage(task, OperatorPrepareStage::LoadingOperators);
                OperatorLoader* loader = request.registry->find(request.type_name);
                if (!loader) {
                    const auto* desc = request.registry->probe_descriptor(request.type_name);
                    if (!desc) {
                        finish_task(task, false,
                                    "unknown operator type '" + request.type_name + "'",
                                    "operator type not found: " + request.type_name);
                    } else {
                        finish_task(task, false,
                                    "failed to load operator '" + request.type_name + "'",
                                    "operator dylib load failed for " + request.type_name);
                    }
                    return;
                }
                finish_task(task, true, {}, {}, {request.type_name});
                return;
            }
            case OperatorPrepareRequest::Kind::PrepareGraphOperators: {
                if (!request.graph) {
                    finish_task(task, false, "missing graph to prepare",
                                "operator preparation request missing graph payload");
                    return;
                }
                set_stage(task, OperatorPrepareStage::PreparingGraphOperators);
                if (!request.registry->load_for_graph(*request.graph)) {
                    std::string graph_name = request.graph->source_path().empty()
                        ? "graph"
                        : request.graph->source_path();
                    finish_task(task, false,
                                "failed to prepare operators for " + graph_name,
                                "registry.load_for_graph failed for " + graph_name);
                    return;
                }
                std::vector<std::string> prepared_types;
                std::unordered_set<std::string> seen;
                for (const auto& node : request.graph->nodes()) {
                    if (seen.insert(node.type).second)
                        prepared_types.push_back(node.type);
                }
                std::sort(prepared_types.begin(), prepared_types.end());
                finish_task(task, true, {}, {}, std::move(prepared_types));
                return;
            }
            case OperatorPrepareRequest::Kind::ReloadPackageOperators: {
                set_stage(task, OperatorPrepareStage::ReloadingOperators);
                std::vector<std::string> prepared_types;
                for (const auto& reload : request.reloads) {
                    if (!request.registry->reload_operator(reload.type_name, reload.dylib_path)) {
                        finish_task(task, false,
                                    "failed to reload operator '" + reload.type_name + "'",
                                    "registry.reload_operator failed for " + reload.type_name +
                                        " from " + reload.dylib_path);
                        return;
                    }
                    prepared_types.push_back(reload.type_name);
                }
                finish_task(task, true, {}, {}, std::move(prepared_types));
                return;
            }
        }
    }

    void worker_loop() {
        while (true) {
            std::shared_ptr<TaskState> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                task = queue_.front();
                queue_.pop_front();
            }

            execute_task(task);

            std::lock_guard<std::mutex> lock(mutex_);
            if (!task->key.empty()) {
                auto it = dedupe_.find(task->key);
                if (it != dedupe_.end()) {
                    auto existing = it->second.lock();
                    if (!existing || existing->id == task->id) dedupe_.erase(it);
                }
            }
            if (task->graph_affecting && graph_affecting_in_flight_ > 0)
                graph_affecting_in_flight_--;
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<TaskId, std::shared_ptr<TaskState>> tasks_;
    std::unordered_map<std::string, std::weak_ptr<TaskState>> dedupe_;
    std::deque<std::shared_ptr<TaskState>> queue_;
    std::thread worker_;
    bool stop_ = false;
    TaskId next_task_id_ = 1;
    size_t graph_affecting_in_flight_ = 0;
};

inline OperatorPreparationService& operator_preparation_service() {
    static OperatorPreparationService service;
    return service;
}

inline OperatorPrepareRequest make_prepare_operator_type_request(
        OperatorRegistry& registry,
        std::string type_name,
        bool graph_affecting = false) {
    OperatorPrepareRequest request;
    request.kind = OperatorPrepareRequest::Kind::PrepareOperatorType;
    request.registry = &registry;
    request.type_name = std::move(type_name);
    request.graph_affecting = graph_affecting;
    return request;
}

inline OperatorPrepareRequest make_prepare_graph_request(
        OperatorRegistry& registry,
        const Graph& graph,
        bool graph_affecting = false) {
    OperatorPrepareRequest request;
    request.kind = OperatorPrepareRequest::Kind::PrepareGraphOperators;
    request.registry = &registry;
    request.graph = std::make_shared<Graph>(graph);
    request.graph_affecting = graph_affecting;
    return request;
}

inline OperatorPrepareRequest make_reload_package_operators_request(
        OperatorRegistry& registry,
        std::vector<OperatorReloadSpec> reloads,
        bool graph_affecting = false) {
    OperatorPrepareRequest request;
    request.kind = OperatorPrepareRequest::Kind::ReloadPackageOperators;
    request.registry = &registry;
    request.reloads = std::move(reloads);
    request.graph_affecting = graph_affecting;
    return request;
}

inline OperatorPrepareResult prepare_operator_type_sync(
        OperatorRegistry& registry,
        const std::string& type_name,
        bool graph_affecting = false) {
    auto id = operator_preparation_service().submit(
        make_prepare_operator_type_request(registry, type_name, graph_affecting));
    return operator_preparation_service().wait(id);
}

inline OperatorPrepareResult prepare_graph_operators_sync(
        OperatorRegistry& registry,
        const Graph& graph,
        bool graph_affecting = false) {
    auto id = operator_preparation_service().submit(
        make_prepare_graph_request(registry, graph, graph_affecting));
    return operator_preparation_service().wait(id);
}

inline OperatorPrepareResult reload_package_operators_sync(
        OperatorRegistry& registry,
        std::vector<OperatorReloadSpec> reloads,
        bool graph_affecting = false) {
    auto id = operator_preparation_service().submit(
        make_reload_package_operators_request(registry, std::move(reloads), graph_affecting));
    return operator_preparation_service().wait(id);
}

} // namespace vivid
