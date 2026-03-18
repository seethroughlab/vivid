#include "ui/i18n.h"
#include <yyjson.h>
#include <cstdio>

namespace vivid::ui {

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

bool I18n::load(const char* json_path) {
    strings_.clear();
    locale_.clear();

    yyjson_read_flag flags = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;
    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(json_path, flags, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[i18n] failed to load %s: %s\n", json_path,
                     err.msg ? err.msg : "unknown error");
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        std::fprintf(stderr, "[i18n] %s: root is not an object\n", json_path);
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        yyjson_val* val = yyjson_obj_iter_get_val(key);
        if (yyjson_is_str(key) && yyjson_is_str(val)) {
            strings_.emplace(yyjson_get_str(key), yyjson_get_str(val));
        }
    }

    // Extract locale id if present (e.g. "locale": "fr")
    yyjson_val* loc = yyjson_obj_get(root, "locale");
    if (loc && yyjson_is_str(loc))
        locale_ = yyjson_get_str(loc);

    yyjson_doc_free(doc);
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
