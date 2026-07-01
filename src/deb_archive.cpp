#include "../cmd/deb_archive.h"
#include "../cmd/compress_util.h"
#include "../cmd/tar_extractor.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <unordered_map>

namespace debostree::deb {

namespace {

constexpr size_t AR_GLOBAL_HEADER_SIZE = 8;  /* "!<arch>\n" */
constexpr size_t AR_ENTRY_HEADER_SIZE = 60;

struct ArEntry {
    std::string name;
    std::vector<uint8_t> data;
};

/* Parsuje cale archiwum ar w pamieci na liste wpisow (nazwa + dane).
 * Format .deb ma zazwyczaj 3 wpisy, ale parser jest ogolny -- dziala dla
 * dowolnej liczby wpisow w archiwum ar. */
std::vector<ArEntry> parse_ar_archive(const std::vector<uint8_t>& data) {
    if (data.size() < AR_GLOBAL_HEADER_SIZE ||
        std::memcmp(data.data(), "!<arch>\n", AR_GLOBAL_HEADER_SIZE) != 0) {
        throw std::runtime_error("deb::DebArchive: nieprawidlowy naglowek ar (oczekiwano '!<arch>\\n')");
    }

    std::vector<ArEntry> entries;
    size_t pos = AR_GLOBAL_HEADER_SIZE;

    while (pos + AR_ENTRY_HEADER_SIZE <= data.size()) {
        const char* header = reinterpret_cast<const char*>(data.data() + pos);

        /* Nazwa: 16 bajtow, padded spacjami. Pliki "debian-binary",
         * "control.tar" i "data.tar" (z rozszerzeniem kompresji) maja
         * krotkie, ustalone nazwy ktore zawsze mieszcza sie w 16 bajtach,
         * wiec nie potrzebujemy obslugi dlugich nazw GNU ar (rozszerzenie
         * "//" tabeli nazw) -- to nie wystepuje w plikach .deb. */
        std::string name(header, 16);
        auto last_non_space = name.find_last_not_of(' ');
        name = (last_non_space == std::string::npos) ? "" : name.substr(0, last_non_space + 1);
        if (!name.empty() && name.back() == '/') name.pop_back();

        std::string size_field(header + 48, 10);
        uint64_t entry_size = std::strtoull(size_field.c_str(), nullptr, 10);

        size_t data_start = pos + AR_ENTRY_HEADER_SIZE;
        if (data_start + entry_size > data.size()) {
            throw std::runtime_error(
                "deb::DebArchive: uszkodzone archiwum ar -- wpis '" + name +
                "' wykracza poza koniec pliku");
        }

        ArEntry entry;
        entry.name = name;
        entry.data.assign(data.data() + data_start, data.data() + data_start + entry_size);
        entries.push_back(std::move(entry));

        size_t padded_size = entry_size + (entry_size % 2);
        pos = data_start + padded_size;
    }

    return entries;
}

/* Znajduje wpis ar ktorego nazwa zaczyna sie od danego prefiksu (np.
 * "control.tar" pasuje do "control.tar.gz", "control.tar.xz", ...) --
 * format .deb nie precyzuje ktora kompresja zostanie uzyta, wiec szukamy
 * po prefiksie a nie dokladnej nazwie. */
const ArEntry* find_entry_by_prefix(const std::vector<ArEntry>& entries, const std::string& prefix) {
    for (auto& e : entries) {
        if (e.name.rfind(prefix, 0) == 0) return &e;
    }
    return nullptr;
}

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/* Parsuje plik "control" (RFC822-podobny, jeden "akapit" -- w przeciwienstwie
 * do indeksu Packages ktory ma wiele akapitow) na ControlInfo. */
ControlInfo parse_control_file(const std::vector<uint8_t>& content) {
    ControlInfo info;
    std::unordered_map<std::string, std::string*> field_map = {
        {"Package",      &info.package},
        {"Version",      &info.version},
        {"Architecture", &info.architecture},
        {"Depends",      &info.depends},
        {"Pre-Depends",  &info.pre_depends},
        {"Provides",     &info.provides},
        {"Conflicts",    &info.conflicts},
        {"Replaces",     &info.replaces},
        {"Breaks",       &info.breaks},
        {"Maintainer",   &info.maintainer},
        {"Description",  &info.description},
    };

    std::istringstream iss(std::string(content.begin(), content.end()));
    std::string line;
    std::string* current_field = nullptr;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        bool is_continuation = (line[0] == ' ' || line[0] == '\t');
        if (is_continuation) {
            if (current_field) *current_field += " " + trim(line);
            continue;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) { current_field = nullptr; continue; }

        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));

