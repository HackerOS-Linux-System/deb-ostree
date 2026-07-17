#include "../cmd/sources_parser.h"
#include "../cmd/logging.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace debostree::sources {

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> parse_sources_list(const std::string& path) {
    std::vector<std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);

        /* Pomiń komentarze i puste linie */
        if (line.empty() || line[0] == '#') continue;

        /* Pomiń deb-src (nie potrzebujemy źródeł) */
        if (line.rfind("deb-src", 0) == 0) continue;

        /* Zachowaj tylko linie "deb <url> ..." */
        if (line.rfind("deb ", 0) == 0) {
            result.push_back(line);
            log::debug("sources.list: " + line.substr(0, 80));
        }
    }
    return result;
}

std::vector<std::string> parse_sources_deb822(const std::string& path) {
    std::vector<std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;

    /* DEB822: pola mogą być wieloliniowe (linia kontynuacji zaczyna się spacją).
     * Bloki oddzielone pustą linią. */
    std::string types, uris, suites, components;
    bool enabled = true;

    auto flush_block = [&]() {
        if (types.empty() || uris.empty() || suites.empty()) return;
        if (!enabled) { enabled = true; return; }

        /* Eksploduj wielokrotne URIs, suites, components */
        std::istringstream uris_s(uris), suites_s(suites);
        std::string uri, suite;
        while (uris_s >> uri) {
            while (suites_s >> suite) {
                /* types może być "deb deb-src" -- bierzemy tylko "deb" */
                if (types.find("deb") == std::string::npos) continue;
                std::string line = "deb " + uri + " " + suite;
                if (!components.empty()) line += " " + components;
                result.push_back(line);
                log::debug("sources.deb822: " + line.substr(0, 80));
            }
        }
        types.clear(); uris.clear(); suites.clear();
        components.clear(); enabled = true;
    };

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            if (line.empty()) flush_block();
            continue;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key   = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));

        /* Konwertuj klucz do lowercase */
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if      (key == "types")      types      = value;
        else if (key == "uris")       uris       = value;
        else if (key == "suites")     suites     = value;
        else if (key == "components") components = value;
        else if (key == "enabled")    enabled    = (value != "no");
    }
    flush_block();
    return result;
}

std::vector<std::string> load_sources(const std::string& sources_list,
                                       const std::string& sources_dir)
{
    std::vector<std::string> result;

    /* 1. /etc/apt/sources.list */
    if (fs::exists(sources_list)) {
        auto v = parse_sources_list(sources_list);
        result.insert(result.end(), v.begin(), v.end());
        log::debug("sources: " + sources_list + ": " + std::to_string(v.size()) + " zrodel");
    }

    /* 2. /etc/apt/sources.list.d/ -- [*].list i [*].sources */
    std::error_code ec;
    if (fs::exists(sources_dir, ec)) {
        std::vector<fs::path> paths;
        for (auto& e : fs::directory_iterator(sources_dir, ec))
            paths.push_back(e.path());
        std::sort(paths.begin(), paths.end()); /* deterministyczna kolejnosc */

        for (auto& p : paths) {
            auto ext = p.extension().string();
            std::vector<std::string> v;
            if      (ext == ".list")    v = parse_sources_list(p.string());
            else if (ext == ".sources") v = parse_sources_deb822(p.string());
            else continue;

            result.insert(result.end(), v.begin(), v.end());
            if (!v.empty())
                log::debug("sources: " + p.filename().string() + ": "
                           + std::to_string(v.size()) + " zrodel");
        }
    }

    return result;
}

} // namespace debostree::sources
