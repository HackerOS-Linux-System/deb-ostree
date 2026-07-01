#include "../cmd/deb_fetcher.h"
#include "../cmd/compress_util.h"
#include "../cmd/logging.h"

#include <curl/curl.h>
#include <openssl/evp.h>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdio>

namespace debostree::deb {

namespace {

/* Callback libcurl zapisujacy odebrane dane do std::string (uzywane przy
 * fetch_url_to_string). Sygnatura wymagana przez CURLOPT_WRITEFUNCTION. */
size_t write_to_string_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

/* Callback libcurl zapisujacy odebrane dane do otwartego std::ofstream
 * (uzywane przy fetch_url_to_file -- duze pliki .deb nie trafiaja cale
 * do pamieci, tylko strumieniowo na dysk). */
size_t write_to_file_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

std::string sha256_hex(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("deb::sha256_hex: nie mozna otworzyc " + path);

    /* Używamy EVP API (OpenSSL 1.1+ / 3.x) zamiast przestarzałego SHA256_CTX.
     * SHA256_Init/Update/Final są deprecated w OpenSSL 3.x i generują
     * ostrzeżenia -Wdeprecated-declarations. EVP_MD_CTX jest zalecanym
     * zamiennikiem i działa zarówno z OpenSSL 1.1 jak i 3.x. */
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

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

AptSource parse_apt_source_line(const std::string& line) {
    std::istringstream iss(trim(line));
    std::string type;
    iss >> type;

    if (type != "deb")
        throw std::runtime_error("deb::parse_apt_source_line: oczekiwano 'deb' na poczatku linii, otrzymano: " + type);

    AptSource src;
    iss >> src.base_url >> src.suite;

    std::string component;
    while (iss >> component) src.components.push_back(component);

    if (src.base_url.empty() || src.suite.empty())
        throw std::runtime_error("deb::parse_apt_source_line: niekompletna linia sources.list: " + line);

    return src;
}

DebFetcher::DebFetcher() {
    curl_handle_ = curl_easy_init();
    if (!curl_handle_)
        throw std::runtime_error("deb::DebFetcher: curl_easy_init() nie powiodlo sie");
}

DebFetcher::~DebFetcher() {
    if (curl_handle_) curl_easy_cleanup(static_cast<CURL*>(curl_handle_));
}

std::string DebFetcher::fetch_url_to_string(const std::string& url) {
    CURL* curl = static_cast<CURL*>(curl_handle_);
    curl_easy_reset(curl);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);          /* caly transfer, sekundy */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "deb-ostree/0.0.1");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);        /* HTTP >=400 -> CURLE_HTTP_RETURNED_ERROR */

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        throw std::runtime_error(
            "deb::DebFetcher: pobieranie " + url + " nie powiodlo sie: " + curl_easy_strerror(res));

    return response;
}

void DebFetcher::fetch_url_to_file(const std::string& url, const std::string& dest_path) {
    CURL* curl = static_cast<CURL*>(curl_handle_);
    curl_easy_reset(curl);

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw std::runtime_error("deb::DebFetcher: nie mozna zapisac " + dest_path);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);          /* pliki .deb moga byc duze */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "deb-ostree/0.0.1");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    out.close();

    if (res != CURLE_OK) {
        std::remove(dest_path.c_str());
        throw std::runtime_error(
            "deb::DebFetcher: pobieranie " + url + " nie powiodlo sie: " + curl_easy_strerror(res));
    }
}

std::string DebFetcher::fetch_packages_index(const AptSource& source, const std::string& component,
                                             const std::string& arch) {
    std::string base = source.base_url + "/dists/" + source.suite + "/" + component +
                       "/binary-" + arch + "/Packages";

    /* Probujemy kolejno .xz (najmniejszy transfer), .gz (powszechnie
     * dostepny), plain (fallback dla mirrorow bez kompresji -- rzadkie). */
    for (const char* ext : {".xz", ".gz", ""}) {
        try {
            std::string url = base + ext;
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
        "deb::DebFetcher: nie udalo sie pobrac indeksu Packages dla " +
        source.suite + "/" + component + " (probowano .xz, .gz, plain)");
}

void DebFetcher::fetch_deb_package(const std::string& base_url, const std::string& filename,
                                   const std::string& dest_path, const std::string& expected_sha256) {
    std::string url = base_url + "/" + filename;
    log::info("Pobieranie pakietu: " + url);

    fetch_url_to_file(url, dest_path);

    if (!expected_sha256.empty()) {
        std::string actual = sha256_hex(dest_path);
        if (actual != expected_sha256) {
            std::remove(dest_path.c_str());
            throw std::runtime_error(
                "deb::DebFetcher: SUMA KONTROLNA SIE NIE ZGADZA dla " + filename +
                "\n  oczekiwano: " + expected_sha256 +
                "\n  otrzymano:  " + actual +
                "\nTo moze oznaczac uszkodzony transfer LUB podmieniony pakiet -- "
                "instalacja przerwana ze wzgledow bezpieczenstwa.");
        }
        log::debug("SHA256 zweryfikowane: " + filename);
    } else {
        log::warn("Brak oczekiwanej sumy SHA256 dla " + filename +
                 " -- pobrano BEZ weryfikacji integralnosci.");
    }
}

} // namespace debostree::deb
