#pragma once

#include <cstddef>
#include <string>
#include <vector>

// ADR-0017 — the undo history: a cursor over a list of labeled document snapshots. Ported from
// vivid-classic's src/runtime/core/undo_manager (snake_case to match this tree). Pure data — no App,
// no GPU — so it is headless-testable. The snapshots are canonical-document-projection JSON strings
// (see persist.h); this class neither produces nor interprets them, it only stores the history.
//
// Model: entry 0 is the BASELINE (the state before the first edit); push() appends the POST-edit
// state and advances the cursor; undo() moves the cursor back one and returns the earlier state;
// redo() moves it forward. So can_undo() is "cursor > 0" and the label at the cursor describes the
// action an undo() would revert.
namespace vivid {

class UndoManager {
public:
    explicit UndoManager(std::size_t max_snapshots = 200);

    // Append the post-edit state with a human label ("Delete Node"). replace_top overwrites the
    // current cursor entry instead of appending — the coalescing path (a rapid same-key edit).
    // Pushing after an undo discards the now-orphaned redo history.
    void push(std::string json, bool replace_top = false, std::string label = {});

    bool undo(std::string& out_json);
    bool redo(std::string& out_json);

    void clear();

    bool can_undo() const;
    bool can_redo() const;

    // Label of the action an undo() would revert (the cursor entry), or a redo() would re-apply
    // (the next entry). Empty when the corresponding move isn't available.
    const std::string& undo_label() const;
    const std::string& redo_label() const;

    std::size_t size() const { return snapshots_.size(); }
    std::size_t max_snapshots() const { return max_snapshots_; }

private:
    struct Entry {
        std::string json;
        std::string label;
    };
    std::vector<Entry> snapshots_;
    std::size_t        max_snapshots_ = 200;
    std::size_t        cursor_ = 0;
};

}  // namespace vivid
