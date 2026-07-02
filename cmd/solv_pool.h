#pragma once
/*
 * deb-ostree -- solv_pool.h
 * Wrapper C++ na libsolv -- resolver zależności dla pakietów .deb.
 *
 * Wersja: 0.1.0 -- pełna obsługa OR-zależności, Breaks, Recommends,
 *                   multi-arch, resolve_upgrade, szczegółowa diagnostyka.
 *
 * Zmiany:
 *   - SolvPool::create(arch) -- architektura jako parametr
 *   - add_installed_packages() -- detekcja konfliktów z @System
 *   - resolve_upgrade() -- aktualizacja zainstalowanych pakietów
 *   - OR-zależności (A | B) przez REL_OR w libsolv
 *   - Breaks mapowane na SOLVABLE_CONFLICTS
 *   - Recommends jako słabe zależności (SOLVABLE_SUGGESTS)
 *   - format_solver_problems() -- szczegółowa diagnostyka z rozwiązaniami
 */

#include "apt_repo_index.h"
#include "status_db.h"

#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solver.h>
#include <solv/transaction.h>

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace debostree::solv {

class SolvError : public std::runtime_error {
public:
    explicit SolvError(const std::string& what) : std::runtime_error(what) {}
};

struct ResolvedPackage {
    std::string name;
    std::string version;
    std::string filename;
    std::string sha256;
    uint64_t    size = 0;
};

class SolvPool {
public:
    /*
     * Tworzy nową, pustą pulę dla danej architektury Debian
     * (amd64, arm64, armhf, i386, ...).
     * Ustawia pool_setdisttype(DISTTYPE_DEB) -- poprawne porównywanie wersji
     * z epochą i suffixem Debiana (np. "2:9.0.1378-2" > "9.0.1300-1").
     */
    static SolvPool create(const std::string& deb_arch = "amd64");

    ~SolvPool();
    SolvPool(const SolvPool&) = delete;
    SolvPool& operator=(const SolvPool&) = delete;
    SolvPool(SolvPool&& other) noexcept;
    SolvPool& operator=(SolvPool&& other) noexcept;

    /*
     * Dodaje repozytorium z sparsowanego RepoIndex.
     * Obsługuje:
     *   - Depends, Pre-Depends -> SOLVABLE_REQUIRES
     *   - Conflicts, Breaks    -> SOLVABLE_CONFLICTS
     *   - Provides             -> SOLVABLE_PROVIDES
     *   - Replaces             -> SOLVABLE_OBSOLETES
     *   - Recommends           -> SOLVABLE_SUGGESTS (słabe)
     *   - OR-zależności (A|B)  -> pool_rel2id(REL_OR, A, B)
     * Filtruje pakiety o architekturze niezgodnej z create(arch)
     * (zachowuje "all"/"noarch").
     */
    void add_repo_from_index(const apt::RepoIndex& index, const std::string& repo_name);

    /*
     * Ładuje zainstalowane pakiety jako repo @System.
     * Solver wykrywa konflikty między nowymi a zainstalowanymi.
     * Wywołać PO add_repo_from_index, PRZED resolve_*.
     */
    void add_installed_packages(const std::vector<statusdb::InstalledPackage>& installed);

    /*
     * Rozwiązuje zależności instalacji. Zwraca pełną listę pakietów do
     * pobrania i zainstalowania (z tranzytywnymi zależnościami) w kolejności
     * topologicznej bezpiecznej do instalacji.
     *
     * Polityki solvera:
     *   - ALLOW_DOWNGRADE = 0   (nigdy nie obniżaj wersji)
     *   - BEST_OBEY_POLICY = 1  (preferuj najnowszą wersję)
     *   - IGNORE_RECOMMENDED = 1 (Recommends opcjonalne)
     *   - ALLOW_UNINSTALL = 0   (błąd zamiast usunięcia conflicting pkg)
     *
     * Rzuca SolvError ze szczegółową diagnostyką gdy brak rozwiązania.
     */
    std::vector<ResolvedPackage> resolve_install(
        const std::vector<std::string>& package_names);

    /*
     * Rozwiązuje listę pakietów do usunięcia.
     * ALLOW_UNINSTALL = 1: usuwa też pakiety zależne (jak apt remove --auto).
     * CLEANDEPS = 0: nie usuwa automatycznie osieroconych zależności.
     */
    std::vector<std::string> resolve_remove(
        const std::vector<std::string>& package_names);

    /*
     * Rozwiązuje upgrade pakietów. Jeśli package_names jest puste,
     * aktualizuje wszystkie zainstalowane (SOLVER_UPDATE | SOLVER_SOLVABLE_ALL).
     * Zwraca tylko pakiety które faktycznie zmienią wersję.
     *
     * Nowe w 0.1.0.
     */
    std::vector<ResolvedPackage> resolve_upgrade(
        const std::vector<std::string>& package_names = {});

    ::Pool* raw() { return pool_; }

private:
    SolvPool() = default;
    ::Pool*     pool_ = nullptr;
    std::string arch_ = "amd64";

    std::string format_solver_problems(::Solver* solver);
};

} // namespace debostree::solv
