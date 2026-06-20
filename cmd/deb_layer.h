#pragma once
/*
 * deb-ostree -- deb_layer.h
 * Instalacja/usuniecie pakietow .deb wewnatrz zamontowanego overlayfs przy
 * uzyciu prawdziwego apt-get i dpkg z opcja "--root=<merged_dir>".
 *
 * Filozofia: NIE reimplementujemy resolvera zaleznosci .deb. Delegujemy do
 * apt, ktory ma pelna wiedze o dostepnosci pakietow, zaleznosci przechodnie,
 * wersje, konflikty i pre-depends. deb-ostree jest "orchestratorem transakcji",
 * a apt/dpkg sa "silnikiem pakietow" -- ta sama podzial jak rpm-ostree/libdnf.
 *
 * Wersja: 0.0.1
 */

#include "types.h"
#include "overlay_manager.h"

#include <string>
#include <vector>

namespace debostree {

class DebLayer {
public:
    explicit DebLayer(Config cfg);

    /*
     * Pobiera pakiety z apt, rozwiazuje zaleznosci i instaluje je w
     * session.merged_dir przez "apt-get install --root=merged_dir".
     * Najpierw wykonuje --simulate, by wyciagnac pelna liste pakietow
     * ktore faktycznie zostana zainstalowane (wlacznie z zaleznosci).
     * Zwraca liste PackageLayer z nazwa i wersja kazdego z nich.
     */
    std::vector<PackageLayer> install_packages(const OverlaySession&          session,
                                               const std::vector<std::string>& names);

    /*
     * Usuwa pakiety z session.merged_dir przez "apt-get remove --root=...".
     * Nie usuwa zaleznosci ktore nie sa juz potrzebne (autoremove nie jest
     * wywolywany automatycznie -- uzytkownik moze uzyc osobno).
     */
    void remove_packages(const OverlaySession&          session,
                         const std::vector<std::string>& names);

    /*
     * Odswieza indeks pakietow w merged_dir ("apt-get update --root=...").
     * Potrzebne zanim mozna zainstalowac pakiet z nowo dodanego zrodla.
     */
    void refresh_package_index(const OverlaySession& session);

    /* Sprawdza przez dpkg -s czy pakiet jest zainstalowany w podanym rootfs. */
    bool is_installed(const std::string& rootfs_path, const std::string& pkg_name);

private:
    Config cfg_;

    /*
     * Zwraca prefiks "env VAR=val ..." zapewniajacy nieinteraktywne zachowanie
     * apt i dpkg (DEBIAN_FRONTEND=noninteractive, bez list zmian, bez
     * triggerow apt.daily.*, bez man-db/doc-base w kontekscie immutable image).
     */
    std::vector<std::string> apt_env_prefix() const;
};

} // namespace debostree
