#pragma once
#include <string>
#include <unordered_map>

namespace vivid::ui {

class I18n {
public:
    static I18n& instance();

    // Load translations from a JSON file. Returns false if file not found/parse error.
    // Format: { "save": "Enregistrer", "cancel": "Annuler", ... }
    bool load(const char* json_path);

    // Look up a translated string. Returns fallback if no translation loaded.
    const char* get(const char* key, const char* fallback) const;

    // Plural form. Returns singular_fb/plural_fb unless overridden by locale.
    // Locale file keys: "key_one" and "key_other" for singular/plural forms.
    const char* get_plural(const char* key, int n,
                           const char* singular_fb, const char* plural_fb) const;

    const std::string& locale() const { return locale_; }

private:
    I18n() = default;
    std::unordered_map<std::string, std::string> strings_;
    std::string locale_;
};

} // namespace vivid::ui

// Convenience macros — placed outside namespace so callsites don't need a using-directive
#define T(key, fallback) ::vivid::ui::I18n::instance().get(key, fallback)
#define T_PLURAL(key, n, singular, plural) \
    ::vivid::ui::I18n::instance().get_plural(key, n, singular, plural)
