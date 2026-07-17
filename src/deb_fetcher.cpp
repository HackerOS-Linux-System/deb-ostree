#include "../cmd/deb_fetcher.h"
#include "../cmd/compress_util.h"
#include "../cmd/logging.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <cstdlib>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdio>
#include <thread>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::deb {

namespace {

size_t write_to_string_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t write_to_file_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

std::string sha256_hex(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("deb::sha256_hex: nie mozna otworzyc " + path);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        throw std::runtime_error("deb::sha256_hex: EVP_MD_CTX_new() nie powiodlo sie");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("deb::sha256_hex: EVP_DigestInit_ex nie powiodlo sie");
    }

    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(f.gcount())) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("deb::sha256_hex: EVP_DigestUpdate nie powiodlo sie");
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("deb::sha256_hex: EVP_DigestFinal_ex nie powiodlo sie");
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << std::setw(2) << static_cast<int>(digest[i]);
    return oss.str();
}

std::string sha256_string(const std::string& data) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << std::setw(2) << static_cast<int>(digest[i]);
    return oss.str();
}

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

/* ── parse_apt_source_line ── */

AptSource parse_apt_source_line(const std::string& line) {
    std::istringstream iss(trim(line));
    std::string type;
    iss >> type;

    if (type != "deb")
        throw std::runtime_error(
            "deb::parse_apt_source_line: oczekiwano 'deb' na poczatku linii, otrzymano: " + type);

    AptSource src;
    iss >> src.base_url >> src.suite;

    std::string component;
    while (iss >> component) src.components.push_back(component);

    if (src.base_url.empty() || src.suite.empty())
        throw std::runtime_error(
            "deb::parse_apt_source_line: niekompletna linia sources.list: " + line);

    return src;
}

/* ── DebFetcher ── */

DebFetcher::DebFetcher(const std::string& tmp_dir) : tmp_dir_(tmp_dir) {
    fs::create_directories(tmp_dir_);
    curl_handle_ = curl_easy_init();
    if (!curl_handle_)
        throw std::runtime_error("deb::DebFetcher: curl_easy_init() nie powiodlo sie");
}

DebFetcher::DebFetcher() : DebFetcher("/tmp/deb-ostree-fetch") {}

DebFetcher::~DebFetcher() {
    if (curl_handle_) curl_easy_cleanup(static_cast<CURL*>(curl_handle_));
}

/* Wewnętrzna implementacja z retry (wykładnicze cofanie).
 * max_retries: liczba DODATKOWYCH prób po pierwszej (0 = bez retry).
 * retry_delay_s: pierwsza przerwa między próbami (kolejne = *2). */
std::string DebFetcher::fetch_url_to_string_with_retry(const std::string& url,
                                                        int max_retries,
                                                        int retry_delay_s) {
    CURL* curl = static_cast<CURL*>(curl_handle_);
    std::string last_error;

    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        if (attempt > 0) {
            int delay = retry_delay_s * (1 << (attempt - 1)); /* 2s, 4s, 8s... */
            log::warn("Retry " + std::to_string(attempt) + "/" + std::to_string(max_retries)
                      + " za " + std::to_string(delay) + "s: " + url);
            std::this_thread::sleep_for(std::chrono::seconds(delay));
        }

        curl_easy_reset(curl);
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "deb-ostree/0.2.0");
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        /* Proxy HTTP/HTTPS ze zmiennych środowiskowych (#9) */
        const char* http_proxy  = std::getenv("http_proxy");
        const char* https_proxy = std::getenv("https_proxy");
        const char* all_proxy   = std::getenv("all_proxy");
        const char* proxy_val   = https_proxy ? https_proxy
                                : (http_proxy ? http_proxy : all_proxy);
        if (proxy_val && proxy_val[0])
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy_val);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) return response;

        last_error = curl_easy_strerror(res);
        log::debug("curl error (attempt " + std::to_string(attempt) + "): " + last_error);
    }

    throw std::runtime_error(
        "deb::DebFetcher: pobieranie " + url + " nie powiodlo sie po "
        + std::to_string(max_retries + 1) + " probach: " + last_error);
}

std::string DebFetcher::fetch_url_to_string(const std::string& url) {
    return fetch_url_to_string_with_retry(url, 3 /* retries */, 2 /* delay_s */);
}

