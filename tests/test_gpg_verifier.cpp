#include "../cmd/gpg_verifier.h"

#include <iostream>
#include <cassert>
#include <stdexcept>

using namespace debostree;

static const char* SAMPLE_INRELEASE = R"(
Origin: Debian
Label: Debian
Suite: stable
Codename: bookworm
Date: Sat, 27 Jan 2024 07:46:47 UTC
Valid-Until: Sat, 03 Feb 2024 07:46:47 UTC
Acquire-By-Hash: yes
Architectures: amd64 arm64 armel armhf i386 mips64el mipsel ppc64el s390x
Components: main contrib non-free non-free-firmware
MD5Sum:
 abc123  12345  main/binary-amd64/Packages.xz
 def456  23456  main/binary-amd64/Packages.gz
SHA256:
 a1b2c3d4e5f6000000000000000000000000000000000000000000000000000001  12345  main/binary-amd64/Packages.xz
 a1b2c3d4e5f6000000000000000000000000000000000000000000000000000002  23456  main/binary-amd64/Packages.gz
 a1b2c3d4e5f6000000000000000000000000000000000000000000000000000003  11111  contrib/binary-amd64/Packages.xz
 a1b2c3d4e5f6000000000000000000000000000000000000000000000000000004  99999  main/source/Sources.xz
SHA512:
 bignumberhere  12345  main/binary-amd64/Packages.xz
)";

static void test_parse_sha256_section() {
    std::cout << "[TEST] test_parse_sha256_section ... ";

    auto checksums = gpg::GpgVerifier::parse_release_checksums(SAMPLE_INRELEASE);

    assert(!checksums.empty() && "Powinny być parsowane sumy SHA256");
    assert(checksums.count("main/binary-amd64/Packages.xz") &&
           "Brak wpisu main/binary-amd64/Packages.xz");
    assert(checksums.count("main/binary-amd64/Packages.gz") &&
           "Brak wpisu main/binary-amd64/Packages.gz");
    assert(checksums.count("contrib/binary-amd64/Packages.xz") &&
           "Brak wpisu contrib/binary-amd64/Packages.xz");

    assert(checksums["main/binary-amd64/Packages.xz"] ==
           "a1b2c3d4e5f6000000000000000000000000000000000000000000000000000001");
    assert(checksums["contrib/binary-amd64/Packages.xz"] ==
           "a1b2c3d4e5f6000000000000000000000000000000000000000000000000000003");

    std::cout << "OK (" << checksums.size() << " wpisów SHA256)\n";
}

static void test_no_sha256_section() {
    std::cout << "[TEST] test_no_sha256_section (brak sekcji SHA256) ... ";

    const char* no_sha = "Origin: Test\nLabel: Test\nMD5Sum:\n abc 123 file.xz\n";
    auto checksums = gpg::GpgVerifier::parse_release_checksums(no_sha);
    assert(checksums.empty() && "Brak sekcji SHA256 => pusta mapa");
    std::cout << "OK\n";
}

static void test_sha256_stops_at_next_field() {
    std::cout << "[TEST] test_sha256_stops_at_next_field ... ";

    /* Sekcja SHA512 NIE powinna być parsowana jako SHA256 */
    auto checksums = gpg::GpgVerifier::parse_release_checksums(SAMPLE_INRELEASE);
    /* Sprawdź że bignumberhere nie trafiło do mapy */
    for (auto& [k, v] : checksums) {
        assert(v != "bignumberhere" && "SHA512 nie powinno trafić do mapy SHA256");
    }
    std::cout << "OK\n";
}

static void test_gpgv_available_check() {
    std::cout << "[TEST] test_gpgv_available ... ";
    /* Tylko sprawdzamy że metoda jest wywoływalna i zwraca bool */
    bool avail = gpg::GpgVerifier::gpgv_available();
    std::cout << "OK (" << (avail ? "gpgv dostepny" : "gpgv niedostepny -- soft-fail") << ")\n";
}

int main() {
    std::cout << "=== GpgVerifier unit tests ===\n";
    try {
        test_parse_sha256_section();
        test_no_sha256_section();
        test_sha256_stops_at_next_field();
        test_gpgv_available_check();
        std::cout << "\nWszystkie testy GPG_VERIFIER zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