        auto it = field_map.find(key);
        if (it != field_map.end()) {
            *(it->second) = value;
            current_field = it->second;
        } else {
            current_field = nullptr;
        }
    }

    return info;
}

} // namespace

std::vector<uint8_t> DebArchive::read_whole_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("deb::DebArchive: nie mozna otworzyc " + path);
    return std::vector<uint8_t>(
        (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

DebArchive DebArchive::open(const std::string& deb_path) {
    std::vector<uint8_t> raw = read_whole_file(deb_path);
    std::vector<ArEntry> entries = parse_ar_archive(raw);

    const ArEntry* debian_binary = find_entry_by_prefix(entries, "debian-binary");
    if (!debian_binary)
        throw std::runtime_error("deb::DebArchive: brak wpisu 'debian-binary' w " + deb_path +
                                 " -- to nie jest poprawny plik .deb");

    const ArEntry* control_entry = find_entry_by_prefix(entries, "control.tar");
    if (!control_entry)
        throw std::runtime_error("deb::DebArchive: brak wpisu control.tar w " + deb_path);

    const ArEntry* data_entry = find_entry_by_prefix(entries, "data.tar");
    if (!data_entry)
        throw std::runtime_error("deb::DebArchive: brak wpisu data.tar w " + deb_path);

    DebArchive archive;
    /* control.tar jest zazwyczaj maly (kilkadziesiat KB) -- dekompresujemy
     * od razu, nie ma sensu odkladac. */
    archive.control_tar_ = compress::decompress_auto(control_entry->data);
    /* data.tar moze byc duze (dziesiatki/setki MB) -- zachowujemy
     * skompresowane, dekompresja nastapi tylko jesli extract_data_to()
     * zostanie faktycznie wywolane. */
    archive.data_tar_raw_ = data_entry->data;

    return archive;
}

ControlInfo DebArchive::read_control() const {
    std::vector<uint8_t> control_content =
        tarball::extract_single_file(control_tar_, "./control");

    if (control_content.empty()) {
        /* Niektore archiwa nie maja prefiksu "./" w nazwach -- probujemy
         * bez niego jako fallback. */
        control_content = tarball::extract_single_file(control_tar_, "control");
    }

    if (control_content.empty())
        throw std::runtime_error("deb::DebArchive: nie znaleziono pliku 'control' w control.tar");

    return parse_control_file(control_content);
}

std::string DebArchive::read_maintainer_script(const std::string& script_name) const {
    std::vector<uint8_t> content = tarball::extract_single_file(control_tar_, "./" + script_name);
    if (content.empty()) {
        content = tarball::extract_single_file(control_tar_, script_name);
    }
    return std::string(content.begin(), content.end());
}

void DebArchive::extract_data_to(const std::string& dest_dir) const {
    std::vector<uint8_t> data_tar = compress::decompress_auto(data_tar_raw_);
    tarball::extract_to_directory(data_tar, dest_dir);
}

std::vector<std::string> DebArchive::list_data_files() const {
    std::vector<uint8_t> data_tar = compress::decompress_auto(data_tar_raw_);
    std::vector<tarball::Entry> entries = tarball::list_entries(data_tar);

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for (auto& e : entries) paths.push_back(e.path);
    return paths;
}

} // namespace debostree::deb
