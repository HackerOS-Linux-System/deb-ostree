#pragma once
/*
 * deb-ostree -- deb_layer.h
 * DebLayer wykonuje instalacje/usuniecie pakietow .deb wewnatrz zamontowanego
 * overlay (OverlaySession::merged_dir) -- CALKOWICIE BEZ uzycia apt/dpkg.
 *
 * Przeplyw "deb-ostree install <pkg>":
 *   1. SolvPool.add_repo_from_index() dla kazdego skonfigurowanego sources
 *      (indeksy Packages pobrane przez DebFetcher, sparsowane przez
 *      apt::RepoIndex)
 *   2. SolvPool.resolve_install({"pkg"}) -- libsolv zwraca pelna liste
 *      pakietow do zainstalowania z transytywnymi zaleznosciami
 *   3. Dla kazdego pakietu na liscie: DebFetcher.fetch_deb_package()
 *      (pobranie .deb z weryfikacja SHA256 z indeksu)
 *   4. Dla kazdego pobranego .deb: DebArchive.open() + extract_data_to()
 *      (rozpakowanie zawartosci do merged_dir, bez dpkg -x)
 *   5. Skrypty maintainer (postinst itp.) sa wykonywane w chroot wzgledem
 *      merged_dir -- to JEDYNE miejsce gdzie nadal uruchamiamy proces
 *      zewnetrzny (sam skrypt pakietu, ktorego tresc nie jest pod nasza
 *      kontrola), nie apt/dpkg.
 *   6. Wpis do lokalnej "bazy zainstalowanych pakietow" (status_db.h) --
 *      zastepuje /var/lib/dpkg/status, ktorej tu nie uzywamy.
 *
 * Wersja: 0.1.0
 */

#include "types.h"
#include "overlay_manager.h"
#include "deb_fetcher.h"

#include <string>
#include <vector>

namespace debostree {

class DebLayer {
public:
    explicit DebLayer(Config cfg);

    /*
     * Rozwiazuje zaleznosci (libsolv), pobiera wszystkie potrzebne .deb
     * (z weryfikacja SHA256) i instaluje je (rozpakowanie + skrypty
     * maintainer) w session.merged_dir. Zwraca liste WSZYSTKICH pakietow
     * faktycznie zainstalowanych (podane + transytywne zaleznosci).
     */
    std::vector<PackageLayer> install_packages(const OverlaySession& session,
                                               const std::vector<std::string>& names);

    /*
     * Rozwiazuje "co usunac" (libsolv) i usuwa pliki nalezace do tych
     * pakietow z session.merged_dir wedlug listy plikow zapisanej w
     * status_db przy instalacji. Wykonuje skrypt "prerm"/"postrm" jesli
     * pakiet go ma.
     */
    void remove_packages(const OverlaySession& session,
                         const std::vector<std::string>& names);

    /*
     * Pobiera (przez DebFetcher) i parsuje (przez apt::RepoIndex) indeksy
     * Packages dla wszystkich skonfigurowanych apt_sources -- odpowiednik
     * "apt-get update", ale bez apt.
     */
    void refresh_package_index(const OverlaySession& session);

    /* Sprawdza przez status_db czy pakiet jest zainstalowany w danym rootfs. */
    bool is_installed(const std::string& rootfs_path, const std::string& package_name);

private:
    Config cfg_;
    deb::DebFetcher fetcher_;

    /* Wykonuje skrypt maintainer (np. "postinst") wewnatrz chroot wzgledem
     * session.merged_dir, jesli DebArchive go ma. To JEDYNE miejsce gdzie
     * DebLayer woła proces zewnetrzny ("chroot") -- konieczne bo skrypty
     * maintainer pakietow .deb sa dowolnym kodem powloki ktory zaklada
     * dzialanie wewnatrz docelowego systemu, nie da sie ich
     * "zinterpretowac" bez prawdziwego chroot.
     */
    void run_maintainer_script(const OverlaySession& session,
                               const std::string& package_name,
                               const std::string& script_content,
                               const std::string& script_name);
};

} // namespace debostree
