#pragma once
/*
 * deb-ostree -- pool_builder.h  [NOWY v0.2.0]
 * Wspólna funkcja budowania SolvPool z indeksów apt.
 *
 * Problem (#13): ta sama logika (fetch InRelease → GPG → cache → build pool)
 * była duplikowana w deb_layer.cpp, cmd_upgrade.cpp i cmd_search.cpp.
 * Jedna poprawka (np. retry, weryfikacja SHA256) wymagała zmiany w trzech miejscach.
 *
 * Rozwiązanie: centralna funkcja build_solv_pool() w pool_builder.h/cpp
 * używana przez wszystkie trzy moduły.
 *
 * Wersja: 0.2.0
 */

#include "solv_pool.h"
#include "types.h"
#include "progress.h"

#include <string>

namespace debostree {

/*
 * Buduje SolvPool wypełnioną indeksami ze wszystkich cfg.apt_sources.
 * Obsługuje: cache indeksów (TTL 24h), weryfikację GPG InRelease,
 * SHA256 indeksu przed dekompresją, @System z zainstalowanymi pakietami.
 *
 * cfg:          konfiguracja (apt_sources, arch, keyring_dir itd.)
 * bar:          pasek postępu (metoda spin/tick są wywoływane wewnątrz)
 * rootfs_path:  jeśli niepusta, ładuje zainstalowane pakiety do @System
 *               (wykrywanie konfliktów z istniejącymi pakietami)
 *
 * Rzuca std::runtime_error gdy brak apt_sources lub błąd krytyczny.
 */
solv::SolvPool build_solv_pool(const Config& cfg,
                                progress::ProgressBar& bar,
                                const std::string& rootfs_path = "");

} // namespace debostree
