#pragma once
#include <string>
#include <unordered_map>

namespace s3d {
namespace i18n {

enum class Language { English = 0, Ukrainian = 1 };

// Key/value translation tables loaded from `assets/lang/*.lang`.
//
// Unlike the previous global table, this is an instance: the application owns
// it and hands a reference to everything that renders text. Nothing is
// reachable without going through that reference.
class Localizer {
public:
    // Loads `assets/lang/en.lang` and `assets/lang/uk.lang` from the detected
    // asset root. Missing files are reported and skipped.
    void loadBundles();

    void setLanguage(Language language);
    Language language() const { return language_; }

    // Returns the translation for `key`, or `key` itself when missing - so a
    // missing entry shows up in the UI instead of rendering as empty text.
    const char* get(const char* key) const;
    const std::string& languageName() const;

private:
    void applyLanguage(Language language);

    std::unordered_map<Language, std::unordered_map<std::string, std::string>> tables_;
    Language language_ = Language::English;
    std::unordered_map<std::string, std::string> active_;
};

} // namespace i18n
} // namespace s3d