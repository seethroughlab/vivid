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
    // The baseline this build understands.
    CHECK(kSessionSchemaVersion == 1);

    int fv = -1;

    // Equal to the current schema -> read as-is.
    CHECK(classify_session_version(json{{"version", kSessionSchemaVersion}}, &fv) == S::Ok);
    CHECK(fv == kSessionSchemaVersion);

    // Missing "version" -> treated as the v1 baseline (pre-versioned files), so Ok here.
    fv = -1;
    CHECK(classify_session_version(json{{"window", json::object()}}, &fv) == S::Ok);
    CHECK(fv == 1);

    // Older than current -> Migrated (best-effort read; fields are individually optional).
    fv = -1;
    CHECK(classify_session_version(json{{"version", 0}}, &fv) == S::Migrated);
    CHECK(fv == 0);

    // Newer than current -> TooNew (REFUSE: closes the old silent-accept gap).
    fv = -1;
    CHECK(classify_session_version(json{{"version", kSessionSchemaVersion + 1}}, &fv) == S::TooNew);
    CHECK(fv == kSessionSchemaVersion + 1);
    CHECK(classify_session_version(json{{"version", 999}}) == S::TooNew);

    // A non-object document defaults to the baseline rather than throwing.
    CHECK(classify_session_version(json("not-an-object")) == S::Ok);

    return vivid::test::summary("test_version_guard");
}
