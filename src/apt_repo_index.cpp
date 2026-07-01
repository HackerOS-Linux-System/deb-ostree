#include "../cmd/apt_repo_index.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include <cstdlib>

namespace debostree::apt {

namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/*
 * Parsuje JEDEN blok pol (od "Package:" do nastepnej pustej linii) i
 * wypelnia PackageEntry. Linie kontynuacji (zaczynajace sie od spacji/taba)
 * sa dolaczane do wartosci poprzedniego pola z separatorem spacji.
 */
PackageEntry parse_block(const std::vector<std::string>& lines) {
    PackageEntry entry;
    std::unordered_map<std::string, std::string*> field_map = {
        {"Package",      &entry.package},
        {"Version",      &entry.version},
        {"Architecture", &entry.architecture},
        {"Filename",     &entry.filename},
        {"SHA256",       &entry.sha256},
        {"Depends",      &entry.depends},
        {"Pre-Depends",  &entry.pre_depends},
        {"Recommends",   &entry.recommends},
        {"Conflicts",    &entry.conflicts},
        {"Provides",     &entry.provides},
        {"Replaces",     &entry.replaces},
        {"Breaks",       &entry.breaks},
    };

    std::string* current_field = nullptr;

    for (auto& raw_line : lines) {
        if (raw_line.empty()) continue;

        bool is_continuation = (raw_line[0] == ' ' || raw_line[0] == '\t');
        if (is_continuation) {
            if (current_field) {
                *current_field += " " + trim(raw_line);
            }
            continue;
        }

        auto colon = raw_line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(raw_line.substr(0, colon));
        std::string value = trim(raw_line.substr(colon + 1));

        if (key == "Size") {
            entry.size = std::strtoull(value.c_str(), nullptr, 10);
            current_field = nullptr;
            continue;
        }

        auto it = field_map.find(key);
        if (it != field_map.end()) {
            *(it->second) = value;
            current_field = it->second;
        } else {
            current_field = nullptr;
        }
    }

    return entry;
}

} // namespace

RepoIndex RepoIndex::parse(const std::string& content) {
    RepoIndex idx;
    std::istringstream iss(content);
    std::string line;
    std::vector<std::string> current_block;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) {
            if (!current_block.empty()) {
                PackageEntry entry = parse_block(current_block);
                if (!entry.package.empty()) {
                    idx.entries_.push_back(std::move(entry));
                }
                current_block.clear();
            }
            continue;
        }
        current_block.push_back(line);
    }

    if (!current_block.empty()) {
        PackageEntry entry = parse_block(current_block);
        if (!entry.package.empty()) {
            idx.entries_.push_back(std::move(entry));
        }
    }

    return idx;
}

RepoIndex RepoIndex::load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("apt::RepoIndex: nie mozna otworzyc " + path);

    std::ostringstream buf;
    buf << f.rdbuf();
    return parse(buf.str());
}

} // namespace debostree::apt
