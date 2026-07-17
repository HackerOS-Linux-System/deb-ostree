#include "../cmd/maintainer_scripts.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace debostree::maintscripts {

std::string info_dir(const std::string& rootfs_path) {
    /* /var/lib/dpkg/info/ -- identycznie z dpkg */
    return rootfs_path + "/var/lib/dpkg/info";
}

static std::string script_path(const std::string& rootfs_path,
                                const std::string& package_name,
                                const std::string& script_type) {
    return info_dir(rootfs_path) + "/" + package_name + "." + script_type;
}

void save_script(const std::string& rootfs_path,
                 const std::string& package_name,
                 const std::string& script_type,
                 const std::string& script_content)
{
    if (script_content.empty()) return;

    std::error_code ec;
    fs::create_directories(info_dir(rootfs_path), ec);

    std::string path = script_path(rootfs_path, package_name, script_type);
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) {
        log::warn("maintscripts: nie mozna zapisac " + path);
        return;
    }
    f << script_content;
    fs::permissions(path,
        fs::perms::owner_read  | fs::perms::owner_write  | fs::perms::owner_exec |
        fs::perms::group_read  | fs::perms::group_exec   |
        fs::perms::others_read | fs::perms::others_exec,
        ec);

    log::debug("maintscripts: /var/lib/dpkg/info/" + package_name + "." + script_type);
}

std::string load_script(const std::string& rootfs_path,
                         const std::string& package_name,
                         const std::string& script_type)
{
    std::string path = script_path(rootfs_path, package_name, script_type);
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

void remove_scripts(const std::string& rootfs_path,
                    const std::string& package_name)
{
    std::error_code ec;
    /* Usun skrypty -- zachowaj .list i .md5sums (dpkg ich nie usuwa przy remove) */
    for (auto& t : {"preinst", "postinst", "prerm", "postrm", "config"}) {
        fs::remove(script_path(rootfs_path, package_name, t), ec);
    }
    log::debug("maintscripts: usunieto skrypty " + package_name + " z dpkg/info/");
}

} // namespace debostree::maintscripts
