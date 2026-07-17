#pragma once
/*
 * deb-ostree -- confext.h  [NOWY v0.2.0]
 * Integracja z systemd-confext (systemd.confext(7)) dla pakietow
 * instalujacych pliki konfiguracyjne do /etc (#19).
 *
 * Problem: pakiety .deb moga instalowac pliki do /etc ktore w modelu
 * immutable (OSTree) powinny byc zarzadzane przez:
 *   - systemd-confext: nakładki konfiguracji w /etc/extensions/
 *   - lub konfigi w /usr/etc/ (vendor prefix)
 *
 * Rozwiazanie: po rozpakowaniu paczki deb_layer pyta confext.h czy
 * zainstalowane pliki /etc/[*] powinny byc przeniesione do osobnej
 * warstwy confext, ktora jest aktywowana przez systemd-confext merge.
 *
 * Tryby:
 *   ConfextMode::None     -- tradycyjne /etc w overlayfs (domyslny)
 *   ConfextMode::Separate -- /etc pliki do /etc/extensions/<pkg>/ (confext)
 *   ConfextMode::UsrEtc   -- /etc pliki do /usr/etc/ (vendor prefix)
 *
 * Wersja: 0.2.0
 */

#include <string>
#include <vector>

namespace debostree::confext {

enum class ConfextMode {
    None,      /* Tradycyjne /etc w overlayfs (kompatybilne wstecz) */
    Separate,  /* Pliki /etc do systemd-confext extension w /etc/extensions/<pkg>/ */
    UsrEtc     /* Pliki /etc do /usr/etc/ (FHS vendor prefix) */
};

struct ConfextResult {
    std::string extension_dir; /* gdzie trafily pliki /etc (jesli Separate) */
    std::vector<std::string> moved_files; /* lista przeniesionych plikow */
    bool activated = false; /* czy wywolano systemd-confext merge */
};

/*
 * Po rozpakowaniu pakietu, przetwarza pliki etc_files (sciezki wzgledne
 * do rootfs, zaczynajace sie od /etc/) zgodnie z trybem confext_mode.
 *
 * Dla ConfextMode::Separate:
 *   Tworzy /etc/extensions/<pkg_name>/ z plikami .confext per FHS:
 *     /etc/extensions/<pkg>
 *       usr/
 *         lib/
 *           extension-release.d/
 *             extension-release.<pkg>   <- metadane confext
 *       etc/
 *         <plik konfiguracyjny>
 *
 * rootfs_path: sciezka do merged_dir sesji overlay
 * pkg_name:    nazwa pakietu
 * etc_files:   lista plikow /etc/[*] zainstalowanych przez pakiet
 * mode:        tryb confext
 */
ConfextResult process_etc_files(const std::string& rootfs_path,
                                 const std::string& pkg_name,
                                 const std::vector<std::string>& etc_files,
                                 ConfextMode mode);

/*
 * Sprawdza czy systemd-confext jest dostepny i wywoluje
 * "systemd-confext merge" lub "systemd-confext refresh"
 * zeby aktywowac nowe extensions.
 */
bool activate_confext(const std::string& rootfs_path);

/*
 * Parsuje tryb z nazwy: "none", "separate", "usr-etc".
 * Rzuca std::invalid_argument dla nieznanej nazwy.
 */
ConfextMode parse_mode(const std::string& name);

/*
 * Sprawdza lista plikow i zwraca te ktore nalezy do /etc.
 */
std::vector<std::string> filter_etc_files(const std::vector<std::string>& files);

} // namespace debostree::confext
