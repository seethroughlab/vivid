#include <nlohmann/json.hpp>
#include "ui/i18n.h"
#include <fstream>
#include <cstdio>

namespace vivid::ui {

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

bool I18n::load(const char* json_path) {
    strings_.clear();
    locale_.clear();

    nlohmann::json j;
    try {
        std::ifstream ifs(json_path);
        if (!ifs) {
            std::fprintf(stderr, "[i18n] failed to load %s: could not open file\n", json_path);
            return false;
        }
        j = nlohmann::json::parse(ifs, nullptr, true, true);  // ignore_comments=true
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[i18n] failed to load %s: %s\n", json_path, e.what());
        return false;
    }

    if (!j.is_object()) {
        std::fprintf(stderr, "[i18n] %s: root is not an object\n", json_path);
        return false;
    }

    for (auto& [key, val] : j.items()) {
        if (val.is_string()) {
            strings_.emplace(key, val.get<std::string>());
        }
    }

    // Extract locale id if present (e.g. "locale": "fr")
    auto it = j.find("locale");
    if (it != j.end() && it->is_string())
        locale_ = it->get<std::string>();

    return true;
}

const char* I18n::get(const char* key, const char* fallback) const {
    if (strings_.empty()) return fallback;
    auto it = strings_.find(key);
    return (it != strings_.end()) ? it->second.c_str() : fallback;
}

const char* I18n::get_plural(const char* key, int n,
                             const char* singular_fb, const char* plural_fb) const {
    if (strings_.empty()) return (n == 1) ? singular_fb : plural_fb;

    // Look for key_one / key_other
    std::string k(key);
    auto it = strings_.find((n == 1) ? (k + "_one") : (k + "_other"));
    if (it != strings_.end()) return it->second.c_str();

    // Fall back to base key
    it = strings_.find(k);
    if (it != strings_.end()) return it->second.c_str();

    return (n == 1) ? singular_fb : plural_fb;
}

} // namespace vivid::ui
