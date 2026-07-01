#pragma once
/*
 * deb-ostree -- solv_pool.h
 * Wrapper C++ na libsolv -- zastepuje resolver zaleznosci apt-get.
 *
 * libsolv (https://github.com/openSUSE/libsolv) to biblioteka SAT-solver
 * uzywana produkcyjnie przez zypper/dnf do rozwiazywania zaleznosci
 * pakietow. Wspiera natywnie format Debian/dpkg (rozszerzenie "libsolv-ext"
 * z funkcjami repo_add_debpackages, pool_installable itd.) -- to jest ta
 * czesc libsolv ktora pozwala parsowac relacje wersji w stylu Debiana
 * (">= 1.2.3", "<< 2.0") bez wlasnego parsera skladni zaleznosci.
 *
 * Przeplyw uzycia:
 *   1. SolvPool::create()                        -- nowa, pusta pula
 *   2. pool.add_repo_from_index(apt::RepoIndex)   -- dla kazdego mirror/suite
 *   3. pool.resolve_install({"vim", "htop"})      -- zwraca pelna liste
 *      pakietow do zainstalowania (z transytywnymi zaleznosciami)
 *
 * Ten modul ZASTEPUJE apt-get/libapt-pkg jako "silnik rozwiazywania
 * zaleznosci" w deb-ostree -- DebLayer (przepisany) uzywa SolvPool +
 * DebFetcher + DebArchive, bez wywolywania apt/dpkg w ogole.
 *
 * Wersja: 0.0.1
 */

#include "apt_repo_index.h"

#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solver.h>
#include <solv/transaction.h>

#include <string>
#include <vector>
#include <stdexcept>

namespace debostree::solv {

/* Wyjatek niosacy blad libsolv (np. nierozwiazywalne zaleznosci, konflikt
 * pakietow) -- tresc bledu zawiera pelny opis problemow zglaszanych przez
 * solver (np. "nothing provides libfoo needed by bar"). */
class SolvError : public std::runtime_error {
public:
    explicit SolvError(const std::string& what) : std::runtime_error(what) {}
};

/* Wynik rozwiazania zaleznosci -- jeden pakiet do zainstalowania, z pelnymi
 * metadanymi potrzebnymi do pobrania pliku .deb z mirror. */
struct ResolvedPackage {
    std::string name;
    std::string version;
    std::string filename;  /* sciezka relatywna w mirror (z Packages "Filename:") */
    std::string sha256;
    uint64_t    size = 0;
};

/*
 * SolvPool zarzadza pula libsolv (Pool*) zyciem rownym calej operacji
 * resolwowania -- RAII na pool_free dzieje sie automatycznie przy
 * zwalnianiu puli.
 */
class SolvPool {
public:
    /* Tworzy nowa, pusta pule skonfigurowana dla architektury "amd64"
     * (jedyna wspierana na tym etapie -- patrz ROADMAP dla multi-arch). */
    static SolvPool create();

    ~SolvPool();
    SolvPool(const SolvPool&) = delete;
    SolvPool& operator=(const SolvPool&) = delete;
    SolvPool(SolvPool&& other) noexcept;
    SolvPool& operator=(SolvPool&& other) noexcept;

    /*
     * Dodaje repozytorium do puli na podstawie sparsowanego RepoIndex
     * (apt::RepoIndex -- wynik apt_repo_index.h). repo_name to etykieta
     * dla diagnostyki (np. "trixie-main"), nie wplywa na rozwiazywanie.
     *
     * Wewnetrznie konwertuje kazdy apt::PackageEntry na format Solvable
     * libsolv, parsujac pola Depends/Conflicts/Provides przez API libsolv
     * dedykowane dla skladni Debian (libsolv ma wlasny parser relacji
     * wersji ">=", "<=", "=" zgodny z polityka Debiana, niezalezny od apt).
     */
    void add_repo_from_index(const apt::RepoIndex& index, const std::string& repo_name);

    /*
     * Rozwiazuje zaleznosci dla listy nazw pakietow do zainstalowania.
     * Zwraca PELNA liste pakietow (wlacznie z podanymi i wszystkimi ich
     * transytywnymi zaleznosciami) w kolejnosci bezpiecznej do instalacji.
     *
     * Rzuca SolvError jesli solver nie znajdzie rozwiazania (np. konflikt,
     * brak pakietu w pulach, niespelnialne ograniczenie wersji).
     */
    std::vector<ResolvedPackage> resolve_install(const std::vector<std::string>& package_names);

    /*
     * Rozwiazuje "co zostanie usuniete jesli usuniemy te pakiety" --
     * libsolv automatycznie uwzgledni pakiety ktore zalezaly WYLACZNIE od
     * usuwanych, ale NIE usuwa pakietow ktore sa tez wymagane przez cos innego.
     */
    std::vector<std::string> resolve_remove(const std::vector<std::string>& package_names);

    ::Pool* raw() { return pool_; }

private:
    SolvPool() = default;
    ::Pool* pool_ = nullptr;

    /* Buduje string komunikatu bledu z WSZYSTKICH problemow solvera
     * (nie tylko pierwszego) -- jeden SolvError z pelna lista. */
    static std::string format_solver_problems(::Solver* solver);
};

} // namespace debostree::solv
