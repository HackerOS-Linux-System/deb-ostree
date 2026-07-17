#include "../cmd/index_cache.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace debostree::cache {

/* ── helpers ── */

static uint64_t unix_now() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

/* Konwertuje URL na prefix pliku w stylu apt:
 * http://deb.debian.org/debian -> deb.debian.org_debian
 * (apt uzywa: host_path z '/' zastapione '_')
 */
static std::string url_to_apt_prefix(const std::string& base_url) {
    std::string url = base_url;
    /* Usun schemat (http:// https://) */
    size_t scheme = url.find("://");
    if (scheme != std::string::npos) url = url.substr(scheme + 3);
    /* Usun trailing slash */
    while (!url.empty() && url.back() == '/') url.pop_back();
    /* Zamien '/' i ':' na '_' */
    std::string result;
    for (char c : url) {
        result += (c == '/' || c == ':') ? '_' : c;
    }
    return result;
}

/* Buduje nazwe pliku Packages w stylu apt:
 * <host_prefix>_dists_<suite>_<component>_binary-<arch>_Packages
 * np. deb.debian.org_debian_dists_bookworm_main_binary-amd64_Packages
 */
static std::string apt_packages_filename(const std::string& base_url,
                                          const std::string& suite,
                                          const std::string& component,
                                          const std::string& arch = "amd64") {
    return url_to_apt_prefix(base_url) + "_dists_" + suite + "_"
         + component + "_binary-" + arch + "_Packages";
}

/* Plik metadanych TTL (tylko dla deb-ostree, apt go ignoruje) */
static std::string meta_filename(const std::string& base_url,
                                  const std::string& suite,
                                  const std::string& component,
                                  const std::string& arch = "amd64") {
    return apt_packages_filename(base_url, suite, component, arch) + ".deb-ostree-meta";
}

static void write_meta(const std::string& path, uint64_t ts, bool gpg_ok) {
    std::ofstream f(path, std::ios::trunc);
    f << "timestamp=" << ts << "\n"
      << "gpg_verified=" << (gpg_ok ? "1" : "0") << "\n";
}

static bool read_meta(const std::string& path, uint64_t& ts, bool& gpg_ok) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    ts = 0; gpg_ok = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("timestamp=", 0) == 0)
            try { ts = std::stoull(line.substr(10)); } catch (...) {}
        if (line.rfind("gpg_verified=", 0) == 0)
            gpg_ok = (line.substr(13) == "1");
    }
    return ts > 0;
}

/* ── IndexCache ── */

IndexCache::IndexCache(std::string lists_dir, uint64_t max_age_s)
    : lists_dir_(std::move(lists_dir))
    , max_age_s_(max_age_s)
{
    std::error_code ec;
    fs::create_directories(lists_dir_, ec);
}

std::optional<CacheEntry> IndexCache::get(const std::string& base_url,
                                           const std::string& suite,
                                           const std::string& component) const
{
    /* Sprawdz architekture z nazwy pliku -- domyslnie amd64 */
    std::string arch = "amd64";
    std::string pkgs_path = lists_dir_ + "/" +
                            apt_packages_filename(base_url, suite, component, arch);
    std::string meta_path = lists_dir_ + "/" +
                            meta_filename(base_url, suite, component, arch);

    if (!fs::exists(pkgs_path)) return std::nullopt;

    /* Sprawdz TTL przez meta file */
    uint64_t ts = 0; bool gpg_ok = false;
    if (fs::exists(meta_path)) {
        if (!read_meta(meta_path, ts, gpg_ok)) return std::nullopt;
        if (unix_now() - ts > max_age_s_) {
            log::debug("cache: wygasly " + suite + "/" + component);
            return std::nullopt;
        }
    } else {
        /* Brak meta -- plik mogl byc zapisany przez apt.
         * Uzywamy stat() do pobrania mtime (C++17, bez file_clock C++20). */
        struct ::stat st{};
        if (::stat(pkgs_path.c_str(), &st) == 0) {
            ts = static_cast<uint64_t>(st.st_mtime);
            if (unix_now() - ts > max_age_s_) {
                log::debug("cache: wygasly (mtime) " + suite + "/" + component);
                return std::nullopt;
            }
            gpg_ok = true;
            log::debug("cache: trafienie apt (mtime) " + suite + "/" + component);
        }
    }

    std::ifstream pf(pkgs_path, std::ios::binary);
    if (!pf.is_open()) return std::nullopt;
    std::ostringstream buf;
    buf << pf.rdbuf();

    /* Wczytaj InRelease jesli istnieje */
    std::string inrelease;
    std::string ir_path = lists_dir_ + "/" +
                         url_to_apt_prefix(base_url) + "_dists_" + suite + "_InRelease";
    if (fs::exists(ir_path)) {
        std::ifstream irf(ir_path);
        std::ostringstream ibuf;
        ibuf << irf.rdbuf();
        inrelease = ibuf.str();
    }

    uint64_t age_min = (unix_now() - ts) / 60;
    log::debug("cache: trafienie " + suite + "/" + component +
               " (wiek: " + std::to_string(age_min) + " min, plik: " +
               apt_packages_filename(base_url, suite, component, arch) + ")");

    CacheEntry ce;
    ce.packages_content  = buf.str();
    ce.inrelease_content = inrelease;
    ce.cached_at_unix    = ts;
    ce.gpg_verified      = gpg_ok;
    return ce;
}

