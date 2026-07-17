#include "../cmd/solv_pool.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/status_db.h"

#include <iostream>
#include <stdexcept>
#include <cassert>

using namespace debostree;

/* ── Dane testowe ── */

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

static const char* OR_PACKAGES = R"(
Package: app-or-test
Version: 1.0
Architecture: amd64
Depends: libA | libB
Filename: pool/main/app-or-test_1.0_amd64.deb
SHA256: aaaa000000000000000000000000000000000000000000000000000000000001
Size: 100
Description: pakiet z OR-zaleznoscia

Package: libA
Version: 1.0
Architecture: amd64
Filename: pool/main/libA_1.0_amd64.deb
SHA256: aaaa000000000000000000000000000000000000000000000000000000000002
Size: 200
Description: pierwsza alternatywa OR

Package: libB
Version: 1.0
Architecture: amd64
Filename: pool/main/libB_1.0_amd64.deb
SHA256: aaaa000000000000000000000000000000000000000000000000000000000003
Size: 300
Description: druga alternatywa OR

Package: app-virtual
Version: 1.0
Architecture: amd64
Depends: virtual-mta
Filename: pool/main/app-virtual_1.0_amd64.deb
SHA256: aaaa000000000000000000000000000000000000000000000000000000000004
Size: 100
Description: zalezy od wirtualnego pakietu

Package: real-mta
Version: 1.0
Architecture: amd64
Provides: virtual-mta
Filename: pool/main/real-mta_1.0_amd64.deb
SHA256: aaaa000000000000000000000000000000000000000000000000000000000005
Size: 500
Description: implementuje virtual-mta

)";

/* ── Testy podstawowe ── */

static void test_basic_resolve() {
    std::cout << "[TEST] test_basic_resolve ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    auto resolved = pool.resolve_install({"pkgC"});
    assert(!resolved.empty() && "resolve_install zwrocil pusty wynik");

    bool found_A = false, found_B = false, found_C = false;
    for (auto& p : resolved) {
        if (p.name == "pkgA") found_A = true;
        if (p.name == "pkgB") found_B = true;
        if (p.name == "pkgC") found_C = true;
    }
    /* Suppress unused warnings -- asserts use them */
    (void)found_A; (void)found_B; (void)found_C;

    assert(found_A && "pkgA nie zostal rozwiazany jako zaleznosc pkgB");
    assert(found_B && "pkgB nie zostal rozwiazany jako zaleznosc pkgC");
    assert(found_C && "pkgC nie zostal znaleziony w wynikach");

    std::cout << "OK (" << resolved.size() << " pakietow)\n";
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
    (void)threw;
    assert(threw && "SolvError nie zostal rzucony dla brakujacego pakietu");
    std::cout << "OK\n";
}

static void test_resolve_remove() {
    std::cout << "[TEST] test_resolve_remove ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    std::vector<statusdb::InstalledPackage> installed = {
        {"pkgA", "1.0", {}},
        {"pkgB", "2.0", {}},
        {"pkgC", "3.0", {}}
    };
    pool.add_installed_packages(installed);

    auto to_remove = pool.resolve_remove({"pkgC"});
    assert(!to_remove.empty() && "resolve_remove nie zwrocil nic");

    bool found_C = false;
    for (auto& n : to_remove) if (n == "pkgC") found_C = true;
    (void)found_C;
    assert(found_C && "pkgC nie jest na liscie do usuniecia");

    std::cout << "OK (" << to_remove.size() << " pakietow)\n";
}

static void test_installed_packages_loaded() {
    std::cout << "[TEST] test_installed_packages_loaded ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "test-repo");

    std::vector<statusdb::InstalledPackage> installed = {{"pkgA", "1.0", {}}};
    pool.add_installed_packages(installed);

    bool threw_solv = false;
    size_t resolved_count = 0;
    try {
        auto resolved = pool.resolve_install({"pkgA"});
        resolved_count = resolved.size();
    } catch (const solv::SolvError&) {
        threw_solv = true;
    }

    if (threw_solv) {
        std::cout << "OK (SolvError: already installed -- oczekiwane)\n";
    } else {
        std::cout << "OK (resolved: " << resolved_count << " pkg -- "
                  << (resolved_count == 0 ? "nic do zrobienia" : "re-install") << ")\n";
    }
}

/* ── Testy OR-zaleznosci, Provides, konflikty, arch:all (v0.2.0) ── */

