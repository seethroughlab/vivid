// Headless unit test for vivid-package.json parsing — specifically the operator `kind`
// field and how it defaults the wgpu link flag. Pure parse (no clang++, no wgpu): writes
// temp manifests and asserts parse_package_manifest's behavior.
#include "packages/package_manifest.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace vivid;

// Write a vivid-package.json with `body` (the operators array + any extra keys) into a fresh
// temp package dir and parse it.
static PackageManifest parse_with(const std::string& tag, const std::string& operators_json) {
    const fs::path dir = fs::temp_directory_path() / ("vivid_manifest_test_" + tag);
    fs::create_directories(dir);
    std::ofstream(dir / "vivid-package.json")
        << "{ \"name\": \"t\", \"version\": \"0.1.0\", \"operators\": " << operators_json << " }";
    return parse_package_manifest(dir.string());
}

int main() {
    // kind gpu_visual -> links wgpu (gpu == true).
    {
        PackageManifest m = parse_with("gpu", R"([{"name":"G","source":"g.cpp","kind":"gpu_visual"}])");
        CHECK(m.ok);
        CHECK(m.operators.size() == 1);
        CHECK(m.operators[0].kind == "gpu_visual");
        CHECK(m.operators[0].gpu == true);
    }
    // kind audio_effect / instrument / frame -> no wgpu (gpu == false).
    {
        PackageManifest m = parse_with("aud", R"([
            {"name":"Fx","source":"fx.cpp","kind":"audio_effect"},
            {"name":"Inst","source":"inst.cpp","kind":"instrument"},
            {"name":"Ctl","source":"ctl.cpp","kind":"frame"}])");
        CHECK(m.ok);
        CHECK(m.operators.size() == 3);
        CHECK(m.operators[0].gpu == false);
        CHECK(m.operators[1].gpu == false);
        CHECK(m.operators[2].gpu == false);
    }
    // An explicit "gpu" overrides the kind-derived default.
    {
        PackageManifest m = parse_with("override",
            R"([{"name":"Fx","source":"fx.cpp","kind":"audio_effect","gpu":true}])");
        CHECK(m.ok);
        CHECK(m.operators[0].gpu == true);   // explicit gpu:true wins over audio_effect's false
    }
    // Back-compat: no kind, no gpu -> defaults to linking wgpu (gpu == true).
    {
        PackageManifest m = parse_with("legacy", R"([{"name":"G","source":"g.cpp"}])");
        CHECK(m.ok);
        CHECK(m.operators[0].kind.empty());
        CHECK(m.operators[0].gpu == true);
    }
    // Back-compat: no kind, explicit gpu:false -> respected (old audio/frame manifests).
    {
        PackageManifest m = parse_with("legacy_false", R"([{"name":"N","source":"n.cpp","gpu":false}])");
        CHECK(m.ok);
        CHECK(m.operators[0].gpu == false);
    }
    // An unknown kind is a hard parse error (catches typos before a confusing build).
    {
        PackageManifest m = parse_with("bad", R"([{"name":"X","source":"x.cpp","kind":"audioeffect"}])");
        CHECK(!m.ok);
        CHECK(m.error.find("unknown \"kind\"") != std::string::npos);
    }

    // Ph5 P2-02: manifest_present distinguishes a REAL bad package (a present-but-unparseable
    // vivid-package.json — discovery must surface it) from a dir that simply isn't a package.
    {   // malformed JSON in a present manifest -> !ok AND present
        const fs::path dir = fs::temp_directory_path() / "vivid_manifest_test_malformed";
        fs::create_directories(dir);
        std::ofstream(dir / "vivid-package.json") << "{ not valid json ";
        PackageManifest m = parse_package_manifest(dir.string());
        CHECK(!m.ok && m.manifest_present);
    }
    {   // no manifest at all -> !ok and NOT present (just not a package; must not be surfaced)
        const fs::path dir = fs::temp_directory_path() / "vivid_manifest_test_none";
        fs::create_directories(dir);
        fs::remove(dir / "vivid-package.json");
        PackageManifest m = parse_package_manifest(dir.string());
        CHECK(!m.ok && !m.manifest_present);
    }
    {   // a valid manifest -> ok and present
        PackageManifest m = parse_with("present_ok", R"([{"name":"G","source":"g.cpp","kind":"gpu_visual"}])");
        CHECK(m.ok && m.manifest_present);
    }

    return vivid::test::summary("test_package_manifest");
}
