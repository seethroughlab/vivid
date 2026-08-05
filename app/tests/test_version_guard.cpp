// Headless test for the session-version load guard (P4.1). classify_session_version is
// a pure header function, so this covers the accept / migrate / refuse policy without
// pulling in Session/NodeGraph/GPU. (The full load round-trip needs those.)
#include "persist.h"
#include "test_helpers.h"

using vivid::classify_session_version;
using vivid::kSessionSchemaVersion;
using S = vivid::SessionVersionStatus;
using json = nlohmann::json;

int main() {
    // The baseline this build understands. Pinned deliberately: bumping it means old files get
    // MIGRATED, so the bump and the migration have to be written together.
    //   v2 the dynamic-track set · v3 Composite's `mode` became an enum index (ADR-0016 / S5c)
    //   v4 graph sticky notes + per-node labels (ADR-0033 P5) — purely additive, read back with defaults
    CHECK(kSessionSchemaVersion == 4);

    int fv = -1;

    // Equal to the current schema -> read as-is.
    CHECK(classify_session_version(json{{"version", kSessionSchemaVersion}}, &fv) == S::Ok);
    CHECK(fv == kSessionSchemaVersion);

    // Missing "version" -> treated as the v1 baseline (pre-versioned files); older than the
    // current schema, so Migrated (restored onto the pre-built role set).
    fv = -1;
    CHECK(classify_session_version(json{{"window", json::object()}}, &fv) == S::Migrated);
    CHECK(fv == 1);

    // An explicit older version -> Migrated (best-effort read; fields are individually optional).
    fv = -1;
    CHECK(classify_session_version(json{{"version", 1}}, &fv) == S::Migrated);
    CHECK(fv == 1);

    // Newer than current -> TooNew (REFUSE: closes the old silent-accept gap).
    fv = -1;
    CHECK(classify_session_version(json{{"version", kSessionSchemaVersion + 1}}, &fv) == S::TooNew);
    CHECK(fv == kSessionSchemaVersion + 1);
    CHECK(classify_session_version(json{{"version", 999}}) == S::TooNew);

    // A non-object document defaults to the v1 baseline rather than throwing.
    CHECK(classify_session_version(json("not-an-object")) == S::Migrated);

    return vivid::test::summary("test_version_guard");
}
