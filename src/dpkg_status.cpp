#include "../cmd/dpkg_status.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace debostree::dpkg_compat {

static std::string dpkg_status_path(const std::string& rootfs_path) {
    return rootfs_path + "/var/lib/dpkg/status";
}

/* Parsuje /var/lib/dpkg/status na bloki (mapa: nazwa -> blok RFC822) */
static std::unordered_map<std::string, std::string>
load_dpkg_status_blocks(const std::string& path) {
    std::unordered_map<std::string, std::string> blocks;
    std::ifstream f(path);
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
        if (line.rfind("Package: ", 0) == 0) {
            current_name = line.substr(9);
        }
    }
    if (!current_name.empty()) blocks[current_name] = current_block;
    return blocks;
}

/* Buduje blok dpkg/status dla pakietu deb-ostree */
static std::string build_dpkg_block(const statusdb::InstalledPackage& pkg,
                                     const deb::ControlInfo* ctrl) {
    std::ostringstream oss;
    oss << "Package: " << pkg.name << "\n";
    oss << "Status: install ok installed\n";

    if (ctrl) {
        if (!ctrl->maintainer.empty())
            oss << "Maintainer: " << ctrl->maintainer << "\n";
        if (!ctrl->architecture.empty())
            oss << "Architecture: " << ctrl->architecture << "\n";
        if (!ctrl->pre_depends.empty())
            oss << "Pre-Depends: " << ctrl->pre_depends << "\n";
        if (!ctrl->depends.empty())
            oss << "Depends: " << ctrl->depends << "\n";
        if (!ctrl->conflicts.empty())
            oss << "Conflicts: " << ctrl->conflicts << "\n";
        if (!ctrl->provides.empty())
            oss << "Provides: " << ctrl->provides << "\n";
        if (!ctrl->replaces.empty())
            oss << "Replaces: " << ctrl->replaces << "\n";
        if (!ctrl->breaks.empty())
            oss << "Breaks: " << ctrl->breaks << "\n";
    } else {
        oss << "Architecture: amd64\n";
    }

    oss << "Version: " << pkg.version << "\n";
    oss << "Installed-By: deb-ostree\n";

    if (ctrl && !ctrl->description.empty()) {
        oss << "Description: " << ctrl->description << "\n";
    } else {
        oss << "Description: (installed by deb-ostree)\n";
    }

    return oss.str();
}

void sync_dpkg_status(const std::string& rootfs_path,
                      const std::vector<statusdb::InstalledPackage>& packages,
                      const std::vector<deb::ControlInfo>& control_infos)
{
    std::string status_path = dpkg_status_path(rootfs_path);
    fs::create_directories(fs::path(status_path).parent_path());

    /* Wczytaj istniejący status (pakiety z obrazu bazowego) */
    auto existing_blocks = load_dpkg_status_blocks(status_path);

    /* Zbuduj mapę control_infos */
    std::unordered_map<std::string, const deb::ControlInfo*> ctrl_map;
    for (auto& ci : control_infos) ctrl_map[ci.package] = &ci;

    /* Zestaw pakietów deb-ostree */
    std::unordered_set<std::string> our_names;
    for (auto& p : packages) our_names.insert(p.name);

    /* Zapisz nowy status */
    std::ofstream f(status_path, std::ios::trunc);

    /* Najpierw pakiety bazowe (z obrazu, nie zarządzane przez nas) */
    for (auto& [name, block] : existing_blocks) {
        if (our_names.count(name) == 0) {
            f << block << "\n";
        }
    }

    /* Nasze pakiety warstwowe */
    for (auto& pkg : packages) {
        const deb::ControlInfo* ctrl = nullptr;
        auto it = ctrl_map.find(pkg.name);
        if (it != ctrl_map.end()) ctrl = it->second;

        f << build_dpkg_block(pkg, ctrl) << "\n";
    }

    log::debug("dpkg_compat: zaktualizowano " + status_path +
               " (" + std::to_string(packages.size()) + " pakietów deb-ostree)");
}

void remove_from_dpkg_status(const std::string& rootfs_path,
                              const std::string& package_name)
{
    std::string status_path = dpkg_status_path(rootfs_path);
    auto blocks = load_dpkg_status_blocks(status_path);
    blocks.erase(package_name);

    std::ofstream f(status_path, std::ios::trunc);
    for (auto& [name, block] : blocks) {
        f << block << "\n";
    }
    log::debug("dpkg_compat: usunięto " + package_name + " z " + status_path);
}

} // namespace debostree::dpkg_compat
