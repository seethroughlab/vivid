// Ph4 audit P1-02: a visual-graph node whose op_type isn't registered (missing/uninstalled package)
// must not lose the user's tuned params on a save-back. persist.cpp preserves the raw params/
// file_params/pinned via capture_orphan_payload (at load) + apply_orphan_payload (at save). These
// are pure JSON transforms, so this test guards the exact round-trip without the GPU/NodeGraph stack.
#include "persist_orphan.h"
#include "test_helpers.h"

using nlohmann::json;

int main() {
    // A degraded chain node: op_type not registered, but carrying tuned params, a file param, a pin.
    const json entry = {
        {"op_type", "SomeUninstalledOp"}, {"id", 7}, {"in", -1},
        {"params", { {"gain", 0.75}, {"mix", 0.30} }},
        {"file_params", { {"image", "/path/x.png"} }},
        {"pinned", json::array({ "gain" })},
    };

    // Load side: capture preserves exactly the three orphan keys.
    const json orphan = vivid::capture_orphan_payload(entry);
    CHECK(orphan.contains("params") && orphan["params"]["gain"] == 0.75 && orphan["params"]["mix"] == 0.30);
    CHECK(orphan.contains("file_params") && orphan["file_params"]["image"] == "/path/x.png");
    CHECK(orphan.contains("pinned") && orphan["pinned"].size() == 1);
    CHECK(!orphan.contains("op_type") && !orphan.contains("id"));   // only param-keyed data, nothing else

    // Save side: a still-missing node re-serializes with empty params (what the zero-param live loop
    // produces); applying the orphan splices the real values back — the loss the fix prevents.
    json node_out = { {"op_type", "SomeUninstalledOp"}, {"id", 7}, {"params", json::object()} };
    vivid::apply_orphan_payload(node_out, orphan);
    CHECK(node_out["params"]["gain"] == 0.75 && node_out["params"]["mix"] == 0.30);
    CHECK(node_out["file_params"]["image"] == "/path/x.png");
    CHECK(node_out["pinned"].size() == 1);

    // Round-trip identity: re-capturing the saved node yields the same payload (lossless across saves).
    CHECK(vivid::capture_orphan_payload(node_out) == orphan);

    // A node with no param-keyed data yields an empty payload — nothing preserved, no spurious keys.
    CHECK(vivid::capture_orphan_payload(json{ {"op_type", "X"}, {"id", 1} }).empty());
    // apply of an empty/non-object payload is a no-op.
    json untouched = { {"params", json::object()} };
    vivid::apply_orphan_payload(untouched, json::object());
    CHECK(untouched["params"].empty());

    return vivid::test::summary("test_persist_orphan");
}
