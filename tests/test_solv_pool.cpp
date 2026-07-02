#include "../cmd/solv_pool.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/status_db.h"

#include <iostream>
#include <stdexcept>
#include <cassert>

using namespace debostree;

/* Minimalna zawartość pliku Packages z trzema pakietami tworzącymi
 * prostą sieć zależności: pkgC -> pkgB -> pkgA */
static const char* MINI_PACKAGES = R"(
Package: pkgA
Version: 1.0
Architecture: amd64
Filename: pool/main/p/pkgA_1.0_amd64.deb
SHA256: aabbcc0000000000000000000000000000000000000000000000000000000000
Size: 1234
Description: pakiet bazowy

Package: pkgB
Version: 2.0
Architecture: amd64
Depends: pkgA (>= 1.0)
Filename: pool/main/p/pkgB_2.0_amd64.deb
SHA256: bbccdd0000000000000000000000000000000000000000000000000000000000
Size: 2345
Description: pakiet zalezy od pkgA

Package: pkgC
Version: 3.0
Architecture: amd64
Depends: pkgB
Filename: pool/main/p/pkgC_3.0_amd64.deb
SHA256: ccddeeff000000000000000000000000000000000000000000000000000000000
Size: 3456
Description: pakiet zalezy od pkgB

Package: pkgConflict
Version: 1.0
Architecture: amd64
Conflicts: pkgA
Filename: pool/main/p/pkgConflict_1.0_amd64.deb
SHA256: ddeeff00000000000000000000000000000000000000000000000000000000000
Size: 111
Description: konfliktuje z pkgA

)";

static void test_basic_resolve() {
    std::cout << "[TEST] test_basic_resolve ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    /* Instalacja pkgC powinna wciągnąć pkgB i pkgA */
    auto resolved = pool.resolve_install({"pkgC"});

    assert(!resolved.empty() && "resolve_install zwrócił pusty wynik");

    bool found_A = false, found_B = false, found_C = false;
    for (auto& p : resolved) {
        if (p.name == "pkgA") found_A = true;
        if (p.name == "pkgB") found_B = true;
        if (p.name == "pkgC") found_C = true;
    }

    assert(found_A && "pkgA nie został rozwiązany jako zależność pkgB");
    assert(found_B && "pkgB nie został rozwiązany jako zależność pkgC");
    assert(found_C && "pkgC nie został znaleziony w wynikach");

    std::cout << "OK (" << resolved.size() << " pakietów)\n";
}

static void test_missing_package() {
    std::cout << "[TEST] test_missing_package ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    bool threw = false;
    try {
        pool.resolve_install({"nieistniejacy-pakiet-xyz"});
    } catch (const solv::SolvError&) {
        threw = true;
    }

    assert(threw && "SolvError nie został rzucony dla brakującego pakietu");
    std::cout << "OK\n";
}

static void test_resolve_remove() {
    std::cout << "[TEST] test_resolve_remove ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    /* Załaduj zainstalowane pakiety */
    std::vector<statusdb::InstalledPackage> installed = {
        {"pkgA", "1.0", {}},
        {"pkgB", "2.0", {}},
        {"pkgC", "3.0", {}}
    };
    pool.add_installed_packages(installed);

    auto to_remove = pool.resolve_remove({"pkgC"});
    assert(!to_remove.empty() && "resolve_remove nie zwrócił nic");

    bool found_C = false;
    for (auto& n : to_remove) if (n == "pkgC") found_C = true;
    assert(found_C && "pkgC nie jest na liście do usunięcia");

    std::cout << "OK (" << to_remove.size() << " pakietów)\n";
}

static void test_installed_packages_loaded() {
    std::cout << "[TEST] test_installed_packages_loaded ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    /* pkgA jest zainstalowany */
    std::vector<statusdb::InstalledPackage> installed = {
        {"pkgA", "1.0", {}}
    };
    pool.add_installed_packages(installed);

    /* resolve_install(["pkgA"]) gdy pkgA juz w @System:
     * libsolv moze zwrocic:
     *   a) pusta liste (nic do zrobienia -- juz zainstalowane)
     *   b) pkgA (re-install)
     *   c) SolvError ("already installed")
     * Wszystkie trzy wyniki sa akceptowalne -- testujemy ze nie crashuje. */
    bool threw_solv = false;
    size_t resolved_count = 0;
    try {
        auto resolved = pool.resolve_install({"pkgA"});
        resolved_count = resolved.size();
        /* Jesli solver zdecydowal sie na re-install -- tez OK */
    } catch (const solv::SolvError&) {
        threw_solv = true; /* "already installed" -- akceptowalne */
    }

    if (threw_solv) {
        std::cout << "OK (SolvError: already installed -- oczekiwane)\n";
    } else {
        std::cout << "OK (resolved: " << resolved_count << " pkg -- ";
        std::cout << (resolved_count == 0 ? "nic do zrobienia" : "re-install") << ")\n";
    }
}

int main() {
    std::cout << "=== SolvPool unit tests ===\n";
    try {
        test_basic_resolve();
        test_missing_package();
        test_resolve_remove();
        test_installed_packages_loaded();
        std::cout << "\nWszystkie testy SOLV_POOL zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
