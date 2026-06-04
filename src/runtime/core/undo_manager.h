#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vivid {

class UndoManager {
public:
    explicit UndoManager(std::size_t maxSnapshots = 100);

    // label is a human-readable description of the action that produced this state
    // (e.g. "Clear pattern"). It rides along with the snapshot so undo/redo surfaces
    // can show "Undo Clear pattern". Optional and last so existing callers are
    // unaffected.
    void push(std::string graphJson, bool replaceTop = false, std::string label = {});

    bool undo(std::string& outGraphJson);
    bool redo(std::string& outGraphJson);

    void clear();

    bool canUndo() const;
    bool canRedo() const;

    // Label of the action that an undo() would revert (the current top), or that a
    // redo() would re-apply (the next entry). Empty when not available.
    const std::string& peekUndoLabel() const;
    const std::string& peekRedoLabel() const;

    std::size_t size() const;
    std::size_t maxSnapshots() const { return max_snapshots_; }

private:
    struct Entry {
        std::string json;
        std::string label;
    };
    std::vector<Entry> snapshots_;
    std::size_t max_snapshots_ = 100;
    std::size_t cursor_ = 0;
};

} // namespace vivid
