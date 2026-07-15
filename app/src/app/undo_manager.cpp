#include "app/undo_manager.h"

namespace vivid {

UndoManager::UndoManager(std::size_t max_snapshots)
    : max_snapshots_(max_snapshots > 0 ? max_snapshots : 1) {}

void UndoManager::push(std::string json, bool replace_top, std::string label) {
    if (snapshots_.empty()) {
        snapshots_.push_back({ std::move(json), std::move(label) });
        cursor_ = 0;
        return;
    }

    // Coalesce: overwrite the current top in place (a rapid same-key edit) instead of appending.
    if (replace_top && cursor_ == snapshots_.size() - 1) {
        snapshots_.back() = { std::move(json), std::move(label) };
        return;
    }

    // A new edit after an undo discards the orphaned redo tail.
    if (cursor_ + 1 < snapshots_.size())
        snapshots_.erase(snapshots_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1), snapshots_.end());

    snapshots_.push_back({ std::move(json), std::move(label) });
    cursor_ = snapshots_.size() - 1;

    // Evict from the front past the depth cap, keeping the cursor on the same entry.
    while (snapshots_.size() > max_snapshots_) {
        snapshots_.erase(snapshots_.begin());
        if (cursor_ > 0) --cursor_;
    }
}

bool UndoManager::undo(std::string& out_json) {
    if (!can_undo()) return false;
    --cursor_;
    out_json = snapshots_[cursor_].json;
    return true;
}

bool UndoManager::redo(std::string& out_json) {
    if (!can_redo()) return false;
    ++cursor_;
    out_json = snapshots_[cursor_].json;
    return true;
}

void UndoManager::clear() {
    snapshots_.clear();
    cursor_ = 0;
}

bool UndoManager::can_undo() const { return !snapshots_.empty() && cursor_ > 0; }
bool UndoManager::can_redo() const { return !snapshots_.empty() && cursor_ + 1 < snapshots_.size(); }

const std::string& UndoManager::undo_label() const {
    static const std::string kEmpty;
    // undo() reverts the action that produced the current cursor entry.
    return can_undo() ? snapshots_[cursor_].label : kEmpty;
}

const std::string& UndoManager::redo_label() const {
    static const std::string kEmpty;
    // redo() re-applies the action that produced the next entry.
    return can_redo() ? snapshots_[cursor_ + 1].label : kEmpty;
}

}  // namespace vivid