void DebFetcher::fetch_url_to_file(const std::string& url, const std::string& dest_path) {
    CURL* curl = static_cast<CURL*>(curl_handle_);
    std::string last_error;

    for (int attempt = 0; attempt <= 3; ++attempt) {
        if (attempt > 0) {
            int delay = 2 * (1 << (attempt - 1));
            log::warn("Retry " + std::to_string(attempt) + "/3 za "
                      + std::to_string(delay) + "s: " + url);
            std::this_thread::sleep_for(std::chrono::seconds(delay));
        }

        curl_easy_reset(curl);
        std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            throw std::runtime_error("deb::DebFetcher: nie mozna zapisac " + dest_path);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "deb-ostree/0.2.0");
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        /* Proxy ze zmiennych środowiskowych (#9) */
        {
            const char* pv = std::getenv("https_proxy");
            if (!pv) pv = std::getenv("http_proxy");
            if (!pv) pv = std::getenv("all_proxy");
            if (pv && pv[0]) curl_easy_setopt(curl, CURLOPT_PROXY, pv);
        }

        CURLcode res = curl_easy_perform(curl);
        out.close();

        if (res == CURLE_OK) return;

        last_error = curl_easy_strerror(res);
        fs::remove(dest_path);
    }

    throw std::runtime_error(
        "deb::DebFetcher: pobieranie " + url + " nie powiodlo sie: " + last_error);
}

/* ── fetch_inrelease -- NOWE w 0.1.0 ── */

std::string DebFetcher::fetch_inrelease(const AptSource& source) {
    std::string url = source.base_url + "/dists/" + source.suite + "/InRelease";
    log::debug("Pobieranie InRelease: " + url);
    try {
        return fetch_url_to_string(url);
    } catch (...) {
        /* Fallback: osobny Release + Release.gpg */
        log::debug("InRelease niedostepny, probuje Release...");
        return fetch_url_to_string(
            source.base_url + "/dists/" + source.suite + "/Release");
    }
}

/* ── fetch_packages_index -- aktualizacja 0.1.0 ── */

std::string DebFetcher::fetch_packages_index(const AptSource& source,
                                              const std::string& component,
                                              const std::string& arch) {
    std::string base = source.base_url + "/dists/" + source.suite + "/"
                     + component + "/binary-" + arch + "/Packages";

    for (const char* ext : {".xz", ".gz", ""}) {
        try {
            std::string url = base + std::string(ext);
            log::debug("Pobieranie indeksu: " + url);
            std::string raw = fetch_url_to_string(url);

            std::vector<uint8_t> raw_bytes(raw.begin(), raw.end());
            std::vector<uint8_t> decompressed = compress::decompress_auto(raw_bytes);
            return std::string(decompressed.begin(), decompressed.end());
        } catch (const std::exception& e) {
            log::debug("Nie udalo sie " + std::string(ext) + ": " + e.what());
            continue;
        }
    }

    throw std::runtime_error(
        "deb::DebFetcher: nie udalo sie pobrac indeksu Packages dla "
        + source.suite + "/" + component + " (probowano .xz, .gz, plain)");
}

/* ── fetch_packages_index_with_release_verify -- 0.1.0 ── */
/*
 * Poprawka (#3): weryfikacja SHA256 skompresowanej formy .xz/.gz
 * przed dekompresją. Poprzednia implementacja porównywała SHA256
 * zdekompresowanej treści z sumą z InRelease która jest dla pliku
 * skompresowanego -- zawsze dawała fałszywe ostrzeżenie "brak wpisu".
 *
 * Nowy algorytm:
 *   1. Pobierz surowe bajty (.xz lub .gz) bez dekompresji
 *   2. Wylicz SHA256 z surowych bajtów
 *   3. Porównaj z sumą z InRelease[component/binary-arch/Packages.xz]
 *   4. Dopiero po sukcesie zdekompresuj i zwróć treść
 *   5. Fallback na plain Packages (bez kompresji, SHA256 treści)
 */
