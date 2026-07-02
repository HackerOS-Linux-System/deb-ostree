#pragma once
/*
 * deb-ostree -- status_db.h
 * Lokalna baza zainstalowanych pakietow -- zastepuje /var/lib/dpkg/status
 * i /var/lib/dpkg/info/<pkg>.list, ktorych deb-ostree NIE uzywa (bo nie ma
 * dpkg w ogole).
 *
 * Format: jeden plik JSON-lines w merged_dir/var/lib/deb-ostree/status.db,
 * jedna linia per zainstalowany pakiet:
 *
 *   {"name":"vim","version":"2:9.0.1378-2","files":["/usr/bin/vim", ...]}
 *
 * Uzywane przez DebLayer do:
 *   - is_installed() -- sprawdzenie czy pakiet jest juz w bazie
 *   - remove_packages() -- odczytanie listy plikow nalezacych do pakietu
 *
 * Wersja: 0.1.0
 */

#include <string>
#include <vector>

namespace debostree::statusdb {

struct InstalledPackage {
    std::string name;
    std::string version;
    std::vector<std::string> files; /* sciezki absolutne wewnatrz rootfs */
};

/* Wczytuje baze z rootfs_path/var/lib/deb-ostree/status.db. Zwraca pusta
 * liste jesli plik nie istnieje. */
std::vector<InstalledPackage> load(const std::string& rootfs_path);

/* Zapisuje cala liste z powrotem do pliku (nadpisuje). */
void save(const std::string& rootfs_path, const std::vector<InstalledPackage>& packages);

/* Dodaje lub aktualizuje wpis pakietu w bazie. */
void upsert(const std::string& rootfs_path, const InstalledPackage& pkg);

/* Usuwa wpis pakietu z bazy. Nie usuwa plikow z dysku -- to robi caller. */
void remove(const std::string& rootfs_path, const std::string& package_name);

/* Sprawdza czy pakiet jest w bazie. */
bool is_installed(const std::string& rootfs_path, const std::string& package_name);

} // namespace debostree::statusdb
