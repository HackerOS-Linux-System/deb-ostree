#include "../cmd/solv_pool.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/status_db.h"
#include "../cmd/index_cache.h"
#include "../cmd/logging.h"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

using namespace debostree;

/* ── Helpers ── */

static std::string make_tmpdir() {
    char tmpl[] = "/tmp/deb-ostree-itest-XXXXXX";
    char* r = mkdtemp(tmpl);
    if (!r) throw std::runtime_error("mkdtemp failed");
    return std::string(r);
}

static bool have_root() { return geteuid() == 0; }

/* ── Testy ── */

static void test_statusdb_roundtrip() {
    std::cout << "[ITEST] statusdb_roundtrip ... ";
    std::string rootfs = make_tmpdir();

    /* Wstaw 3 pakiety */
    for (auto& name : {"curl", "vim", "htop"}) {
        statusdb::InstalledPackage p;
        p.name    = name;
        p.version = "1.0-1";
        p.files   = {"/usr/bin/" + std::string(name),
                     "/usr/share/man/man1/" + std::string(name) + ".1.gz"};
        statusdb::upsert(rootfs, p);
    }

    /* Weryfikuj odczyt */
    auto loaded = statusdb::load(rootfs);
    assert(loaded.size() == 3 && "Powinny byc 3 pakiety");
    assert(statusdb::is_installed(rootfs, "vim")  && "vim powinien byc zainstalowany");
    assert(!statusdb::is_installed(rootfs, "nano") && "nano nie powinien byc");

    /* Usuń jeden */
    statusdb::remove(rootfs, "htop");
    auto after = statusdb::load(rootfs);
    assert(after.size() == 2 && "Po usunieciu: 2 pakiety");
    assert(!statusdb::is_installed(rootfs, "htop") && "htop usuniety");

    /* Weryfikuj schemat w pliku (#11) */
    std::string db_file = rootfs + "/var/lib/deb-ostree/status.db";
    std::ifstream f(db_file);
    std::string first_line;
    std::getline(f, first_line);
    assert(first_line.rfind("{\"schema\":", 0) == 0 && "Pierwsza linia powinna byc naglowek schematu");

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

static void test_index_cache_integration() {
    std::cout << "[ITEST] index_cache_integration ... ";
    std::string dir = make_tmpdir();
    cache::IndexCache c(dir, 3600);

    /* Symuluj prawdziwy indeks Packages */
    static const char* MINI = R"(
Package: wget
Version: 1.21.3-1+b1
Architecture: amd64
Filename: pool/main/w/wget/wget_1.21.3-1+b1_amd64.deb
SHA256: aabbcc0000000000000000000000000000000000000000000000000000000001
Size: 988384
Description: retrieves files from the web

)";

    cache::CacheEntry ce;
    ce.packages_content = MINI;
    ce.gpg_verified     = false;
    c.put("http://deb.debian.org/debian", "bookworm", "main", ce);

    auto cached = c.get("http://deb.debian.org/debian", "bookworm", "main");
    assert(cached.has_value() && "Cache powinien miec wpis");
    assert(cached->packages_content == MINI && "Tresc powinna byc identyczna");

    /* Parsowanie z cache powinno byc spójne */
    apt::RepoIndex idx = apt::RepoIndex::parse(cached->packages_content);
    assert(idx.entries().size() == 1 && "Jeden pakiet w indeksie");
    assert(idx.entries()[0].package == "wget" && "Nazwa pakietu: wget");
    assert(idx.entries()[0].size == 988384 && "Rozmiar poprawny");

    fs::remove_all(dir);
    std::cout << "OK\n";
}

