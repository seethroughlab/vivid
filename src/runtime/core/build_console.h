#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vivid {

using BuildTaskId = uint64_t;

enum class BuildTaskKind {
    HotReload,
    PackageBuild,
    PackageConfigure,
    PackageInstall,
    PackageTestCompile,
    PackageTestRun,
    GitClone,
};

enum class BuildConsoleStreamKind {
    Stdout,
    Stderr,
    System,
};

enum class BuildTaskState {
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

enum class BuildConsoleEntryKind {
    TaskStart,
    Line,
    TaskFinish,
};

struct BuildConsoleLine {
    BuildConsoleEntryKind entry_kind = BuildConsoleEntryKind::Line;
    BuildTaskId task_id = 0;
    BuildTaskKind task_kind = BuildTaskKind::PackageBuild;
    BuildConsoleStreamKind stream_kind = BuildConsoleStreamKind::Stdout;
    BuildTaskState task_state = BuildTaskState::Running;
    uint64_t timestamp_ms = 0;
    uint64_t sequence = 0;
    std::string task_label;
    std::string text;
};

struct BuildConsoleSnapshot {
    uint64_t version = 0;
    uint64_t auto_reveal_generation = 0;
    size_t running_task_count = 0;
    std::vector<BuildConsoleLine> lines;
};

class BuildConsole {
public:
    explicit BuildConsole(size_t max_lines = 10000)
        : max_lines_(std::max<size_t>(1, max_lines)) {}

    BuildTaskId begin_task(BuildTaskKind kind, std::string label) {
        std::lock_guard<std::mutex> lock(mutex_);
        const BuildTaskId id = next_task_id_++;
        tasks_[id] = TaskMeta{kind, std::move(label), BuildTaskState::Running};
        running_task_count_ += 1;
        auto_reveal_generation_ += 1;
        append_locked(make_entry_locked(
            BuildConsoleEntryKind::TaskStart, id, tasks_[id].kind,
            BuildConsoleStreamKind::System, BuildTaskState::Running,
            tasks_[id].label, "started"));
        return id;
    }

    void append_line(BuildTaskId task_id, BuildConsoleStreamKind stream_kind, const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) return;
        append_text_locked(task_id, it->second, stream_kind, text);
    }

    void append_system_line(BuildTaskId task_id, const std::string& text) {
        append_line(task_id, BuildConsoleStreamKind::System, text);
    }

    void finish_task(BuildTaskId task_id, BuildTaskState state, const std::string& summary = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) return;
        if (it->second.state == BuildTaskState::Running && running_task_count_ > 0) {
            running_task_count_ -= 1;
        }
        TaskMeta meta = it->second;
        meta.state = state;
        std::string text = summary;
        if (text.empty()) {
            switch (state) {
                case BuildTaskState::Succeeded: text = "succeeded"; break;
                case BuildTaskState::Failed: text = "failed"; break;
                case BuildTaskState::Cancelled: text = "cancelled"; break;
                case BuildTaskState::Running: text = "running"; break;
            }
        }
        append_locked(make_entry_locked(
            BuildConsoleEntryKind::TaskFinish, task_id, meta.kind,
            BuildConsoleStreamKind::System, state, meta.label, text));
        tasks_.erase(it);
    }

    BuildConsoleSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        BuildConsoleSnapshot snap;
        snap.version = version_;
        snap.auto_reveal_generation = auto_reveal_generation_;
        snap.running_task_count = running_task_count_;
        snap.lines.assign(lines_.begin(), lines_.end());
        return snap;
    }

    size_t running_task_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_task_count_;
    }

private:
    struct TaskMeta {
        BuildTaskKind kind = BuildTaskKind::PackageBuild;
        std::string label;
        BuildTaskState state = BuildTaskState::Running;
    };

    static uint64_t now_ms() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    BuildConsoleLine make_entry_locked(BuildConsoleEntryKind entry_kind,
                                       BuildTaskId task_id,
                                       BuildTaskKind task_kind,
                                       BuildConsoleStreamKind stream_kind,
                                       BuildTaskState task_state,
                                       const std::string& task_label,
                                       const std::string& text) {
        BuildConsoleLine line;
        line.entry_kind = entry_kind;
        line.task_id = task_id;
        line.task_kind = task_kind;
        line.stream_kind = stream_kind;
        line.task_state = task_state;
        line.timestamp_ms = now_ms();
        line.sequence = next_sequence_++;
        line.task_label = task_label;
        line.text = text;
        return line;
    }

    void append_text_locked(BuildTaskId task_id,
                            const TaskMeta& task,
                            BuildConsoleStreamKind stream_kind,
                            const std::string& text) {
        size_t start = 0;
        bool emitted = false;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            std::string chunk = (end == std::string::npos)
                ? text.substr(start)
                : text.substr(start, end - start);
            if (!chunk.empty() && chunk.back() == '\r')
                chunk.pop_back();
            if (!chunk.empty()) {
                append_locked(make_entry_locked(
                    BuildConsoleEntryKind::Line, task_id, task.kind, stream_kind,
                    task.state, task.label, chunk));
                emitted = true;
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (!emitted && text.empty()) {
            append_locked(make_entry_locked(
                BuildConsoleEntryKind::Line, task_id, task.kind, stream_kind,
                task.state, task.label, ""));
        }
    }

    void append_locked(BuildConsoleLine line) {
        lines_.push_back(std::move(line));
        version_ += 1;
        while (lines_.size() > max_lines_)
            lines_.pop_front();
    }

    size_t max_lines_ = 10000;
    mutable std::mutex mutex_;
    std::deque<BuildConsoleLine> lines_;
    std::unordered_map<BuildTaskId, TaskMeta> tasks_;
    uint64_t version_ = 0;
    uint64_t auto_reveal_generation_ = 0;
    uint64_t next_task_id_ = 1;
    uint64_t next_sequence_ = 1;
    size_t running_task_count_ = 0;
};

}  // namespace vivid
