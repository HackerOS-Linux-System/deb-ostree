#include "../cmd/oci_ref.h"

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <vector>
#include <string>

using namespace debostree;

static void test_valid_refs() {
    std::cout << "[TEST] test_valid_refs ... ";

    struct Case { std::string input; std::string registry; std::string org; std::string image; std::string tag; };
    std::vector<Case> cases = {
        {"ghcr.io/mojorg/debian-bootc:bookworm",         "ghcr.io",   "mojorg",   "debian-bootc", "bookworm"},
        {"registry.example.com/team/myos:1.0.0",         "registry.example.com", "team", "myos", "1.0.0"},
        {"quay.io/fedora/fedora-bootc:40",                "quay.io",   "fedora",   "fedora-bootc", "40"},
        {"docker.io/library/debian:bookworm-slim",        "docker.io", "library",  "debian",       "bookworm-slim"},
        {"localhost/local-build:latest",                  "localhost", "",         "local-build",  "latest"},
        {"mojorg/debian-bootc:bookworm",                  "docker.io", "mojorg",   "debian-bootc", "bookworm"},
        {"deb-ostree-oci:ghcr.io/org/img:tag",            "ghcr.io",  "org",      "img",          "tag"},
    };

    for (auto& tc : cases) {
        auto r = oci::parse(tc.input);
        assert(r.valid && ("Should be valid: " + tc.input).c_str());
        if (!tc.registry.empty()) assert(r.registry == tc.registry);
        if (!tc.org.empty())      assert(r.organization == tc.org);
        assert(r.image == tc.image);
        assert(r.tag   == tc.tag);
    }
    std::cout << "OK (" << cases.size() << " referencji)\n";
}

static void test_invalid_refs() {
    std::cout << "[TEST] test_invalid_refs ... ";

    std::vector<std::string> invalid = {
        "",              /* pusta */
        "debian",        /* samo image, brak org i registry */
    };

    for (auto& ref : invalid) {
        auto r = oci::parse(ref);
        assert(!r.valid && ("Should be invalid: '" + ref + "'").c_str());
        assert(!r.error.empty());
    }
    std::cout << "OK (" << invalid.size() << " blednych referencji odrzuconych)\n";
}

static void test_validate_or_throw() {
    std::cout << "[TEST] test_validate_or_throw ... ";

    /* Poprawne -- nie rzuca */
    try {
        oci::validate_or_throw("ghcr.io/mojorg/debian-bootc:bookworm");
        oci::validate_or_throw("quay.io/org/image:latest");
    } catch (...) {
        assert(false && "Nie powinno rzucic dla poprawnych referencji");
    }

    /* Bledne -- rzuca runtime_error */
    bool threw = false;
    try {
        oci::validate_or_throw("debian");
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg(e.what());
        /* Komunikat powinien zawierac instrukcje */
        assert(msg.find("deb-ostree deploy") != std::string::npos &&
               "Komunikat bledu powinien zawierac 'deb-ostree deploy'");
    }
    (void)threw;
    assert(threw && "Powinna byc rzucona runtime_error");

    std::cout << "OK\n";
}

static void test_require_origin_refspec() {
    std::cout << "[TEST] test_require_origin_refspec ... ";

    /* Ustawiony refspec -- nie rzuca */
    try {
        oci::require_origin_refspec(
            "deb-ostree-oci:ghcr.io/mojorg/debian-bootc:bookworm", "install");
        oci::require_origin_refspec(
            "ghcr.io/org/image:tag", "upgrade");
    } catch (...) {
        assert(false && "Nie powinno rzucic dla poprawnego refspec");
    }

    /* Pusty refspec -- rzuca */
    bool threw = false;
    try {
        oci::require_origin_refspec("", "install");
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg(e.what());
        assert(msg.find("deploy") != std::string::npos);
        assert(msg.find("rebase") != std::string::npos);
    }
    (void)threw;
    assert(threw && "Pusty refspec powinien rzucic blad");

    std::cout << "OK\n";
}

static void test_full_ref_formatting() {
    std::cout << "[TEST] test_full_ref_formatting ... ";

    auto r1 = oci::parse("ghcr.io/mojorg/debian-bootc:bookworm");
    assert(r1.valid);
    assert(r1.full_ref() == "ghcr.io/mojorg/debian-bootc:bookworm");
    assert(r1.short_ref() == "mojorg/debian-bootc:bookworm");

    auto r2 = oci::parse("quay.io/fedora/fedora-bootc:40");
    assert(r2.valid);
    assert(r2.full_ref() == "quay.io/fedora/fedora-bootc:40");

    std::cout << "OK\n";
}

int main() {
    std::cout << "=== OciRef unit tests (v0.2.0) ===\n";
    try {
        test_valid_refs();
        test_invalid_refs();
        test_validate_or_throw();
        test_require_origin_refspec();
        test_full_ref_formatting();
        std::cout << "\nWszystkie testy OCI_REF zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
