#include "runtime/control/control_server_internal.h"

#include <cstdio>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "\n=== Test: Build activity query handlers ===\n");

    vivid::BuildConsole console;
    auto build_task = console.begin_task(vivid::BuildTaskKind::PackageBuild, "demo-package");
    console.append_line(build_task, vivid::BuildConsoleStreamKind::Stdout, "Compiling demo.cpp");
    console.append_line(build_task, vivid::BuildConsoleStreamKind::Stderr, "demo.cpp:12: error: broken");
    console.finish_task(build_task, vivid::BuildTaskState::Failed, "compile failed");

    auto test_task = console.begin_task(vivid::BuildTaskKind::PackageTestRun, "demo-tests");
    console.append_system_line(test_task, "running tests");

    auto activity = nlohmann::json::parse(vivid::handle_get_build_activity(
        &console, nlohmann::json{{"scope", "recent"}, {"limit", 5}}));
    check(activity.value("ok", false), "get_build_activity succeeds");
    if (activity.value("ok", false)) {
        check(activity["tasks"].is_array() && activity["tasks"].size() >= 2,
              "get_build_activity returns grouped tasks");
        if (activity["tasks"].is_array() && !activity["tasks"].empty()) {
            check(activity["tasks"][0].contains("top_error_lines"),
                  "get_build_activity exposes top_error_lines");
        }
    }

    auto active_only = nlohmann::json::parse(vivid::handle_get_build_activity(
        &console, nlohmann::json{{"scope", "active"}, {"limit", 5}}));
    check(active_only.value("ok", false), "active scope succeeds");
    if (active_only.value("ok", false)) {
        check(active_only["tasks"].is_array() && active_only["tasks"].size() == 1,
              "active scope returns only running tasks");
    }

    auto failure = nlohmann::json::parse(vivid::handle_explain_build_failure(
        &console, nlohmann::json{{"task_id", "latest"}, {"max_lines", 10}}));
    check(failure.value("ok", false), "explain_build_failure succeeds");
    if (failure.value("ok", false)) {
        check(failure["task"].value("task_id", 0) == build_task,
              "latest failure resolves the failed build task");
        check(failure["output_excerpt"].get<std::string>().find("error: broken") != std::string::npos,
              "failure explanation includes output excerpt");
        check(failure["top_error_lines"].is_array() && !failure["top_error_lines"].empty(),
              "failure explanation includes top error lines");
    }

    auto missing = nlohmann::json::parse(vivid::handle_explain_build_failure(
        nullptr, nlohmann::json::object()));
    check(!missing.value("ok", true), "build failure handler reports missing console");

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
