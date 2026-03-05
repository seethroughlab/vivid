# Undo/Redo Design (Milestone 1 Item 8)

## Scope

This doc defines the first implementation pass for snapshot-based graph undo/redo in Vivid.

## Public API

Target location: `src/runtime/undo_manager.h` and `src/runtime/undo_manager.cpp`.

```cpp
class UndoManager {
public:
    explicit UndoManager(size_t maxSnapshots = 100);

    // Pushes a new snapshot. Clears redo history when pushing after undo.
    // If replaceTop is true, replaces current top snapshot (drag coalescing).
    void push(std::string graphJson, bool replaceTop = false);

    // Moves history cursor backward/forward and returns the target snapshot.
    // Returns false if no action is available.
    bool undo(std::string& outGraphJson);
    bool redo(std::string& outGraphJson);

    // Clears all history.
    void clear();

    bool canUndo() const;
    bool canRedo() const;

    size_t size() const;
    size_t maxSnapshots() const;
};
```

## Snapshot Granularity

- Unit of history: full graph JSON document (`Graph::save()` output).
- Undo/redo apply path: `Graph::load()` with the selected snapshot JSON.
- Coalescing:
  - Continuous param edits and node drags: replace top snapshot if same edit key and delta < 300 ms.
  - Topology edits (add/remove node/wire, paste, delete): always push a new snapshot.

## Max History Depth

- Default: 100 snapshots.
- Behavior when full: drop oldest snapshot (ring-buffer semantics).
- Make depth configurable later via settings, but hard-code `100` for first pass.

## Integration Notes

- `RuntimeCommandSink` should own `UndoManager` and push after each successful mutation.
- File load should `clear()` and push loaded graph as baseline state.
- Any new mutation after one or more `undo()` calls clears redo history.
- Initial implementation is keyboard-only (`Cmd+Z`, `Cmd+Shift+Z`), then toolbar/MCP wiring.
