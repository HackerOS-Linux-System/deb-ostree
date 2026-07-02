#include "../cmd/status_db.h"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

using namespace debostree;

static std::string make_tmp_rootfs() {
    std::string dir = "/tmp/deb-ostree-test-statusdb-XXXXXX";
    char tmpl[64];
    std::snprintf(tmpl, sizeof(tmpl), "/tmp/deb-ostree-test-statusdb-XXXXXX");
    char* result = mkdtemp(tmpl);
    if (!result) throw std::runtime_error("mkdtemp failed");
    return std::string(result);
}

static void test_upsert_and_load() {
    std::cout << "[TEST] test_upsert_and_load ... ";
    std::string rootfs = make_tmp_rootfs();

    statusdb::InstalledPackage pkg;
    pkg.name    = "vim";
    pkg.version = "2:9.0.1378-2";
    pkg.files   = {"/usr/bin/vim", "/usr/share/man/man1/vim.1.gz"};

    statusdb::upsert(rootfs, pkg);

    auto loaded = statusdb::load(rootfs);
    assert(loaded.size() == 1 && "Powinien być 1 pakiet");
    assert(loaded[0].name    == "vim");
    assert(loaded[0].version == "2:9.0.1378-2");
    assert(loaded[0].files.size() == 2);

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

static void test_is_installed() {
    std::cout << "[TEST] test_is_installed ... ";
    std::string rootfs = make_tmp_rootfs();

    statusdb::InstalledPackage pkg;
    pkg.name    = "htop";
    pkg.version = "3.2.2-1";
    pkg.files   = {"/usr/bin/htop"};
    statusdb::upsert(rootfs, pkg);

    assert( statusdb::is_installed(rootfs, "htop")  && "htop powinien być zainstalowany");
    assert(!statusdb::is_installed(rootfs, "nano")  && "nano NIE powinien być zainstalowany");

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

static void test_remove() {
    std::cout << "[TEST] test_remove ... ";
    std::string rootfs = make_tmp_rootfs();

    for (auto& name : {"pkgA", "pkgB", "pkgC"}) {
        statusdb::InstalledPackage p;
        p.name = name; p.version = "1.0"; p.files = {};
        statusdb::upsert(rootfs, p);
    }

    statusdb::remove(rootfs, "pkgB");

    auto loaded = statusdb::load(rootfs);
    assert(loaded.size() == 2 && "Powinny być 2 pakiety po usunięciu");
    for (auto& p : loaded) assert(p.name != "pkgB" && "pkgB powinien być usunięty");

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

static void test_upsert_update() {
    std::cout << "[TEST] test_upsert_update (aktualizacja wersji) ... ";
    std::string rootfs = make_tmp_rootfs();

    statusdb::InstalledPackage pkg;
    pkg.name = "curl"; pkg.version = "7.88.1-10"; pkg.files = {"/usr/bin/curl"};
    statusdb::upsert(rootfs, pkg);

    /* Aktualizacja do nowszej wersji */
    pkg.version = "8.5.0-2";
    pkg.files   = {"/usr/bin/curl", "/usr/lib/libcurl.so.4"};
    statusdb::upsert(rootfs, pkg);

    auto loaded = statusdb::load(rootfs);
    assert(loaded.size() == 1 && "Upsert powinien nadpisać, nie dodawać");
    assert(loaded[0].version == "8.5.0-2" && "Wersja powinna być zaktualizowana");
    assert(loaded[0].files.size() == 2 && "Pliki powinny być zaktualizowane");

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

static void test_empty_db() {
    std::cout << "[TEST] test_empty_db ... ";
    std::string rootfs = make_tmp_rootfs();

    auto loaded = statusdb::load(rootfs);
    assert(loaded.empty() && "Pusta baza powinna zwracać pustą listę");
    assert(!statusdb::is_installed(rootfs, "anything") && "is_installed na pustej bazie = false");

    fs::remove_all(rootfs);
    std::cout << "OK\n";
}

int main() {
    std::cout << "=== StatusDb unit tests ===\n";
    try {
        test_upsert_and_load();
        test_is_installed();
        test_remove();
        test_upsert_update();
        test_empty_db();
        std::cout << "\nWszystkie testy STATUS_DB zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
