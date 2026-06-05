#include "runtime/control/control_server_internal.h"

namespace vivid {

namespace {

struct BuildTaskSummary {
    BuildTaskId task_id = 0;
    BuildTaskKind kind = BuildTaskKind::PackageBuild;
    BuildTaskState state = BuildTaskState::Running;
    std::string label;
    std::string summary;
    uint64_t last_timestamp_ms = 0;
    uint64_t last_sequence = 0;
    std::vector<BuildConsoleLine> lines;
};

static const char* build_task_kind_str(BuildTaskKind kind) {
    switch (kind) {
        case BuildTaskKind::HotReload: return "hot_reload";
        case BuildTaskKind::PackageBuild: return "package_build";
        case BuildTaskKind::PackageConfigure: return "package_configure";
        case BuildTaskKind::PackageInstall: return "package_install";
        case BuildTaskKind::PackageTestCompile: return "package_test_compile";
        case BuildTaskKind::PackageTestRun: return "package_test_run";
        case BuildTaskKind::GitClone: return "git_clone";
        default: return "unknown";
    }
}

static const char* build_stream_kind_str(BuildConsoleStreamKind kind) {
    switch (kind) {
        case BuildConsoleStreamKind::Stdout: return "stdout";
        case BuildConsoleStreamKind::Stderr: return "stderr";
        case BuildConsoleStreamKind::System: return "system";
        default: return "unknown";
    }
}

static const char* build_task_state_str(BuildTaskState state) {
    switch (state) {
        case BuildTaskState::Running: return "running";
        case BuildTaskState::Succeeded: return "succeeded";
        case BuildTaskState::Failed: return "failed";
        case BuildTaskState::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

static const char* build_entry_kind_str(BuildConsoleEntryKind kind) {
    switch (kind) {
        case BuildConsoleEntryKind::TaskStart: return "task_start";
        case BuildConsoleEntryKind::Line: return "line";
        case BuildConsoleEntryKind::TaskFinish: return "task_finish";
        default: return "unknown";
    }
}

static std::vector<std::string> json_string_array(const nlohmann::json& root, const char* key) {
    std::vector<std::string> values;
    if (!root.contains(key) || !root[key].is_array()) return values;
    for (const auto& item : root[key]) {
        if (item.is_string())
            values.push_back(item.get<std::string>());
    }
    return values;
}

static std::vector<BuildTaskSummary> collect_build_task_summaries(BuildConsole* build_console) {
    std::vector<BuildTaskSummary> tasks;
    if (!build_console) return tasks;

    auto snapshot = build_console->snapshot();
    std::unordered_map<BuildTaskId, std::size_t> index_by_id;
    for (const auto& line : snapshot.lines) {
        auto it = index_by_id.find(line.task_id);
        if (it == index_by_id.end()) {
            BuildTaskSummary summary;
            summary.task_id = line.task_id;
            summary.kind = line.task_kind;
            summary.state = line.task_state;
            summary.label = line.task_label;
            summary.summary = line.text;
            summary.last_timestamp_ms = line.timestamp_ms;
            summary.last_sequence = line.sequence;
            summary.lines.push_back(line);
            index_by_id[line.task_id] = tasks.size();
            tasks.push_back(std::move(summary));
        } else {
            auto& summary = tasks[it->second];
            summary.kind = line.task_kind;
            summary.state = line.task_state;
            summary.label = line.task_label;
            summary.summary = line.text;
            summary.last_timestamp_ms = line.timestamp_ms;
            summary.last_sequence = line.sequence;
            summary.lines.push_back(line);
        }
    }

    std::sort(tasks.begin(), tasks.end(),
              [](const BuildTaskSummary& a, const BuildTaskSummary& b) {
                  return a.last_sequence > b.last_sequence;
              });
    return tasks;
}

static nlohmann::json build_console_line_json(const BuildConsoleLine& line) {
    return nlohmann::json{
        {"task_id", line.task_id},
        {"entry_kind", build_entry_kind_str(line.entry_kind)},
        {"task_kind", build_task_kind_str(line.task_kind)},
        {"stream_kind", build_stream_kind_str(line.stream_kind)},
        {"task_state", build_task_state_str(line.task_state)},
        {"timestamp_ms", line.timestamp_ms},
        {"sequence", line.sequence},
        {"task_label", line.task_label},
        {"text", line.text},
    };
}

static nlohmann::json top_error_lines_json(const BuildTaskSummary& task, std::size_t max_lines) {
    nlohmann::json lines = nlohmann::json::array();
    std::unordered_set<std::string> seen;
    for (const auto& line : task.lines) {
        const std::string lower = line.text;
        const std::string lower_copy = [&]() {
            std::string tmp = lower;
            std::transform(tmp.begin(), tmp.end(), tmp.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return tmp;
        }();
        // Include "missing required build tool" hints (from the package
        // manager) alongside conventional compiler/linker error markers. The
        // friendly hint doesn't contain the words error/failed/fatal, so the
        // keyword filter would otherwise drop it.
        const bool looks_error =
            line.stream_kind == BuildConsoleStreamKind::Stderr ||
            lower_copy.find("error") != std::string::npos ||
            lower_copy.find("failed") != std::string::npos ||
            lower_copy.find("fatal") != std::string::npos ||
            lower_copy.find("missing required build tool") != std::string::npos;
        if (!looks_error) continue;
        if (!seen.insert(line.text).second) continue;
        lines.push_back(build_console_line_json(line));
        if (lines.size() >= max_lines) break;
    }
    return lines;
}

// Scan a task for the package manager's missing_tool hint line. When found,
// extract the tool name (e.g. "cmake") and the full hint string so the
// explainer can surface a structured remediation object.
struct MissingToolRemediation {
    bool present = false;
    std::string tool;
    std::string hint;
};

static MissingToolRemediation detect_missing_tool_remediation(const BuildTaskSummary& task) {
    MissingToolRemediation out;
    static const std::string kPrefix = "Missing required build tool: ";
    for (const auto& line : task.lines) {
        auto pos = line.text.find(kPrefix);
        if (pos == std::string::npos) continue;
        out.present = true;
        out.hint = line.text;
        std::string tail = line.text.substr(pos + kPrefix.size());
        // Tool name ends at the first '.' in the hint.
        auto dot = tail.find('.');
        out.tool = (dot == std::string::npos) ? tail : tail.substr(0, dot);
        break;
    }
    return out;
}

} // namespace

std::string handle_list_source_roots(SourceIndex& source_index) {
    return nlohmann::json{{"ok", true}, {"roots", source_index.list_roots()}}.dump();
}

std::string handle_search_source(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("query") || !root["query"].is_string())
        return json_err("missing 'query'");
    auto result = source_index.search(root["query"].get<std::string>(),
                                      json_string_array(root, "roots"),
                                      root.value("limit", 20),
                                      json_string_array(root, "file_types"),
                                      json_string_array(root, "path_globs"));
    return result.dump();
}

std::string handle_read_source_file(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("path") || !root["path"].is_string())
        return json_err("missing 'path'");
    auto result = source_index.read_file(root["path"].get<std::string>(),
                                         root.value("max_bytes", 200000));
    return result.dump();
}

std::string handle_read_source_span(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("path") || !root["path"].is_string())
        return json_err("missing 'path'");
    if (!root.contains("start_line") || !root["start_line"].is_number_integer() ||
        !root.contains("end_line") || !root["end_line"].is_number_integer())
        return json_err("missing 'start_line' or 'end_line'");
    auto result = source_index.read_span(root["path"].get<std::string>(),
                                         root["start_line"].get<int>(),
                                         root["end_line"].get<int>());
    return result.dump();
}

std::string handle_find_symbol(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");
    auto result = source_index.find_symbol(root["name"].get<std::string>(),
                                           json_string_array(root, "roots"),
                                           root.value("limit", 20));
    return result.dump();
}

std::string handle_find_references(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");
    auto result = source_index.find_references(root["name"].get<std::string>(),
                                               json_string_array(root, "roots"),
                                               root.value("limit", 50));
    return result.dump();
}

std::string handle_get_build_activity(BuildConsole* build_console, const nlohmann::json& root) {
    if (!build_console)
        return json_err("build console not available");

    auto snapshot = build_console->snapshot();
    auto tasks = collect_build_task_summaries(build_console);
    const std::string scope = root.value("scope", std::string("recent"));
    const std::size_t limit = std::max(1, std::min(root.value("limit", 10), 50));

    nlohmann::json tasks_arr = nlohmann::json::array();
    for (const auto& task : tasks) {
        if (scope == "active" && task.state != BuildTaskState::Running)
            continue;
        nlohmann::json entry = {
            {"task_id", task.task_id},
            {"kind", build_task_kind_str(task.kind)},
            {"label", task.label},
            {"state", build_task_state_str(task.state)},
            {"summary", task.summary},
            {"last_timestamp_ms", task.last_timestamp_ms},
            {"line_count", task.lines.size()},
            {"top_error_lines", top_error_lines_json(task, 3)},
        };
        nlohmann::json recent_lines = nlohmann::json::array();
        const std::size_t start = task.lines.size() > 6 ? task.lines.size() - 6 : 0;
        for (std::size_t i = start; i < task.lines.size(); ++i)
            recent_lines.push_back(build_console_line_json(task.lines[i]));
        entry["recent_lines"] = std::move(recent_lines);
        tasks_arr.push_back(std::move(entry));
        if (tasks_arr.size() >= limit)
            break;
    }

    return nlohmann::json{
        {"ok", true},
        {"scope", scope},
        {"version", snapshot.version},
        {"running_task_count", snapshot.running_task_count},
        {"tasks", std::move(tasks_arr)},
    }.dump();
}

std::string handle_explain_build_failure(BuildConsole* build_console, const nlohmann::json& root) {
    if (!build_console)
        return json_err("build console not available");

    auto tasks = collect_build_task_summaries(build_console);
    if (tasks.empty())
        return json_err("no build activity available");

    BuildTaskId requested_id = 0;
    if (root.contains("task_id")) {
        if (root["task_id"].is_number_unsigned())
            requested_id = root["task_id"].get<BuildTaskId>();
        else if (root["task_id"].is_number_integer())
            requested_id = static_cast<BuildTaskId>(root["task_id"].get<int64_t>());
        else if (root["task_id"].is_string()) {
            const std::string task_id = root["task_id"].get<std::string>();
            if (task_id != "latest")
                return json_err("task_id must be a number or 'latest'");
        }
    }

    const BuildTaskSummary* chosen = nullptr;
    if (requested_id != 0) {
        for (const auto& task : tasks) {
            if (task.task_id == requested_id) {
                chosen = &task;
                break;
            }
        }
        if (!chosen)
            return json_err("build task not found");
    } else {
        for (const auto& task : tasks) {
            if (task.state == BuildTaskState::Failed) {
                chosen = &task;
                break;
            }
        }
        if (!chosen)
            return json_err("no failed build task found");
    }

    const std::size_t max_lines = std::max(5, std::min(root.value("max_lines", 40), 200));
    nlohmann::json excerpt = nlohmann::json::array();
    const std::size_t start = chosen->lines.size() > max_lines ? chosen->lines.size() - max_lines : 0;
    for (std::size_t i = start; i < chosen->lines.size(); ++i)
        excerpt.push_back(build_console_line_json(chosen->lines[i]));

    std::ostringstream joined;
    for (std::size_t i = start; i < chosen->lines.size(); ++i) {
        if (i > start) joined << '\n';
        joined << chosen->lines[i].text;
    }

    auto result = nlohmann::json{
        {"ok", true},
        {"task", {
            {"task_id", chosen->task_id},
            {"kind", build_task_kind_str(chosen->kind)},
            {"label", chosen->label},
            {"state", build_task_state_str(chosen->state)},
            {"summary", chosen->summary},
            {"last_timestamp_ms", chosen->last_timestamp_ms},
        }},
        {"top_error_lines", top_error_lines_json(*chosen, 8)},
        {"lines", std::move(excerpt)},
        {"output_excerpt", joined.str()},
    };

    auto remediation = detect_missing_tool_remediation(*chosen);
    if (remediation.present) {
        result["remediation"] = nlohmann::json{
            {"kind", "install_tool"},
            {"tool", remediation.tool},
            {"hint", remediation.hint},
        };
    }

    return result.dump();
}

} // namespace vivid
