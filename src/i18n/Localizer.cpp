#include "Localizer.hpp"

#include "../core/Log.hpp"
#include "../core/Str.hpp"
#include "../platform/Filesystem.hpp"

#include <fstream>

namespace s3d {
namespace i18n {
namespace {

struct Bundle {
    Language language;
    const char* fileName;
};

const Bundle kBundles[] = {{Language::English, "en.lang"}, {Language::Ukrainian, "uk.lang"}};

} // namespace

void Localizer::loadBundles() {
    const std::string root = str::join(fs::assetRoot(), "lang");
    for (const Bundle& bundle : kBundles) {
        std::unordered_map<std::string, std::string> table;
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
    const auto it = tables_.find(language);
    active_ = it == tables_.end() ? std::unordered_map<std::string, std::string>() : it->second;
}

void Localizer::setLanguage(Language language) {
    language_ = language;
    applyLanguage(language);
}

const char* Localizer::get(const char* key) const {
    const auto it = active_.find(key);
    return it == active_.end() ? key : it->second.c_str();
}

const std::string& Localizer::languageName() const {
    static const std::string kEnglish = "English";
    static const std::string kUkrainian = "Ukrainian";
    return language_ == Language::Ukrainian ? kUkrainian : kEnglish;
}

} // namespace i18n
} // namespace s3d