#pragma once
/*
 * deb-ostree -- maintainer_scripts.h  [NOWY v0.1.0]
 * Persystencja skryptów maintainer (preinst/postinst/prerm/postrm) pakietów.
 *
 * Problem (#1 z listy): przy usuwaniu pakietów deb_layer.cpp nie może
 * wykonać prerm/postrm bo nie ma dostępu do oryginalnego .deb.
 *
 * Rozwiązanie: po instalacji zapisujemy skrypty do:
 *   <rootfs>/var/lib/deb-ostree/info/<pkg>.preinst
 *   <rootfs>/var/lib/deb-ostree/info/<pkg>.postinst
 *   <rootfs>/var/lib/deb-ostree/info/<pkg>.prerm
 *   <rootfs>/var/lib/deb-ostree/info/<pkg>.postrm
 *
 * Ten sam layout co /var/lib/dpkg/info/ -- kompatybilny z dpkg.
 *
 * Wersja: 0.2.0
 */

#include <string>

namespace debostree::maintscripts {

/*
 * Zapisuje skrypt maintainer dla pakietu do katalogu info.
 * Jeśli script_content jest pusty -- nie zapisuje nic (brak skryptu = normalne).
 * script_type: "preinst", "postinst", "prerm", "postrm"
 */
void save_script(const std::string& rootfs_path,
                 const std::string& package_name,
                 const std::string& script_type,
                 const std::string& script_content);

/*
 * Odczytuje skrypt maintainer dla pakietu.
 * Zwraca pusty string jeśli plik nie istnieje.
 */
std::string load_script(const std::string& rootfs_path,
                         const std::string& package_name,
                         const std::string& script_type);

/*
 * Usuwa wszystkie skrypty pakietu z katalogu info.
 * Wywoływać po postrm (końcowe sprzątanie po uninstall).
 */
void remove_scripts(const std::string& rootfs_path,
                    const std::string& package_name);

/*
 * Zwraca ścieżkę do katalogu info.
 */
std::string info_dir(const std::string& rootfs_path);

} // namespace debostree::maintscripts
