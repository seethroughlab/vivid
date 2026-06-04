#include "runtime/core/undo_manager.h"

namespace vivid {

UndoManager::UndoManager(std::size_t maxSnapshots)
    : max_snapshots_(maxSnapshots > 0 ? maxSnapshots : 1) {}

void UndoManager::push(std::string graphJson, bool replaceTop, std::string label) {
    if (snapshots_.empty()) {
        snapshots_.push_back({std::move(graphJson), std::move(label)});
        cursor_ = 0;
        return;
    }

    if (replaceTop && cursor_ == snapshots_.size() - 1) {
        snapshots_.back() = {std::move(graphJson), std::move(label)};
        return;
    }

    // Standard undo/redo behavior: pushing after undo discards redo history.
    if (cursor_ + 1 < snapshots_.size()) {
        snapshots_.erase(snapshots_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1),
                         snapshots_.end());
    }

    snapshots_.push_back({std::move(graphJson), std::move(label)});
    cursor_ = snapshots_.size() - 1;

    while (snapshots_.size() > max_snapshots_) {
        snapshots_.erase(snapshots_.begin());
        if (cursor_ > 0) cursor_--;
    }
}

bool UndoManager::undo(std::string& outGraphJson) {
    if (!canUndo()) return false;
    cursor_--;
    outGraphJson = snapshots_[cursor_].json;
    return true;
}

bool UndoManager::redo(std::string& outGraphJson) {
    if (!canRedo()) return false;
    cursor_++;
    outGraphJson = snapshots_[cursor_].json;
    return true;
}

void UndoManager::clear() {
    snapshots_.clear();
    cursor_ = 0;
}

bool UndoManager::canUndo() const {
    return !snapshots_.empty() && cursor_ > 0;
}

bool UndoManager::canRedo() const {
    return !snapshots_.empty() && cursor_ + 1 < snapshots_.size();
}

const std::string& UndoManager::peekUndoLabel() const {
    static const std::string kEmpty;
    // undo() reverts the action that produced the current top (snapshots_[cursor_]).
    if (!canUndo()) return kEmpty;
    return snapshots_[cursor_].label;
}

const std::string& UndoManager::peekRedoLabel() const {
    static const std::string kEmpty;
    // redo() re-applies the action that produced the next entry.
    if (!canRedo()) return kEmpty;
    return snapshots_[cursor_ + 1].label;
}

std::size_t UndoManager::size() const {
    return snapshots_.size();
}

} // namespace vivid
