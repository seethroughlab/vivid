// Ph4 audit P1-01: a Full-tier undo/redo frees plugin instances; any floated plugin-editor window
// still holding a raw handle into one would dangle → use-after-free. The fix closes EVERY open editor
// (via close_plugin_editor_windows → close_editor_pool) before the teardown, unlike the per-frame reap
// which only closes windows already self-reporting closed. This test guards the load-bearing property
// of that helper — close AND null every open slot, skip empty ones — headlessly, with a fake window
// type + a recording closer (the real close_plugin_editor_windows needs the GUI/plugin stack).
#include "app/plugin_editor_pool.h"
#include "test_helpers.h"

#include <vector>

int main() {
    struct FakeWin { int id; };
    std::vector<int> closed;
    auto closer = [&closed](FakeWin* w) { closed.push_back(w->id); };

    FakeWin a{1}, b{2}, c{3};
    // A sparse pool like the real editor pools: some open (non-null), some empty (null).
    FakeWin* pool[5] = { &a, nullptr, &b, nullptr, &c };

    vivid::close_editor_pool(pool, closer);

    // Every open editor was closed exactly once, in slot order...
    CHECK(closed.size() == 3);
    CHECK(closed[0] == 1 && closed[1] == 2 && closed[2] == 3);
    // ...and EVERY slot is now null — the property that prevents a dangling handle on teardown.
    for (FakeWin* p : pool) CHECK(p == nullptr);

    // Idempotent: a second pass finds nothing open and closes nothing more.
    vivid::close_editor_pool(pool, closer);
    CHECK(closed.size() == 3);

    // An all-empty pool is a safe no-op.
    FakeWin* empty[3] = { nullptr, nullptr, nullptr };
    vivid::close_editor_pool(empty, closer);
    CHECK(closed.size() == 3);

    return vivid::test::summary("test_plugin_editor_pool");
}
