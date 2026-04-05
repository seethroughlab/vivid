#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "test_helpers.h"

namespace {

using json = nlohmann::json;

json load_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open " + path.string());
    }
    return json::parse(in, nullptr, true, true);
}

std::set<std::string> json_keys(const json& j) {
    std::set<std::string> out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        out.insert(it.key());
    }
    return out;
}

std::set<std::string> collect_t_keys(const std::filesystem::path& root) {
    const std::vector<std::filesystem::path> scan_dirs = {
        root / "src" / "ui",
        root / "src" / "runtime" / "core",
        root / "src" / "runtime" / "platform",
    };

    const std::regex t_re("T\\(\"([^\"]+)\"");
    const std::regex t_plural_re("T_PLURAL\\(\"([^\"]+)\",\\s*\"([^\"]+)\"");
    std::set<std::string> keys;

    for (const auto& dir : scan_dirs) {
        if (!std::filesystem::exists(dir)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".mm") continue;

            std::ifstream in(entry.path());
            if (!in) continue;
            const std::string content((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());

            for (std::sregex_iterator it(content.begin(), content.end(), t_re), end; it != end; ++it) {
                keys.insert((*it)[1].str());
            }
            for (std::sregex_iterator it(content.begin(), content.end(), t_plural_re), end; it != end; ++it) {
                keys.insert((*it)[1].str());
                keys.insert((*it)[2].str());
            }
        }
    }

    return keys;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_locale_catalog_parity <source_dir>\n");
        return 1;
    }

    const std::filesystem::path root = argv[1];
    const std::filesystem::path locales_dir = root / "locales";
    const std::filesystem::path en_path = locales_dir / "en.json";
    std::vector<std::string> locale_names;
    for (const auto& entry : std::filesystem::directory_iterator(locales_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        const auto stem = entry.path().stem().string();
        if (stem == "en") continue;
        locale_names.push_back(stem);
    }
    std::sort(locale_names.begin(), locale_names.end());

    std::fprintf(stderr, "\n=== Test: locale catalog parity ===\n\n");

    json en_json = load_json(en_path);
    const auto en_keys = json_keys(en_json);
    const auto t_keys = collect_t_keys(root);

    std::vector<std::string> missing_in_en;
    for (const auto& key : t_keys) {
        if (!en_keys.count(key)) missing_in_en.push_back(key);
    }

    if (!missing_in_en.empty()) {
        std::fprintf(stderr, "Missing i18n keys in locales/en.json:\n");
        for (const auto& key : missing_in_en) {
            std::fprintf(stderr, "  %s\n", key.c_str());
        }
    }
    check(missing_in_en.empty(), "English locale contains every T(...) and T_PLURAL(...) key referenced by desktop UI code");

    for (const auto& name : locale_names) {
        const auto path = locales_dir / (name + ".json");
        json locale_json = load_json(path);
        const auto locale_keys = json_keys(locale_json);

        std::vector<std::string> missing;
        for (const auto& key : en_keys) {
            if (!locale_keys.count(key)) missing.push_back(key);
        }

        if (!missing.empty()) {
            std::fprintf(stderr, "Locale %s missing %zu key(s):\n", name.c_str(), missing.size());
            for (const auto& key : missing) {
                std::fprintf(stderr, "  %s\n", key.c_str());
            }
        }

        const std::string msg = "Locale " + name + " contains every key from locales/en.json";
        check(missing.empty(), msg.c_str());
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
