#pragma once
/*
 * deb-ostree -- types.h
 * Wspolne typy domenowe uzywane w calym deb-ostree.
 *
 * Wersja: 0.2.0
 *   Pelna kompatybilnosc sciezek z dpkg/apt:
 *     - /var/lib/dpkg/status       -- baza zainstalowanych pakietow (primary)
 *     - /var/lib/dpkg/info/        -- pliki pakietow i skrypty maintainer
 *     - /var/lib/apt/lists/        -- cache indeksow Packages
 *     - /etc/apt/sources.list      -- zrodla apt (+ sources.list.d/)
 *     - /etc/apt/trusted.gpg.d/    -- klucze GPG
 *
 * deb-ostree nie wywoluje apt ani dpkg jako procesow.
 * Czyta/pisze te pliki bezposrednio (wlasny parser RFC822, wlasny resolver).
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree {

enum class LayerOp { Install, Uninstall, Override };

struct PackageLayer {
    std::string name;
    std::string version;
    LayerOp     op = LayerOp::Install;
};

struct Deployment {
    std::string id;
    uint64_t    timestamp = 0;
    std::string osname;
    std::string checksum;
    int         serial  = 0;
    bool        booted  = false;
    bool        staged  = false;
    bool        pinned  = false;
    std::string origin_refspec;
    std::vector<PackageLayer> layered_packages;
};

struct TransactionResult {
    bool        success         = false;
    std::string new_checksum;
    std::string error_message;
    bool        requires_reboot = false;
};

struct Config {
    std::string sysroot_path     = "/";
    std::string ostree_repo_path = "/ostree/repo";
    std::string osname           = "debian";

    /* Katalog roboczy transakcji deb-ostree (jedyna wlasna sciezka) */
    std::string overlay_work_dir = "/var/lib/deb-ostree/overlay-work";

    /* === Sciezki identyczne z apt/dpkg === */

    /* Cache indeksow Packages -- identyczny z /var/lib/apt/lists/
     * Format plikow: <host>_dists_<suite>_<comp>_binary-<arch>_Packages */
    std::string apt_lists_path = "/var/lib/apt/lists";

    /* Plik konfiguracyjny zrodel apt -- czytany przez deb-ostree,
     * NIE ma wlasnej listy source_N. Zrodla brane z systemu. */
    std::string apt_sources_list = "/etc/apt/sources.list";

    /* Katalog dodatkowych plikow sources.list.d/ */
    std::string apt_sources_dir  = "/etc/apt/sources.list.d";

    /* Klucze GPG -- identyczny z apt */
    std::string keyring_dir = "/etc/apt/trusted.gpg.d";

    /* Architektura -- domyslnie amd64, nadpisywalna przez --arch */
    std::string arch = "amd64";

    /* Tryb confext dla plikow /etc (#19) */
    std::string confext_mode = "none";

    /* Lista zrodel apt -- wypelniana przez sources_parser z apt_sources_list
     * i apt_sources_dir. Moze byc nadpisana przez [apt] -> source_N w .hk
     * dla srodowisk bez zainstalowanego apt (np. builder/CI). */
    std::vector<std::string> apt_sources;
};

} // namespace debostree
