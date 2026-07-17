#include "../cmd/status_db.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <unistd.h>

namespace fs = std::filesystem;

namespace debostree::statusdb {

/* ── Sciezki ── */

std::string dpkg_status_path(const std::string& rootfs_path) {
    return rootfs_path + "/var/lib/dpkg/status";
}

std::string dpkg_info_dir(const std::string& rootfs_path) {
    return rootfs_path + "/var/lib/dpkg/info";
}

std::string dpkg_list_path(const std::string& rootfs_path, const std::string& pkg) {
    return dpkg_info_dir(rootfs_path) + "/" + pkg + ".list";
}

/* ── Parser RFC822 dpkg/status ── */

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/* Parsuje jeden blok dpkg/status (oddzielony pustą linią) do InstalledPackage.
 * Zwraca false jeśli blok jest pusty lub nieprawidlowy. */
static bool parse_dpkg_block(const std::string& block, InstalledPackage& pkg) {
    pkg = {};
    std::istringstream iss(block);
    std::string line;
    std::string last_key;
    std::ostringstream desc_buf;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        /* Linia kontynuacji (zaczyna sie spacją) */
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            if (last_key == "description") {
                desc_buf << "\n" << line;
            }
            continue;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key   = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));

        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        last_key = key;

        if      (key == "package")        pkg.name          = value;
        else if (key == "status")         pkg.status        = value;
        else if (key == "version")        pkg.version       = value;
        else if (key == "architecture")   pkg.architecture  = value;
        else if (key == "maintainer")     pkg.maintainer    = value;
        else if (key == "depends")        pkg.depends       = value;
        else if (key == "pre-depends")    pkg.pre_depends   = value;
        else if (key == "provides")       pkg.provides      = value;
        else if (key == "section")        pkg.section       = value;
        else if (key == "priority")       pkg.priority      = value;
        else if (key == "installed-size") {
            try { pkg.installed_size = std::stoull(value); } catch (...) {}
        }
        else if (key == "description")    desc_buf << value;
    }

    pkg.description = desc_buf.str();
    return !pkg.name.empty();
}

/* Serializuje InstalledPackage do bloku RFC822 dpkg/status */
static std::string serialize_dpkg_block(const InstalledPackage& pkg) {
    std::ostringstream oss;
    oss << "Package: "       << pkg.name       << "\n";
    oss << "Status: "        << pkg.status     << "\n";

    if (!pkg.priority.empty())    oss << "Priority: "    << pkg.priority    << "\n";
    if (!pkg.section.empty())     oss << "Section: "     << pkg.section     << "\n";
    if (pkg.installed_size > 0)
        oss << "Installed-Size: " << pkg.installed_size  << "\n";
    if (!pkg.maintainer.empty())  oss << "Maintainer: "  << pkg.maintainer  << "\n";
    if (!pkg.architecture.empty()) oss << "Architecture: " << pkg.architecture << "\n";
    if (!pkg.pre_depends.empty()) oss << "Pre-Depends: " << pkg.pre_depends << "\n";
    if (!pkg.depends.empty())     oss << "Depends: "     << pkg.depends     << "\n";
    if (!pkg.provides.empty())    oss << "Provides: "    << pkg.provides    << "\n";

    oss << "Version: "       << pkg.version    << "\n";
    oss << "Installed-By: deb-ostree\n";

    if (!pkg.description.empty()) {
        oss << "Description: " << pkg.description << "\n";
    } else {
        oss << "Description: (installed by deb-ostree)\n";
    }

    return oss.str();
}

/* Wczytuje caly plik dpkg/status jako mape: name -> (blok_tekst, InstalledPackage).
 * Uzywa string map dla efektywnego update. */
using BlockMap = std::unordered_map<std::string, std::string>;

static BlockMap load_raw_blocks(const std::string& status_path) {
    BlockMap blocks;
    std::ifstream f(status_path);
    if (!f.is_open()) return blocks;

    std::string current_block;
    std::string current_name;
    std::string line;

    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) {
            if (!current_name.empty()) {
                blocks[current_name] = current_block;
                current_block.clear();
                current_name.clear();
            }
            continue;
        }

        current_block += line + "\n";

        if (line.rfind("Package: ", 0) == 0)
            current_name = trim(line.substr(9));
    }

    if (!current_name.empty())
        blocks[current_name] = current_block;

    return blocks;
}

static void write_status_atomic(const std::string& status_path,
                                 const BlockMap& blocks)
{
    fs::create_directories(fs::path(status_path).parent_path());
    std::string tmp = status_path + ".tmp." + std::to_string(::getpid());

    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) throw std::runtime_error("status_db: nie mozna zapisac " + tmp);
        for (auto& [name, block] : blocks) {
            f << block << "\n";
        }
    }

    std::error_code ec;
    fs::rename(tmp, status_path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("status_db: rename nie powiodlo sie: " + ec.message());
    }
}