static void test_or_dependency_basic() {
    std::cout << "[TEST] test_or_dependency_basic (A | B) ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(OR_PACKAGES);
    pool.add_repo_from_index(index, "or-test-repo");

    auto resolved = pool.resolve_install({"app-or-test"});

    assert(!resolved.empty() && "OR-dep: resolver zwrocil pusty wynik");
    assert(resolved.size() >= 2 && "OR-dep: oczekiwano >= 2 pakietow");

    bool found_app = false, found_libA = false, found_libB = false;
    for (auto& p : resolved) {
        if (p.name == "app-or-test") found_app  = true;
        if (p.name == "libA")        found_libA = true;
        if (p.name == "libB")        found_libB = true;
    }

    assert(found_app  && "app-or-test powinien byc w wynikach");
    assert((found_libA || found_libB) && "Zadna z alternatyw OR nie zostala wybrana");
    assert(!(found_libA && found_libB) && "Solver wybral OBE alternatywy OR -- blad");

    std::cout << "OK (wybrano: " << (found_libA ? "libA" : "libB") << ")\n";
}

static void test_or_dependency_second_alternative() {
    std::cout << "[TEST] test_or_dependency_second_alternative (tylko druga dostepna) ... ";

    static const char* ONLY_LIB_B = R"(
Package: app-only-b
Version: 1.0
Architecture: amd64
Depends: libX | libY
Filename: pool/main/app-only-b_1.0_amd64.deb
SHA256: bbbb000000000000000000000000000000000000000000000000000000000001
Size: 100
Description: test

Package: libY
Version: 2.0
Architecture: amd64
Filename: pool/main/libY_2.0_amd64.deb
SHA256: bbbb000000000000000000000000000000000000000000000000000000000002
Size: 200
Description: tylko druga alternatywa dostepna

)";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(ONLY_LIB_B);
    pool.add_repo_from_index(index, "only-b-repo");

    auto resolved = pool.resolve_install({"app-only-b"});

    bool found_app = false, found_libY = false;
    for (auto& p : resolved) {
        if (p.name == "app-only-b") found_app  = true;
        if (p.name == "libY")       found_libY = true;
    }

    assert(found_app  && "app-only-b powinien byc w wynikach");
    assert(found_libY && "libY (druga alternatywa) powinna byc wybrana gdy brak libX");

    std::cout << "OK (libY wybrane jako jedyna dostepna alternatywa)\n";
}

static void test_virtual_package_provides() {
    std::cout << "[TEST] test_virtual_package_provides (Provides:) ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(OR_PACKAGES);
    pool.add_repo_from_index(index, "virt-repo");

    auto resolved = pool.resolve_install({"app-virtual"});

    bool found_real_mta = false;
    for (auto& p : resolved)
        if (p.name == "real-mta") { found_real_mta = true; break; }

    assert(found_real_mta && "real-mta powinien byc wybrany jako dostawca virtual-mta");
    std::cout << "OK (real-mta wybrany przez Provides: virtual-mta)\n";
}

static void test_conflict_detection() {
    std::cout << "[TEST] test_conflict_detection ... ";

    solv::SolvPool pool = solv::SolvPool::create();
    apt::RepoIndex index = apt::RepoIndex::parse(MINI_PACKAGES);
    pool.add_repo_from_index(index, "conflict-repo");

    std::vector<statusdb::InstalledPackage> installed = {{"pkgA", "1.0", {}}};
    pool.add_installed_packages(installed);

    bool threw = false;
    try {
        pool.resolve_install({"pkgConflict"});
    } catch (const solv::SolvError&) {
        threw = true;
    }
    (void)threw;
    assert(threw && "Instalacja pakietu konfliktujacego z @System powinna rzucac SolvError");
    std::cout << "OK (konflikt z @System wykryty)\n";
}

static void test_noarch_package() {
    std::cout << "[TEST] test_noarch_package (Architecture: all) ... ";

    static const char* NOARCH_PKG = R"(
Package: python3-common
Version: 3.11.0
Architecture: all
Filename: pool/main/python3-common_3.11.0_all.deb
SHA256: cccc000000000000000000000000000000000000000000000000000000000001
Size: 100
Description: architekturalnie niezalezny

)";

    solv::SolvPool pool = solv::SolvPool::create("amd64");
    apt::RepoIndex index = apt::RepoIndex::parse(NOARCH_PKG);
    pool.add_repo_from_index(index, "noarch-repo");

    auto resolved = pool.resolve_install({"python3-common"});
    assert(!resolved.empty() && "Pakiet Architecture:all powinien byc dostepny w puli amd64");
    assert(resolved[0].name == "python3-common");

    std::cout << "OK (arch:all zaakceptowany w puli amd64)\n";
}

/* ── main ── */

int main() {
    std::cout << "=== SolvPool unit tests (v0.2.0) ===\n";
    try {
        /* Testy podstawowe */
        test_basic_resolve();
        test_missing_package();
        test_resolve_remove();
        test_installed_packages_loaded();
        /* Testy v0.2.0: OR-zaleznosci, Provides, konflikty, arch:all */
        test_or_dependency_basic();
        test_or_dependency_second_alternative();
        test_virtual_package_provides();
        test_conflict_detection();
        test_noarch_package();
        std::cout << "\nWszystkie testy SOLV_POOL zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
