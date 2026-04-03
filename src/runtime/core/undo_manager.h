#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vivid {

class UndoManager {
public:
    explicit UndoManager(std::size_t maxSnapshots = 100);

    void push(std::string graphJson, bool replaceTop = false);

    bool undo(std::string& outGraphJson);
    bool redo(std::string& outGraphJson);

    void clear();

    bool canUndo() const;
    bool canRedo() const;

    std::size_t size() const;
    std::size_t maxSnapshots() const { return max_snapshots_; }

private:
    std::vector<std::string> snapshots_;
    std::size_t max_snapshots_ = 100;
    std::size_t cursor_ = 0;
};

} // namespace vivid