static void test_solv_full_pipeline() {
    std::cout << "[ITEST] solv_full_pipeline (index->parse->pool->resolve) ... ";

    /* Duży realistyczny mini-indeks z prawdziwymi relacjami */
    static const char* PKGS = R"(
Package: less
Version: 590-2
Architecture: amd64
Depends: libc6 (>= 2.34)
Filename: pool/main/l/less/less_590-2_amd64.deb
SHA256: aa00000000000000000000000000000000000000000000000000000000000001
Size: 153828
Description: pager

Package: libc6
Version: 2.36-9+deb12u4
Architecture: amd64
Provides: libc6 (= 2.36-9+deb12u4)
Filename: pool/main/g/glibc/libc6_2.36-9+deb12u4_amd64.deb
SHA256: bb00000000000000000000000000000000000000000000000000000000000002
Size: 2839876
Description: GNU C Library

Package: ncurses-base
Version: 6.4-4
Architecture: all
Depends: libncurses6 | libncursesw6
Filename: pool/main/n/ncurses/ncurses-base_6.4-4_all.deb
SHA256: cc00000000000000000000000000000000000000000000000000000000000003
Size: 284644
Description: basic terminal type definitions

Package: libncurses6
Version: 6.4-4
Architecture: amd64
Filename: pool/main/n/ncurses/libncurses6_6.4-4_amd64.deb
SHA256: dd00000000000000000000000000000000000000000000000000000000000004
Size: 116468
Description: shared libraries for terminal handling

Package: libncursesw6
Version: 6.4-4
Architecture: amd64
Filename: pool/main/n/ncurses/libncursesw6_6.4-4_amd64.deb
SHA256: ee00000000000000000000000000000000000000000000000000000000000005
Size: 139216
Description: shared libraries for terminal handling (wide char)

)";

    solv::SolvPool pool = solv::SolvPool::create("amd64");
    apt::RepoIndex idx  = apt::RepoIndex::parse(PKGS);
    pool.add_repo_from_index(idx, "debian-bookworm-main");

    /* "less" zalezy od libc6 i ncurses */
    auto resolved = pool.resolve_install({"less"});
    assert(!resolved.empty() && "resolve_install less zwrocil pusty wynik");

    bool found_less = false, found_libc = false;
    for (auto& p : resolved) {
        if (p.name == "less")  found_less = true;
        if (p.name == "libc6") found_libc = true;
    }
    assert(found_less && "less powinien byc w wynikach");
    assert(found_libc && "libc6 (zaleznosc less) powinna byc w wynikach");

    /* ncurses-base ma OR zaleznosc: libncurses6 | libncursesw6 */
    auto resolved_nc = pool.resolve_install({"ncurses-base"});
    bool found_nc6 = false, found_ncw6 = false;
    for (auto& p : resolved_nc) {
        if (p.name == "libncurses6")  found_nc6  = true;
        if (p.name == "libncursesw6") found_ncw6 = true;
    }
    assert((found_nc6 || found_ncw6) && "Jedna z alternatyw OR ncurses powinna byc wybrana");
    assert(!(found_nc6 && found_ncw6) && "Nie obie alternatywy OR jednoczesnie");

    std::cout << "OK (" << resolved.size() << " dep, OR: "
              << (found_nc6 ? "libncurses6" : "libncursesw6") << ")\n";
}

static void test_statusdb_schema_migration() {
    std::cout << "[ITEST] statusdb_schema_migration (stary format bez naglowka) ... ";
    std::string rootfs = make_tmpdir();

    /* Symuluj stary plik bez nagłówka schematu */
    std::string db_dir = rootfs + "/var/lib/deb-ostree";
    fs::create_directories(db_dir);
    std::ofstream old_db(db_dir + "/status.db");
    old_db << "{\"name\":\"vim\",\"version\":\"9.0\",\"files\":[\"/usr/bin/vim\"]}\n";
    old_db.close();

    /* Nowy kod powinien umieć wczytać stary format (bez linii schematu) */
    auto loaded = statusdb::load(rootfs);
    assert(loaded.size() == 1 && "Stary format powinien byc wczytany poprawnie");
    assert(loaded[0].name == "vim" && "Nazwa pakietu");
    assert(loaded[0].version == "9.0" && "Wersja pakietu");

    /* Po upsert -- nowy format ma naglowek */
    statusdb::upsert(rootfs, loaded[0]);
    std::ifstream f(db_dir + "/status.db");
    std::string first;
    std::getline(f, first);
    assert(first.rfind("{\"schema\":", 0) == 0 && "Po upsert: naglowek schematu");

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

int main() {
    std::cout << "=== deb-ostree Integration Tests (v0.2.0) ===\n";
    if (!have_root()) {
        std::cout << "[INFO] Uruchomiony bez root -- testy overlay pominiete\n";
    }
    try {
        test_statusdb_roundtrip();
        test_index_cache_integration();
        test_solv_full_pipeline();
        test_statusdb_schema_migration();
        std::cout << "\nWszystkie testy integracyjne zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
