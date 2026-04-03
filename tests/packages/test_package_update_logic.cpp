// test_package_update_logic.cpp — PackageManager version/compatibility classification
#include "runtime/packages/package_manager.h"
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
        check(!r.update_available, "reinstall same version is not an update");
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

    {
        auto r = PackageManager::assess_update(installed, "", ">=0.1.0 <2.0.0", "0.9.0");
        check(r.classification == PackageUpdateClass::InvalidVersionData,
              "empty remote version classified as invalid");
    }

    {
        PackageInfo missing_installed = installed;
        missing_installed.version.clear();
        auto r = PackageManager::assess_update(missing_installed, "1.3.0", ">=0.1.0 <2.0.0", "0.9.0");
        check(r.classification == PackageUpdateClass::InvalidVersionData,
              "missing installed version classified as invalid");
    }

    // M2 regression: version component that overflows std::stoi must not throw.
    {
        auto r = PackageManager::assess_update(installed, "99999999999.0.0", ">=0.1.0 <2.0.0", "0.9.0");
        check(r.classification == PackageUpdateClass::InvalidVersionData,
              "overflow version component classified as invalid (no exception)");
    }

    {
        PackageInfo overflow_installed = installed;
        overflow_installed.version = "99999999999.0.0";
        auto r = PackageManager::assess_update(overflow_installed, "1.3.0", ">=0.1.0 <2.0.0", "0.9.0");
        check(r.classification == PackageUpdateClass::InvalidVersionData,
              "overflow installed version classified as invalid (no exception)");
    }

    // classify_version_delta tests
    {
        using vivid::PackageUpdateClass;
        auto cvd = vivid::PackageManager::classify_version_delta;

        check(cvd("1.0.0", "1.0.0") == PackageUpdateClass::UpToDate,
              "cvd: identical versions → UpToDate");

        check(cvd("1.0.0", "1.1.0") == PackageUpdateClass::CompatibleUpdate,
              "cvd: minor bump same major → CompatibleUpdate");

        check(cvd("1.0.0", "1.0.1") == PackageUpdateClass::CompatibleUpdate,
              "cvd: patch bump same major → CompatibleUpdate");

        check(cvd("1.1.0", "1.0.0") == PackageUpdateClass::CompatibleUpdate,
              "cvd: same major downgrade → CompatibleUpdate");

        check(cvd("1.0.0", "2.0.0") == PackageUpdateClass::IncompatibleUpdate,
              "cvd: major bump up → IncompatibleUpdate");

        check(cvd("2.0.0", "1.0.0") == PackageUpdateClass::IncompatibleUpdate,
              "cvd: major downgrade → IncompatibleUpdate");

        check(cvd("", "1.0.0") == PackageUpdateClass::InvalidVersionData,
              "cvd: empty saved → InvalidVersionData");

        check(cvd("1.0.0", "") == PackageUpdateClass::InvalidVersionData,
              "cvd: empty installed → InvalidVersionData");

        check(cvd("not-a-version", "1.0.0") == PackageUpdateClass::InvalidVersionData,
              "cvd: bad saved semver → InvalidVersionData");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
