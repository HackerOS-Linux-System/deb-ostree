#pragma once
/*
 * deb-ostree -- types.h
 * Wspolne typy domenowe uzywane w calym deb-ostree.
 * Analog koncepcyjny "rpmostree-types.h" z rpm-ostree.
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree {

/* Operacja wykonana na pakiecie warstwowym. */
enum class LayerOp {
    Install,
    Uninstall,
    Override,
};

/*
 * Pojedynczy pakiet .deb nalozony jako warstwa na obraz bazowy OSTree.
 * Lista PackageLayer jest zapisywana w pliku "origin" deploymentu (w kluczu
 * [deb-ostree] / layered-packages), zeby po upgrade bazy moc je ponownie
 * nalozyc -- analogicznie do rpm-ostree "packages" w origin.
 */
struct PackageLayer {
    std::string name;       /* nazwa pakietu, np. "vim"            */
    std::string version;    /* faktycznie zainstalowana wersja      */
    LayerOp     op = LayerOp::Install;
};

/*
 * Jeden "deployment" = jeden zatwierdzony, bootowalny stan systemu.
 * Kazdy deployment ma unikalny checksum OSTree, numer seryjny w bootloaderze
 * (serial) i liste nalozonych pakietow warstwowych.
 *
 * Porzadek deploymentow w OstreeSysroot odpowiada porzadkowi w menu boot:
 *   index 0 = domyslny (aktualny lub staged)
 *   index 1 = poprzedni (dostepny jako rollback)
 *   index 2+ = starsze (zwykle usuwa je "cleanup")
 */
struct Deployment {
    std::string id;              /* np. "debian-a3f1b2c4d5.0"       */
    std::string osname;          /* np. "debian"                     */
    std::string checksum;        /* commit OSTree (sha256, 64 znaki) */
    int         serial  = 0;     /* numer seryjny deploymentu        */
    bool        booted  = false; /* true = aktualnie zabootowany     */
    bool        staged  = false; /* true = czeka na nastepny reboot  */
    bool        pinned  = false; /* true = chroniony przed cleanup   */
    std::string origin_refspec;  /* np. "deb-ostree-oci:debian/bookworm:latest" */
    std::vector<PackageLayer> layered_packages;
};

/*
 * Wynik operacji commitujacych nowy deployment (install/upgrade/rebase/deploy).
 * Zamiast rzucac wyjatkami z warstwy komend, zwracamy ten struct -- wyjatki
 * sa uzywane tylko w wewnetrznych modulach (OstreeError, std::runtime_error).
 */
struct TransactionResult {
    bool        success          = false;
    std::string new_checksum;
    std::string error_message;
    bool        requires_reboot  = false;
};

/*
 * Konfiguracja wczytywana z /etc/deb-ostree/deb-ostree.conf.
 * Wartosci domyslne odpowiadaja standardowemu systemowi Debian z bootc/OSTree.
 */
struct Config {
    std::string sysroot_path     = "/";
    std::string ostree_repo_path = "/ostree/repo";
    std::string osname           = "debian";
    std::string overlay_work_dir = "/var/lib/deb-ostree/overlay-work";
    std::string apt_lists_path   = "/var/lib/deb-ostree/apt-cache";
    std::vector<std::string> apt_sources;
};

} // namespace debostree