std::string DebFetcher::fetch_packages_index_with_release_verify(
    const AptSource& source,
    const std::string& component,
    const std::unordered_map<std::string, std::string>& release_checksums,
    const std::string& arch)
{
    std::string rel_path_base = component + "/binary-" + arch + "/Packages";
    std::string base_url = source.base_url + "/dists/" + source.suite + "/"
                         + component + "/binary-" + arch + "/Packages";

    /* Próbujemy po kolei: .xz, .gz, plain */
    for (const char* sfx : {".xz", ".gz", ""}) {
        std::string url = base_url + sfx;
        std::string key = rel_path_base + sfx;
        bool has_checksum = release_checksums.count(key) > 0;

        std::string raw_bytes;
        try {
            raw_bytes = fetch_url_to_string_with_retry(url, 3, 2);
        } catch (const std::exception& e) {
            log::debug("fetch_packages: " + url + " niedostepny: " + e.what());
            continue;
        }

        /* Weryfikacja SHA256 surowych bajtów relative do InRelease (#3) */
        if (!release_checksums.empty() && has_checksum) {
            std::string actual_sha = sha256_string(raw_bytes);
            const std::string& expected_sha = release_checksums.at(key);
            if (actual_sha != expected_sha) {
                throw std::runtime_error(
                    "UWAGA BEZPIECZENSTWA: SHA256 indeksu " + key +
                    " NIE ZGADZA SIE z InRelease!\n"
                    "  InRelease: " + expected_sha + "\n"
                    "  Pobrany:   " + actual_sha + "\n"
                    "Mozliwy atak MITM lub uszkodzony mirror -- instalacja przerwana.");
            }
            log::debug("InRelease SHA256 OK: " + key);
        } else if (!release_checksums.empty() && !has_checksum) {
            log::debug("InRelease: brak sumy dla " + key + " -- pomijam weryfikacje");
        }

        /* Dekompresja */
        try {
            std::vector<uint8_t> raw_vec(raw_bytes.begin(), raw_bytes.end());
            std::vector<uint8_t> decompressed = compress::decompress_auto(raw_vec);
            return std::string(decompressed.begin(), decompressed.end());
        } catch (const std::exception& e) {
            log::debug("Dekompresja " + url + " nie powiodla sie: " + e.what());
            /* Jeśli plain (bez kompresji) -- dekompresja to no-op, nie powinna failować */
            if (std::string(sfx).empty())
                return raw_bytes; /* plain text -- zwróć bez dekompresji */
            continue;
        }
    }

    throw std::runtime_error(
        "deb::DebFetcher: nie udalo sie pobrac indeksu Packages dla "
        + source.suite + "/" + component + " (probowano .xz, .gz, plain)");
}

/* ── fetch_deb_package ── */

void DebFetcher::fetch_deb_package(const std::string& base_url,
                                   const std::string& filename,
                                   const std::string& dest_path,
                                   const std::string& expected_sha256) {
    /* Sprawdź czy plik już jest w tmp_dir (cache pobierania) */
    std::string cached = tmp_dir_ + "/" + fs::path(filename).filename().string();
    if (fs::exists(cached) && !expected_sha256.empty()) {
        if (sha256_hex(cached) == expected_sha256) {
            log::debug("Plik z cache pobierania: " + fs::path(filename).filename().string());
            fs::copy_file(cached, dest_path, fs::copy_options::overwrite_existing);
            return;
        }
        fs::remove(cached);
    }

    std::string url = base_url + "/" + filename;
    log::info("Pobieranie pakietu: " + fs::path(filename).filename().string());

    fetch_url_to_file(url, dest_path);

    if (!expected_sha256.empty()) {
        std::string actual = sha256_hex(dest_path);
        if (actual != expected_sha256) {
            fs::remove(dest_path);
            throw std::runtime_error(
                "deb::DebFetcher: SUMA KONTROLNA SIE NIE ZGADZA dla " + filename +
                "\n  oczekiwano: " + expected_sha256 +
                "\n  otrzymano:  " + actual +
                "\nTo moze oznaczac uszkodzony transfer LUB podmieniony pakiet -- "
                "instalacja przerwana ze wzgledow bezpieczenstwa.");
        }
        log::debug("SHA256 zweryfikowane: " + filename);

        /* Zapisz do cache pobierania */
        std::error_code ec;
        fs::copy_file(dest_path, cached, fs::copy_options::overwrite_existing, ec);
    } else {
        log::warn("Brak oczekiwanej sumy SHA256 dla " + filename +
                  " -- pobrano BEZ weryfikacji integralnosci.");
    }
}

} // namespace debostree::deb
