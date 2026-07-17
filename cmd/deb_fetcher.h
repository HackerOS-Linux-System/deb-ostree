#pragma once
/*
 * deb-ostree -- deb_fetcher.h
 * Pobieranie InRelease, indeksów Packages i plików .deb z mirrorów Debiana
 * przez HTTP/HTTPS, z weryfikacją SHA256 i retry.
 *
 * Wersja: 0.2.0
 *   - Dodano fetch_inrelease() -- pobieranie podpisanego pliku InRelease
 *   - Dodano fetch_packages_index_with_release_verify() -- weryfikacja SHA256
 *     indeksu względem sum z InRelease
 *   - Dodano retry z wykładniczym cofaniem (3 próby, 2s/4s/8s)
 *   - Dodano cache pobierania w tmp_dir (DebFetcher(tmp_dir))
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace debostree::deb {

struct AptSource {
    std::string base_url;
    std::string suite;
    std::vector<std::string> components;
};

AptSource parse_apt_source_line(const std::string& line);

class DebFetcher {
public:
    /* tmp_dir: katalog na tymczasowe pliki i cache pobierania .deb */
    explicit DebFetcher(const std::string& tmp_dir);
    DebFetcher(); /* tmp_dir = /tmp/deb-ostree-fetch */
    ~DebFetcher();
    DebFetcher(const DebFetcher&) = delete;
    DebFetcher& operator=(const DebFetcher&) = delete;

    /* Pobiera InRelease (podpisany plik z sumami) lub Release jako fallback.
     * Nie weryfikuje GPG -- to robi gpg::GpgVerifier. */
    std::string fetch_inrelease(const AptSource& source);

    /* Pobiera i dekompresuje indeks Packages (.xz → .gz → plain).
     * Retry: 3 dodatkowe próby z wykładniczym cofaniem. */
    std::string fetch_packages_index(const AptSource& source,
                                     const std::string& component,
                                     const std::string& arch = "amd64");

    /* Jak fetch_packages_index + weryfikacja SHA256 względem sum z InRelease
     * (mapa: ścieżka_relatywna → sha256 z gpg::GpgVerifier::parse_release_checksums).
     * Rzuca std::runtime_error gdy suma się nie zgadza. */
    std::string fetch_packages_index_with_release_verify(
        const AptSource& source,
        const std::string& component,
        const std::unordered_map<std::string, std::string>& release_checksums,
        const std::string& arch = "amd64");

    /* Pobiera plik .deb do dest_path z weryfikacją SHA256.
     * Sprawdza cache pobierania w tmp_dir przed pobraniem z sieci.
     * Retry: 3 dodatkowe próby z wykładniczym cofaniem. */
    void fetch_deb_package(const std::string& base_url,
                           const std::string& filename,
                           const std::string& dest_path,
                           const std::string& expected_sha256 = "");

    std::string fetch_url_to_string(const std::string& url);
    void fetch_url_to_file(const std::string& url, const std::string& dest_path);

private:
    void*       curl_handle_;
    std::string tmp_dir_;

    std::string fetch_url_to_string_with_retry(const std::string& url,
                                               int max_retries = 3,
                                               int retry_delay_s = 2);
};

} // namespace debostree::deb
