#pragma once
/*
 * deb-ostree -- deb_fetcher.h
 * Pobieranie indeksow Packages i plikow .deb z mirror Debiana przez HTTP,
 * z weryfikacja SHA256 -- zastepuje "apt-get update"/"apt-get download".
 *
 * Implementacja oparta na libcurl (przez prosty wrapper RAII) -- standardowa,
 * dobrze przetestowana biblioteka HTTP. Nie reimplementujemy klienta HTTP
 * od zera (poza zakresem tej rozbudowy -- liczy sie eliminacja apt/dpkg,
 * nie eliminacja libcurl).
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree::deb {

/* Pojedyncza linia sources.list w uproszczonej, ale wystarczajacej formie:
 * "deb http://mirror/debian bookworm main contrib" rozbite na pola. */
struct AptSource {
    std::string base_url;   /* "http://deb.debian.org/debian" */
    std::string suite;      /* "bookworm" / "trixie" / ... */
    std::vector<std::string> components; /* ["main", "contrib", ...] */
};

/* Parsuje linie w formacie sources.list (klasyczny, jednoliniowy format
 * "deb <url> <suite> <component...>") na AptSource. Rzuca std::runtime_error
 * przy nieprawidlowym formacie linii. */
AptSource parse_apt_source_line(const std::string& line);

/*
 * DebFetcher pobiera pliki z mirror Debiana (indeksy Packages, archiwa .deb)
 * przez HTTP/HTTPS, weryfikujac SHA256 gdy jest znany.
 */
class DebFetcher {
public:
    DebFetcher();
    ~DebFetcher();
    DebFetcher(const DebFetcher&) = delete;
    DebFetcher& operator=(const DebFetcher&) = delete;

    /*
     * Buduje URL indeksu Packages dla danego AptSource + component +
     * architektura, sciaga go (probujac kolejno .xz, .gz, plain w tej
     * kolejnosci -- preferujemy najmniejszy transfer) i zwraca
     * ZDEKOMPRESOWANA tresc gotowa do apt::RepoIndex::parse().
     */
    std::string fetch_packages_index(const AptSource& source, const std::string& component,
                                     const std::string& arch = "amd64");

    /*
     * Sciaga plik .deb z mirror na podstawie base_url + filename (z pola
     * "Filename:" w Packages) do dest_path. Jesli expected_sha256 nie jest
     * pusty, weryfikuje sume kontrolna po pobraniu -- rzuca
     * std::runtime_error przy niezgodnosci.
     */
    void fetch_deb_package(const std::string& base_url, const std::string& filename,
                           const std::string& dest_path, const std::string& expected_sha256 = "");

private:
    /* CURL* -- typ nieujawniony w headerze (void*), zeby nie wymagac
     * <curl/curl.h> wszedzie gdzie ten plik jest wlaczany; tylko
     * deb_fetcher.cpp potrzebuje prawdziwego typu CURL*. */
    void* curl_handle_;

    std::string fetch_url_to_string(const std::string& url);
    void fetch_url_to_file(const std::string& url, const std::string& dest_path);
};

} // namespace debostree::deb
