// Headless tests for the session-chain migrations — the pure, Session-free core of loading an
// older document: the legacy VOp-int -> op-name map (pre-P1), the v3 Composite `mode` rescale
// (ADR-0016), and the v5 Light3D aim move (ADR-0051). (A full save/load round-trip needs
// Session/NodeGraph/GPU; see mcp/tests/test_persist_roundtrip.py for that.)
#include "persist.h"
#include "test_helpers.h"
#include <nlohmann/json.hpp>
#include <string>

int main() {
    using vivid::legacy_vop_name;
    CHECK(std::string(legacy_vop_name(0)) == "Plasma");
    CHECK(std::string(legacy_vop_name(1)) == "Video");
    CHECK(std::string(legacy_vop_name(2)) == "Feedback");
    CHECK(std::string(legacy_vop_name(3)) == "Blur");
    CHECK(std::string(legacy_vop_name(4)) == "Output");
    // Out-of-range -> Plasma (safe default, never a bad index).
    CHECK(std::string(legacy_vop_name(5))  == "Plasma");
    CHECK(std::string(legacy_vop_name(-1)) == "Plasma");
    CHECK(std::string(legacy_vop_name(999)) == "Plasma");

    // v3 (ADR-0016 / S5c): Composite's `mode` went from a 0..1 float the shader multiplied by 4
    // to a real enum index. A pre-v3 file's value must keep MEANING the same blend mode.
    using vivid::migrate_param_value;
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 0.00f), 0.f, 1e-6);   // normal
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 0.25f), 1.f, 1e-6);   // add (all 4 demos)
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 0.50f), 2.f, 1e-6);   // multiply
    CHECK_NEAR(migrate_param_value(1, "Composite", "mode", 0.75f), 3.f, 1e-6);   // screen
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 1.00f), 4.f, 1e-6);   // overlay
    // A v3 file is already an index: leave it alone (or every load would rescale it again).
    CHECK_NEAR(migrate_param_value(3, "Composite", "mode", 1.0f), 1.f, 1e-6);
    // Only that one param of that one op is touched.
    CHECK_NEAR(migrate_param_value(2, "Composite", "opacity", 0.25f), 0.25f, 1e-6);
    CHECK_NEAR(migrate_param_value(2, "Displace",  "mode",    0.25f), 0.25f, 1e-6);

    // ---- v5 (ADR-0051 P1): a directional Light3D's aim moved from pos_* to dir_* --------------
    using vivid::migrate_node_params;
    using nlohmann::json;

    // The invariant the migration has to preserve: the renderer's toward-light vector is
    // normalize(pos) before, and -normalize(dir) after. They agree exactly when dir = -pos, so a
    // migrated light points where it always did.
    {
        json p = {{"type", 0.f}, {"pos_x", 3.f}, {"pos_y", 4.f}, {"pos_z", 0.f},
                  {"dir_x", 0.f}, {"dir_y", -1.f}, {"dir_z", 0.f}};
        migrate_node_params(4, "Light3D", p);
        CHECK_NEAR(p["dir_x"].get<float>(), -0.6f, 1e-5);   // -normalize(3,4,0)
        CHECK_NEAR(p["dir_y"].get<float>(), -0.8f, 1e-5);
        CHECK_NEAR(p["dir_z"].get<float>(),  0.0f, 1e-5);
        // The position is meaningless on a directional light — don't leave a stale one behind.
        CHECK_NEAR(p["pos_x"].get<float>(), 0.f, 1e-6);
        CHECK_NEAR(p["pos_y"].get<float>(), 0.f, 1e-6);
        CHECK_NEAR(p["pos_z"].get<float>(), 0.f, 1e-6);
    }

    // An ABSENT type means Directional (the param default), so it must migrate too — this is the
    // shape every shipped demo actually has, since they never wrote a `type` for their key light.
    {
        json p = {{"pos_x", 0.5f}, {"pos_y", 1.f}, {"pos_z", 0.8f}};
        migrate_node_params(4, "Light3D", p);
        // -normalize(0.5, 1, 0.8) — and these must equal Light3D's dir_* DEFAULTS, so a demo that
        // never touched its key light migrates to exactly the light a fresh Light3D would give.
        CHECK_NEAR(p["dir_x"].get<float>(), -0.363696f, 1e-5);
        CHECK_NEAR(p["dir_y"].get<float>(), -0.727393f, 1e-5);
        CHECK_NEAR(p["dir_z"].get<float>(), -0.581914f, 1e-5);
    }

    // Point (1) and Spot (2) already used pos_*/dir_* the new way — migrating them would swing
    // lights that were authored correctly.
    for (float t : {1.f, 2.f}) {
        json p = {{"type", t}, {"pos_x", 3.f}, {"pos_y", 4.f}, {"pos_z", 0.f},
                  {"dir_x", 0.f}, {"dir_y", -1.f}, {"dir_z", 0.f}};
        migrate_node_params(4, "Light3D", p);
        CHECK_NEAR(p["pos_x"].get<float>(),  3.f, 1e-6);
        CHECK_NEAR(p["dir_y"].get<float>(), -1.f, 1e-6);
    }

    // A v5 file is already in the new convention: loading it again must not re-negate the aim.
    {
        json p = {{"type", 0.f}, {"pos_x", 0.f}, {"pos_y", 0.f}, {"pos_z", 0.f},
                  {"dir_x", -0.6f}, {"dir_y", -0.8f}, {"dir_z", 0.f}};
        migrate_node_params(5, "Light3D", p);
        CHECK_NEAR(p["dir_x"].get<float>(), -0.6f, 1e-6);
        CHECK_NEAR(p["dir_y"].get<float>(), -0.8f, 1e-6);
    }

    // A degenerate pre-v5 position had no direction to recover (it rendered unlit), so dir_* is
    // left at whatever the file had rather than being filled with a NaN.
    {
        json p = {{"type", 0.f}, {"pos_x", 0.f}, {"pos_y", 0.f}, {"pos_z", 0.f},
                  {"dir_x", 0.f}, {"dir_y", -1.f}, {"dir_z", 0.f}};
        migrate_node_params(4, "Light3D", p);
        CHECK_NEAR(p["dir_y"].get<float>(), -1.f, 1e-6);
    }

    // Total on junk: a missing/malformed param must not throw or invent values.
    {
        json p = json::object();
        migrate_node_params(4, "Light3D", p);          // no pos_* at all -> uses the old defaults
        CHECK(p.contains("dir_x"));
        json not_an_object = json::array();
        migrate_node_params(4, "Light3D", not_an_object);
        CHECK(not_an_object.empty());
        json other = {{"type", 0.f}, {"pos_x", 3.f}};
        migrate_node_params(4, "Shape3D", other);      // a different op is untouched
        CHECK_NEAR(other["pos_x"].get<float>(), 3.f, 1e-6);
        CHECK(!other.contains("dir_x"));
    }

    return vivid::test::summary("test_persist_chain_migration");
}