/* ── API publiczne ── */

static std::vector<InstalledPackage>
load_from_blocks(const BlockMap& blocks, bool only_deb_ostree) {
    std::vector<InstalledPackage> result;
    for (auto& [name, block] : blocks) {
        InstalledPackage pkg;
        if (!parse_dpkg_block(block, pkg)) continue;

        if (only_deb_ostree) {
            /* Nasze pakiety maja "Installed-By: deb-ostree" */
            if (block.find("Installed-By: deb-ostree") == std::string::npos) continue;
        }

        /* Wczytaj liste plikow z dpkg/info/<pkg>.list (jesli istnieje) */
        /* rootfs nie jest znane tutaj -- zaladujemy przez dpkg_list_path z zewnatrz */

        result.push_back(std::move(pkg));
    }
    return result;
}

std::vector<InstalledPackage> load(const std::string& rootfs_path) {
    auto blocks = load_raw_blocks(dpkg_status_path(rootfs_path));
    auto result = load_from_blocks(blocks, true /* only deb-ostree */);

    /* Wczytaj liste plikow z /var/lib/dpkg/info/<pkg>.list */
    for (auto& pkg : result) {
        std::string list_file = dpkg_list_path(rootfs_path, pkg.name);
        std::ifstream lf(list_file);
        if (!lf.is_open()) continue;
        std::string line;
        while (std::getline(lf, line)) {
            line = trim(line);
            if (!line.empty()) pkg.files.push_back(line);
        }
    }

    return result;
}

std::vector<InstalledPackage> load_all(const std::string& rootfs_path) {
    auto blocks = load_raw_blocks(dpkg_status_path(rootfs_path));
    return load_from_blocks(blocks, false /* wszystkie */);
}

void upsert(const std::string& rootfs_path, const InstalledPackage& pkg) {
    std::string status_path = dpkg_status_path(rootfs_path);
    std::string info_dir    = dpkg_info_dir(rootfs_path);

    fs::create_directories(fs::path(status_path).parent_path());
    fs::create_directories(info_dir);

    /* Zaktualizuj dpkg/status */
    auto blocks = load_raw_blocks(status_path);
    blocks[pkg.name] = serialize_dpkg_block(pkg);
    write_status_atomic(status_path, blocks);

    /* Zapisz liste plikow do /var/lib/dpkg/info/<pkg>.list */
    if (!pkg.files.empty()) {
        std::string list_path = dpkg_list_path(rootfs_path, pkg.name);
        std::ofstream lf(list_path, std::ios::trunc);
        for (auto& f : pkg.files) lf << f << "\n";
    }

    log::debug("status_db: upsert " + pkg.name + " " + pkg.version +
               " -> /var/lib/dpkg/status");
}

void remove(const std::string& rootfs_path, const std::string& name) {
    std::string status_path = dpkg_status_path(rootfs_path);
    std::string info_dir    = dpkg_info_dir(rootfs_path);

    auto blocks = load_raw_blocks(status_path);

    /* Zamiast usuwac -- ustawiamy Status: deinstall ok config-files
     * (standard dpkg dla usuniętego pakietu) */
    auto it = blocks.find(name);
    if (it != blocks.end()) {
        /* Zamien Status na "deinstall ok config-files" */
        std::string& block = it->second;
        size_t pos = block.find("Status: ");
        if (pos != std::string::npos) {
            size_t end = block.find('\n', pos);
            block.replace(pos, end - pos,
                          "Status: deinstall ok config-files");
        }
        /* Usun "Installed-By: deb-ostree" */
        size_t ib = block.find("Installed-By: deb-ostree\n");
        if (ib != std::string::npos) block.erase(ib, 25);
    }

    write_status_atomic(status_path, blocks);

    /* Usun pliki info */
    std::error_code ec;
    for (auto& ext : {".list", ".md5sums", ".conffiles"}) {
        fs::remove(info_dir + "/" + name + ext, ec);
    }

    log::debug("status_db: remove " + name + " z /var/lib/dpkg/status");
}

bool is_installed(const std::string& rootfs_path, const std::string& name) {
    auto blocks = load_raw_blocks(dpkg_status_path(rootfs_path));
    auto it = blocks.find(name);
    if (it == blocks.end()) return false;
    /* Sprawdz "Status: install ok installed" i "Installed-By: deb-ostree" */
    return it->second.find("install ok installed") != std::string::npos &&
           it->second.find("Installed-By: deb-ostree") != std::string::npos;
}

} // namespace debostree::statusdb
