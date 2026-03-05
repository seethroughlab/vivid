// test_package_update_logic.cpp — PackageManager version/compatibility classification
#include "runtime/package_manager.h"
#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    using namespace vivid;

    PackageInfo installed;
    installed.name = "demo-package";
    installed.version = "1.2.3";

    {
        auto r = PackageManager::assess_update(installed, "1.3.0", ">=0.1.0 <2.0.0", "0.9.0");
        check(r.update_available, "newer remote version detected");
        check(r.compatible, "compatible update classified as compatible");
        check(r.classification == PackageUpdateClass::CompatibleUpdate, "classification: compatible update");
    }

    {
        auto r = PackageManager::assess_update(installed, "1.3.0", ">=2.0.0 <3.0.0", "0.9.0");
        check(r.update_available, "newer remote still detected");
        check(!r.compatible, "incompatible constraint classified");
        check(r.classification == PackageUpdateClass::IncompatibleUpdate, "classification: incompatible update");
    }

    {
        auto r = PackageManager::assess_update(installed, "1.2.3", ">=0.1.0 <2.0.0", "0.9.0");
        check(!r.update_available, "equal version is not update");
        check(r.classification == PackageUpdateClass::UpToDate, "classification: up to date");
    }

    {
        auto r = PackageManager::assess_update(installed, "1.2.2", ">=0.1.0 <2.0.0", "0.9.0");
        check(!r.update_available, "older remote is not update");
        check(r.classification == PackageUpdateClass::RemoteOlderOrEqual, "classification: remote older");
    }

    {
        auto r = PackageManager::assess_update(installed, "1.3.0", ">=abc", "0.9.0");
        check(r.classification == PackageUpdateClass::InvalidVersionData, "invalid vivid_core constraint classified");
        check(!r.constraint_valid, "invalid constraint flagged");
    }

    {
        auto r = PackageManager::assess_update(installed, "not-semver", ">=0.1.0 <2.0.0", "0.9.0");
        check(r.classification == PackageUpdateClass::InvalidVersionData, "invalid remote semver classified");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
