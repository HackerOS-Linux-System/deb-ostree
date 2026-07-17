#pragma once
/*
 * deb-ostree -- status_db.h
 * Baza zainstalowanych pakietow warstwowych.
 *
 * Wersja: 0.2.0
 *   Uzywamy /var/lib/dpkg/status jako PRIMARY store (format RFC822 dpkg).
 *   Lista plikow pakietu: /var/lib/dpkg/info/<pkg>.list (identycznie z dpkg).
 *   Skrypty maintainer:   /var/lib/dpkg/info/<pkg>.{preinst,postinst,...}
 *
 * deb-ostree NIE wywoluje dpkg -- czyta i pisze te pliki bezposrednio.
 * Dzieki temu "dpkg -l", "dpkg -L <pkg>", "apt list --installed" widza
 * pakiety zainstalowane przez deb-ostree bez zadnej synchronizacji.
 *
 * Format /var/lib/dpkg/status (RFC 822, identyczny z dpkg):
 *   Package: vim
 *   Status: install ok installed
 *   Architecture: amd64
 *   Version: 2:9.0.1378-2
 *   Installed-By: deb-ostree
 *   Description: ...
 *   (pusta linia oddziela rekordy)
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace debostree::statusdb {

struct InstalledPackage {
    std::string name;
    std::string version;
    std::string architecture;
    std::string maintainer;
    std::string description;
    std::string depends;
    std::string pre_depends;
    std::string provides;
    std::string section;
    std::string priority;
    uint64_t    installed_size = 0;
    std::vector<std::string> files;  /* z /var/lib/dpkg/info/<pkg>.list */
    std::string status = "install ok installed";
};

/* Sciezki (identyczne z dpkg) */
std::string dpkg_status_path  (const std::string& rootfs_path);
std::string dpkg_info_dir     (const std::string& rootfs_path);
std::string dpkg_list_path    (const std::string& rootfs_path, const std::string& pkg);

/* Wczytuje wszystkie pakiety warstwowe deb-ostree z dpkg/status.
 * Kryterium: wpis ma pole "Installed-By: deb-ostree" */
std::vector<InstalledPackage> load(const std::string& rootfs_path);

/* Wczytuje wszystkie pakiety z dpkg/status (wlacznie z obrazem bazowym).
 * Uzyteczne do wykrywania konfliktow przez SolvPool::add_installed_packages(). */
std::vector<InstalledPackage> load_all(const std::string& rootfs_path);

/* Zapisuje/aktualizuje pakiet w dpkg/status + tworzy dpkg/info/<pkg>.list */
void upsert(const std::string& rootfs_path, const InstalledPackage& pkg);

/* Usuwa pakiet z dpkg/status i dpkg/info/<pkg>.* */
void remove(const std::string& rootfs_path, const std::string& name);

/* Czy pakiet jest zainstalowany przez deb-ostree? */
bool is_installed(const std::string& rootfs_path, const std::string& name);

} // namespace debostree::statusdb