void IndexCache::put(const std::string& base_url,
                     const std::string& suite,
                     const std::string& component,
                     const CacheEntry& entry)
{
    std::string arch = "amd64";
    std::string pkgs_path = lists_dir_ + "/" +
                            apt_packages_filename(base_url, suite, component, arch);
    std::string meta_path = lists_dir_ + "/" +
                            meta_filename(base_url, suite, component, arch);

    fs::create_directories(lists_dir_);

    /* Zapisz Packages -- identyczny format i sciezka co apt */
    {
        std::ofstream f(pkgs_path, std::ios::binary | std::ios::trunc);
        f << entry.packages_content;
    }

    /* Metadane TTL (dodatkowy plik, apt go ignoruje) */
    write_meta(meta_path,
               entry.cached_at_unix > 0 ? entry.cached_at_unix : unix_now(),
               entry.gpg_verified);

    /* Zapisz InRelease jesli podano */
    if (!entry.inrelease_content.empty()) {
        std::string ir_path = lists_dir_ + "/" +
            url_to_apt_prefix(base_url) + "_dists_" + suite + "_InRelease";
        std::ofstream irf(ir_path, std::ios::trunc);
        irf << entry.inrelease_content;
    }

    log::debug("cache: zapisano " + pkgs_path);
}

void IndexCache::clear() {
    std::error_code ec;
    int removed = 0;
    for (auto& entry : fs::directory_iterator(lists_dir_, ec)) {
        /* Usun tylko nasze pliki meta -- nie usuwaj plikow apt */
        auto name = entry.path().filename().string();
        static const std::string deb_sfx = ".deb-ostree-meta";
        bool is_meta = name.size() >= deb_sfx.size() &&
                       name.compare(name.size() - deb_sfx.size(),
                                    deb_sfx.size(), deb_sfx) == 0;
        if (is_meta) {
            fs::remove(entry.path(), ec);
            /* Usun tez odpowiadajacy plik Packages jezeli istnieje */
            std::string pkgs = entry.path().string().substr(
                0, entry.path().string().size() - deb_sfx.size());
            fs::remove(pkgs, ec);
            ++removed;
        }
    }
    log::info("cache: wyczyszczono " + std::to_string(removed) + " wpisow.");
}

void IndexCache::prune() {
    uint64_t now = unix_now();
    std::error_code ec;
    int removed = 0;
    static const std::string meta_sfx = ".deb-ostree-meta";

    for (auto& entry : fs::directory_iterator(lists_dir_, ec)) {
        auto path = entry.path().string();
        bool is_meta = path.size() >= meta_sfx.size() &&
                       path.compare(path.size() - meta_sfx.size(),
                                    meta_sfx.size(), meta_sfx) == 0;
        if (is_meta) {
            uint64_t ts; bool gpg_ok;
            if (read_meta(path, ts, gpg_ok) && (now - ts > max_age_s_)) {
                std::string base = path.substr(0, path.size() - meta_sfx.size());
                fs::remove(base, ec);
                fs::remove(path, ec);
                ++removed;
            }
        }
    }
    if (removed > 0)
        log::info("cache: usunieto " + std::to_string(removed) + " wygaslych wpisow.");
}

} // namespace debostree::cache
