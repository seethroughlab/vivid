#include "runtime/core/build_console.h"
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main() {
    std::fprintf(stderr, "\n=== Test: BuildConsole ===\n");

    vivid::BuildConsole console(4);

    auto task_a = console.begin_task(vivid::BuildTaskKind::PackageBuild, "pkg-a");
    console.append_line(task_a, vivid::BuildConsoleStreamKind::Stdout, "first line\nsecond line\n");
    console.finish_task(task_a, vivid::BuildTaskState::Succeeded, "done");

    auto snap = console.snapshot();
    check(snap.auto_reveal_generation == 1, "auto reveal increments on begin_task");
    check(snap.running_task_count == 0, "running task count returns to zero");
    check(snap.lines.size() == 4, "start + 2 output lines + finish recorded");
    if (snap.lines.size() == 4) {
        check(snap.lines[0].entry_kind == vivid::BuildConsoleEntryKind::TaskStart, "first entry is task start");
        check(snap.lines[1].text == "first line", "newline-delimited output is split");
        check(snap.lines[2].text == "second line", "second streamed line captured");
        check(snap.lines[3].entry_kind == vivid::BuildConsoleEntryKind::TaskFinish, "last entry is task finish");
    }

    auto task_b = console.begin_task(vivid::BuildTaskKind::GitClone, "repo-b");
    console.append_system_line(task_b, "cloning");
    console.finish_task(task_b, vivid::BuildTaskState::Failed, "git failed");

    snap = console.snapshot();
    check(snap.lines.size() == 4, "retention trims oldest lines");
    if (snap.lines.size() == 4) {
        check(snap.lines.front().sequence > 1, "oldest lines were trimmed");
        check(snap.lines.back().text == "git failed", "latest finish message retained");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
