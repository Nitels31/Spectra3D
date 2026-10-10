#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace s3d {
namespace i18n {

enum class Language {
    English = 0, Ukrainian = 1, German = 2, French = 3, Italian = 4, Spanish = 5,
    Lolcat = 6, Jeb = 7, Pirate = 8, BrokenUtf8 = 9, Hex = 10, GeometryDash = 11,
    Portuguese = 12, Minecraft = 13, Undertale = 14, Celeste = 15, Doom = 16,
    WolfOfWallStreet = 17, AmongUs = 18, Leet = 19, Klingon = 20, Gopnik = 21,
    Shakespeare = 22,
};

// Byte-wise FNV-1a, used for both the stored keys and the `const char*` probes so
// the two can never hash differently: std::hash is not specified to agree
// between std::string and std::string_view, and heterogeneous lookup is only
// correct when it does.
struct TextHash {
    using is_transparent = void;

    size_t operator()(std::string_view text) const noexcept {
        size_t h = (size_t)1469598103934665603ull;
        for (char c : text) {
            h ^= (size_t)(unsigned char)c;
            h *= (size_t)1099511628211ull;
        }
        return h;
    }
};

using TextTable = std::unordered_map<std::string, std::string, TextHash, std::equal_to<>>;

// Key/value translation tables loaded from `assets/lang/*.lang`.
//
// Unlike the previous global table, this is an instance: the application owns
// it and hands a reference to everything that renders text. Nothing is
// reachable without going through that reference.
class Localizer {
public:
    // Loads every bundle in `assets/lang` (en, uk, de, fr, it, es, lol) from the
    // detected asset root. Missing files are reported and skipped, and the
    // application keeps running in whatever it has.
    void loadBundles();

    void setLanguage(Language language);
    Language language() const { return language_; }

    // Returns the translation for `key`, or `key` itself when missing - so a
    // missing entry shows up in the UI instead of rendering as empty text.
    // Called for every visible label every frame, so the probe hashes the
    // characters in place rather than building a std::string first.
    const char* get(const char* key) const;

    const std::string& languageName() const;

private:
    void applyLanguage(Language language);

    std::unordered_map<Language, TextTable> tables_;
    Language language_ = Language::English;
    // Points into tables_ instead of holding a copy of the active language.
    // unordered_map nodes are stable, so a bundle reload keeps this valid;
    // applyLanguage() is what re-points it.
    const TextTable* active_ = nullptr;
};

} // namespace i18n
} // namespace s3d
