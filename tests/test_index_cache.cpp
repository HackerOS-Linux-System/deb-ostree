#include "../cmd/index_cache.h"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace debostree;

static std::string make_tmp_dir() {
    char tmpl[] = "/tmp/deb-ostree-test-cache-XXXXXX";
    char* result = mkdtemp(tmpl);
    if (!result) throw std::runtime_error("mkdtemp failed");
    return std::string(result);
}

static void test_put_and_get() {
    std::cout << "[TEST] test_put_and_get ... ";
    std::string dir = make_tmp_dir();
    cache::IndexCache c(dir, 3600);

    cache::CacheEntry entry;
    entry.packages_content  = "Package: vim\nVersion: 1.0\n\n";
    entry.inrelease_content = "-----BEGIN PGP SIGNED MESSAGE-----\n...";
    entry.gpg_verified      = true;

    c.put("http://deb.debian.org/debian", "bookworm", "main", entry);

    auto result = c.get("http://deb.debian.org/debian", "bookworm", "main");
    assert(result.has_value() && "Cache powinien mieć wpis");
    assert(result->packages_content == entry.packages_content);
    assert(result->gpg_verified == true);

    fs::remove_all(dir);
    std::cout << "OK\n";
}

static void test_cache_miss_missing() {
    std::cout << "[TEST] test_cache_miss_missing ... ";
    std::string dir = make_tmp_dir();
    cache::IndexCache c(dir, 3600);

    auto result = c.get("http://example.com", "bookworm", "main");
    assert(!result.has_value() && "Brak wpisu => nullopt");

    fs::remove_all(dir);
    std::cout << "OK\n";
}

static void test_cache_expiry() {
    std::cout << "[TEST] test_cache_expiry (TTL=1s) ... ";
    std::string dir = make_tmp_dir();
    cache::IndexCache c(dir, 1 /* TTL: 1 sekunda */);

    cache::CacheEntry entry;
    entry.packages_content = "Package: test\nVersion: 1.0\n\n";
    c.put("http://test.local", "trixie", "contrib", entry);

    /* Poczekaj aż TTL minie */
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto result = c.get("http://test.local", "trixie", "contrib");
    assert(!result.has_value() && "Wpis po TTL powinien być traktowany jako brak");

    fs::remove_all(dir);
    std::cout << "OK\n";
}

static void test_clear() {
    std::cout << "[TEST] test_clear ... ";
    std::string dir = make_tmp_dir();
    cache::IndexCache c(dir, 3600);

    cache::CacheEntry e;
    e.packages_content = "Package: x\nVersion: 1.0\n\n";
    c.put("http://a.com", "stable", "main", e);
    c.put("http://b.com", "stable", "main", e);

    c.clear();

    assert(!c.get("http://a.com", "stable", "main").has_value());
    assert(!c.get("http://b.com", "stable", "main").has_value());

    fs::remove_all(dir);
    std::cout << "OK\n";
}

static void test_multiple_components() {
    std::cout << "[TEST] test_multiple_components ... ";
    std::string dir = make_tmp_dir();
    cache::IndexCache c(dir, 3600);

    cache::CacheEntry e1; e1.packages_content = "main-packages";
    cache::CacheEntry e2; e2.packages_content = "contrib-packages";
    cache::CacheEntry e3; e3.packages_content = "non-free-packages";

    c.put("http://deb.debian.org/debian", "bookworm", "main",     e1);
    c.put("http://deb.debian.org/debian", "bookworm", "contrib",  e2);
    c.put("http://deb.debian.org/debian", "bookworm", "non-free", e3);

    assert(c.get("http://deb.debian.org/debian", "bookworm", "main")->packages_content     == "main-packages");
    assert(c.get("http://deb.debian.org/debian", "bookworm", "contrib")->packages_content  == "contrib-packages");
    assert(c.get("http://deb.debian.org/debian", "bookworm", "non-free")->packages_content == "non-free-packages");

    fs::remove_all(dir);
    std::cout << "OK\n";
}

int main() {
    std::cout << "=== IndexCache unit tests ===\n";
    try {
        test_put_and_get();
        test_cache_miss_missing();
        test_cache_expiry();
        test_clear();
        test_multiple_components();
        std::cout << "\nWszystkie testy INDEX_CACHE zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
