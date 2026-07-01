#include "../cmd/status_db.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace debostree::statusdb {

namespace {

std::string db_path(const std::string& rootfs_path) {
    return rootfs_path + "/var/lib/deb-ostree/status.db";
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

/* Parsuje string JSON otoczony cudzyslowami (z podstawowymi sekwencjami
 * ucieczki) zaczynajac od pos (ktory wskazuje na otwierajacy '"').
 * Zwraca sparsowany string i przesuwa pos za zamykajacy '"'. */
std::string parse_json_string(const std::string& line, size_t& pos) {
    if (pos >= line.size() || line[pos] != '"') return "";
    ++pos;
    std::string result;
    while (pos < line.size() && line[pos] != '"') {
        if (line[pos] == '\\' && pos + 1 < line.size()) {
            result += line[pos + 1];
            pos += 2;
        } else {
            result += line[pos];
            ++pos;
        }
    }
    if (pos < line.size()) ++pos; /* przeskocz zamykajacy '"' */
    return result;
}

/* Parsuje jedna linie JSON-lines (plaski obiekt) na InstalledPackage.
 * Format jest zawsze generowany przez nasz wlasny serializer, wiec parser
 * moze byc uproszczony. */
InstalledPackage parse_line(const std::string& line) {
    InstalledPackage pkg;
    size_t pos = 0;

    size_t name_pos = line.find("\"name\":\"");
    if (name_pos != std::string::npos) {
        pos = name_pos + std::string("\"name\":").size();
        pkg.name = parse_json_string(line, pos);
    }

    size_t version_pos = line.find("\"version\":\"");
    if (version_pos != std::string::npos) {
        pos = version_pos + std::string("\"version\":").size();
        pkg.version = parse_json_string(line, pos);
    }

    size_t files_pos = line.find("\"files\":[");
    if (files_pos != std::string::npos) {
        pos = files_pos + std::string("\"files\":[").size();
        while (pos < line.size() && line[pos] != ']') {
            if (line[pos] == '"') {
                pkg.files.push_back(parse_json_string(line, pos));
            } else {
                ++pos; /* przecinek/spacja miedzy elementami */
            }
        }
    }

    return pkg;
}

std::string serialize_line(const InstalledPackage& pkg) {
    std::ostringstream oss;
    oss << "{\"name\":\"" << json_escape(pkg.name) << "\","
        << "\"version\":\"" << json_escape(pkg.version) << "\","
        << "\"files\":[";
    for (size_t i = 0; i < pkg.files.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << json_escape(pkg.files[i]) << "\"";
    }
    oss << "]}";
    return oss.str();
}

} // namespace

std::vector<InstalledPackage> load(const std::string& rootfs_path) {
    std::vector<InstalledPackage> result;
    std::ifstream f(db_path(rootfs_path));
    if (!f.is_open()) return result; /* brak bazy = system bez pakietow warstwowych */

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        InstalledPackage pkg = parse_line(line);
        if (!pkg.name.empty()) result.push_back(std::move(pkg));
    }
    return result;
}

void save(const std::string& rootfs_path, const std::vector<InstalledPackage>& packages) {
    std::string path = db_path(rootfs_path);
    fs::create_directories(fs::path(path).parent_path());

    std::ofstream f(path, std::ios::trunc);
    for (auto& pkg : packages) {
        f << serialize_line(pkg) << "\n";
    }
}

void upsert(const std::string& rootfs_path, const InstalledPackage& pkg) {
    auto packages = load(rootfs_path);

    bool found = false;
    for (auto& existing : packages) {
        if (existing.name == pkg.name) {
            existing = pkg;
            found = true;
            break;
        }
    }
    if (!found) packages.push_back(pkg);

    save(rootfs_path, packages);
}

void remove(const std::string& rootfs_path, const std::string& package_name) {
    auto packages = load(rootfs_path);
    std::vector<InstalledPackage> filtered;
    filtered.reserve(packages.size());
    for (auto& pkg : packages) {
        if (pkg.name != package_name) filtered.push_back(std::move(pkg));
    }
    save(rootfs_path, filtered);
}

bool is_installed(const std::string& rootfs_path, const std::string& package_name) {
    auto packages = load(rootfs_path);
    for (auto& pkg : packages) {
        if (pkg.name == package_name) return true;
    }
    return false;
}

} // namespace debostree::statusdb
