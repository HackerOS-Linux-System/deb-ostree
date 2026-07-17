#pragma once
/*
 * deb-ostree -- dpkg_status.h  [NOWY v0.1.0]
 * Zapis do /var/lib/dpkg/status -- kompatybilność z dpkg/apt.
 *
 * Problem (#17): status_db używa własnego formatu JSON-lines. Narzędzia
 * systemowe (dpkg -l, apt list --installed, debconf) nie widzą pakietów
 * zainstalowanych przez deb-ostree.
 *
 * Rozwiązanie: po każdej operacji install/remove zapisujemy równolegle
 * do /var/lib/dpkg/status w formacie RFC822 zgodnym z dpkg.
 *
 * Format dpkg status (jeden blok na pakiet):
 *
 *   Package: vim
 *   Status: install ok installed
 *   Priority: optional
 *   Section: editors
 *   Installed-Size: 3456
 *   Maintainer: ...
 *   Architecture: amd64
 *   Version: 2:9.0.1378-2
 *   Depends: vim-common (= 2:9.0.1378-2)
 *   Description: Vi IMproved - enhanced vi editor
 *    Vi IMproved is an almost compatible version of the UNIX editor Vi.
 *
 * Wersja: 0.2.0
 */

#include <string>
#include <vector>
#include "../cmd/status_db.h"
#include "../cmd/deb_archive.h"

namespace debostree::dpkg_compat {

/*
 * Synchronizuje /var/lib/dpkg/status w rootfs_path ze status_db.
 * Pakiety z status_db mają Status: "install ok installed".
 * Reszta wpisów dpkg/status (pakiety bazowe z obrazu OSTree) jest
 * zachowywana niezmieniona.
 *
 * rootfs_path: ścieżka do merged_dir sesji overlay (lub deploymentu)
 * packages:    lista zainstalowanych pakietów z status_db
 * control_infos: metadane control z .deb (maintainer, description, section...)
 */
void sync_dpkg_status(const std::string& rootfs_path,
                      const std::vector<statusdb::InstalledPackage>& packages,
                      const std::vector<deb::ControlInfo>& control_infos);

/*
 * Usuwa wpis pakietu z /var/lib/dpkg/status.
 * Używane przy deb-ostree remove.
 */
void remove_from_dpkg_status(const std::string& rootfs_path,
                              const std::string& package_name);

} // namespace debostree::dpkg_compat
