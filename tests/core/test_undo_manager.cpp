#include "runtime/core/undo_manager.h"
#include <cstdio>
#include <string>
#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "--- test_undo_manager ---\n");

    {
        vivid::UndoManager undo(3);
        std::string out;

        check(!undo.canUndo(), "empty manager cannot undo");
        check(!undo.canRedo(), "empty manager cannot redo");
        check(undo.size() == 0, "empty manager has size 0");

        undo.push("{\"v\":1}");
        check(undo.size() == 1, "push adds initial snapshot");
        check(!undo.canUndo(), "single snapshot cannot undo");
        check(!undo.canRedo(), "single snapshot cannot redo");

        undo.push("{\"v\":2}");
        check(undo.size() == 2, "second push increases size");
        check(undo.canUndo(), "two snapshots can undo");
        check(!undo.canRedo(), "at top of history cannot redo");

        check(undo.undo(out), "undo succeeds");
        check(out == "{\"v\":1}", "undo returns previous snapshot");
        check(!undo.canUndo(), "after undo to first snapshot cannot undo further");
        check(undo.canRedo(), "after undo redo is available");

        check(undo.redo(out), "redo succeeds");
        check(out == "{\"v\":2}", "redo restores snapshot");
        check(undo.canUndo(), "after redo can undo");
    }

    {
        vivid::UndoManager undo(3);
        std::string out;
        undo.push("{\"v\":1}");
        undo.push("{\"v\":2}");
        undo.push("{\"v\":2.1}", true);
        check(undo.size() == 2, "replace_top keeps history size unchanged at top");
        check(undo.undo(out), "undo succeeds after replace_top at top");
        check(out == "{\"v\":1}", "undo target unchanged when top was replaced");
        check(undo.redo(out), "redo succeeds after replace_top at top");
        check(out == "{\"v\":2.1}", "replace_top updates top snapshot");
    }

    {
        vivid::UndoManager undo(3);
        std::string out;
        undo.push("{\"v\":1}");
        undo.push("{\"v\":2}");
        undo.push("{\"v\":3}");
        check(undo.undo(out), "undo before branch succeeds");
        check(out == "{\"v\":2}", "undo moved to v2");
        check(undo.canRedo(), "redo exists before branch push");

        undo.push("{\"v\":2b}", true);
        check(!undo.canRedo(), "branch push clears redo stack");
        check(undo.size() == 3, "branch push keeps expected size");
    }

    {
        vivid::UndoManager undo(3);
        std::string out;
        undo.push("{\"v\":1}");
        undo.push("{\"v\":2}");
        undo.push("{\"v\":3}");
        undo.push("{\"v\":4}");

        check(undo.size() == 3, "max history depth enforced");
        check(undo.undo(out), "undo 1 after overflow succeeds");
        check(out == "{\"v\":3}", "first undo after overflow gives v3");
        check(undo.undo(out), "undo 2 after overflow succeeds");
        check(out == "{\"v\":2}", "second undo after overflow gives v2");
        check(!undo.undo(out), "cannot undo past oldest retained snapshot");
    }

    {
        vivid::UndoManager undo(2);
        undo.push("{\"v\":1}");
        undo.push("{\"v\":2}");
        undo.clear();
        check(undo.size() == 0, "clear removes all snapshots");
        check(!undo.canUndo(), "clear resets undo availability");
        check(!undo.canRedo(), "clear resets redo availability");
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
