#include "i18n/Localizer.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <fstream>

namespace s3d {
namespace i18n {
namespace {

struct Bundle {
    Language language;
    const char* fileName;
};

const Bundle kBundles[] = {
    {Language::English, "en.lang"}, {Language::Ukrainian, "uk.lang"},
    {Language::German, "de.lang"}, {Language::French, "fr.lang"},
    {Language::Italian, "it.lang"}, {Language::Spanish, "es.lang"},
    {Language::Lolcat, "lol.lang"},
    {Language::Jeb, "jeb.lang"}, {Language::Pirate, "pir.lang"},
    {Language::BrokenUtf8, "u8f.lang"}, {Language::Hex, "hex.lang"},
    {Language::GeometryDash, "gd.lang"},
    {Language::Portuguese, "pt.lang"}, {Language::Minecraft, "mc.lang"},
    {Language::Undertale, "ut.lang"}, {Language::Celeste, "cel.lang"},
    {Language::Doom, "dm.lang"}, {Language::WolfOfWallStreet, "wows.lang"},
    {Language::AmongUs, "sus.lang"}, {Language::Leet, "leet.lang"},
    {Language::Klingon, "tlh.lang"}, {Language::Gopnik, "gop.lang"},
    {Language::Shakespeare, "sha.lang"},
};

} // namespace

void Localizer::loadBundles() {
    const std::string root = str::join(fs::assetRoot(), "lang");
    // Drop the pointer first: the tables it refers to are about to be replaced.
    active_ = nullptr;
    for (const Bundle& bundle : kBundles) {
        TextTable table;
        const std::string path = str::join(root, bundle.fileName);

        std::ifstream file(path);
        if (!file) {
            Log::warn("i18n: missing %s", path.c_str());
            continue;
        }
        std::string line;
        while (std::getline(file, line)) {
            line = str::trim(line);
            if (line.empty() || line[0] == '#') continue;
            const size_t separator = line.find('=');
            if (separator == std::string::npos) continue;
            const std::string key = str::trim(line.substr(0, separator));
            if (key.empty()) continue;
            table[key] = str::trim(line.substr(separator + 1));
        }
        Log::info("i18n: %d strings from %s", (int)table.size(), path.c_str());
        tables_[bundle.language] = std::move(table);
    }
    applyLanguage(language_);
}

void Localizer::applyLanguage(Language language) {
    language_ = language;
    const auto it = tables_.find(language);
    // A missing bundle leaves nothing active, which is what the empty table the
    // old copy-based version installed used to mean.
    active_ = it == tables_.end() ? nullptr : &it->second;
}

void Localizer::setLanguage(Language language) {
    applyLanguage(language);
}

const char* Localizer::get(const char* key) const {
    if (!active_) return key;
    const auto it = active_->find(key);
    return it == active_->end() ? key : it->second.c_str();
}

// The name in its own language, which is what a list of languages is for: a user
// looking for their language looks for the word they call it by.
const std::string& Localizer::languageName() const {
    static const std::string kEnglish = "English";
    static const std::string kUkrainian = "Українська";
    static const std::string kGerman = "Deutsch";
    static const std::string kFrench = "Français";
    static const std::string kItalian = "Italiano";
    static const std::string kSpanish = "Español";
    static const std::string kLolcat = "Lolcat";
    static const std::string kJeb = "Jeb Kerman";
    static const std::string kPirate = "Pirate";
    static const std::string kBroken = "Broken UTF-8";
    static const std::string kHex = "Hexadecimal";
    static const std::string kGeometryDash = "Geometry Dash";
    static const std::string kPortuguese = "Português";
    static const std::string kMinecraft = "Minecraft";
    static const std::string kUndertale = "Undertale";
    static const std::string kCeleste = "Celeste";
    static const std::string kDoom = "Doom";
    static const std::string kWows = "Wolf of Wall Street";
    static const std::string kAmongUs = "Among Us";
    static const std::string kLeet = "1337";
    static const std::string kKlingon = "Klingon";
    static const std::string kGopnik = "Пацан";
    static const std::string kShakespeare = "Shakespeare";
    switch (language_) {
    case Language::Ukrainian: return kUkrainian;
    case Language::German: return kGerman;
    case Language::French: return kFrench;
    case Language::Italian: return kItalian;
    case Language::Spanish: return kSpanish;
    case Language::Lolcat: return kLolcat;
    case Language::Jeb: return kJeb;
    case Language::Pirate: return kPirate;
    case Language::BrokenUtf8: return kBroken;
    case Language::Hex: return kHex;
    case Language::GeometryDash: return kGeometryDash;
    case Language::Portuguese: return kPortuguese;
    case Language::Minecraft: return kMinecraft;
    case Language::Undertale: return kUndertale;
    case Language::Celeste: return kCeleste;
    case Language::Doom: return kDoom;
    case Language::WolfOfWallStreet: return kWows;
    case Language::AmongUs: return kAmongUs;
    case Language::Leet: return kLeet;
    case Language::Klingon: return kKlingon;
    case Language::Gopnik: return kGopnik;
    case Language::Shakespeare: return kShakespeare;
    case Language::English:
    default: return kEnglish;
    }
}

} // namespace i18n
} // namespace s3d