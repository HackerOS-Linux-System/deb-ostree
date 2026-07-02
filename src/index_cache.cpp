#include "../cmd/index_cache.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace debostree::cache {

/* ── Helpers ── */

static uint64_t unix_now() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

/* Zamienia URL na bezpieczną nazwę pliku (zastępuje :/? znakiem _). */
static std::string url_to_key(const std::string& url) {
    std::string key;
    key.reserve(url.size());
    for (char c : url) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '.' || c == '-') {
            key += c;
        } else {
            key += '_';
        }
    }
    /* Usuń wielokrotne podkreślenia */
    std::string out;
    bool prev_under = false;
    for (char c : key) {
        if (c == '_' && prev_under) continue;
        out += c;
        prev_under = (c == '_');
    }
    return out;
}

/* Minimalistyczny zapis/odczyt metadanych w formacie key=value */
static void write_meta(const std::string& path, uint64_t ts, bool gpg_ok,
                        const std::string& sha256 = "") {
    std::ofstream f(path, std::ios::trunc);
    f << "timestamp=" << ts << "\n"
      << "gpg_verified=" << (gpg_ok ? "1" : "0") << "\n"
      << "sha256=" << sha256 << "\n";
}

static bool read_meta(const std::string& path, uint64_t& ts, bool& gpg_ok) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    ts = 0; gpg_ok = false;
    while (std::getline(f, line)) {
        if (line.rfind("timestamp=", 0) == 0)
            ts = std::stoull(line.substr(10));
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

std::string IndexCache::make_key(const std::string& base_url,
                                  const std::string& suite,
                                  const std::string& component) const {
    return url_to_key(base_url) + "_" + suite + "_" + component;
}

std::string IndexCache::packages_path(const std::string& key) const {
    return lists_dir_ + "/" + key + "_Packages";
}

std::string IndexCache::meta_path(const std::string& key) const {
    return lists_dir_ + "/" + key + "_Packages.meta";
}

std::string IndexCache::inrelease_path(const std::string& key) const {
    /* InRelease jest per-suite, nie per-component.
     * klucz = url_key + "_" + suite + "_" + component
     * Wycinamy ostatni segment (_component) żeby uzyskać klucz per-suite. */
    size_t last_under = key.rfind('_');
    std::string suite_key = (last_under != std::string::npos)
                            ? key.substr(0, last_under) : key;
    return lists_dir_ + "/" + suite_key + "_InRelease";
}

uint64_t IndexCache::current_unix_time() const {
    return unix_now();
}

std::optional<CacheEntry> IndexCache::get(const std::string& base_url,
                                           const std::string& suite,
                                           const std::string& component) const
{
    std::string key  = make_key(base_url, suite, component);
    std::string pkgs = packages_path(key);
    std::string meta = meta_path(key);

    if (!fs::exists(pkgs) || !fs::exists(meta)) return std::nullopt;

    uint64_t ts; bool gpg_ok;
    if (!read_meta(meta, ts, gpg_ok)) return std::nullopt;

    uint64_t now = unix_now();
    if (now - ts > max_age_s_) {
        log::debug("cache: wpis " + key + " wygasł (" +
                   std::to_string((now - ts) / 3600) + "h temu)");
        return std::nullopt;
    }

    /* Wczytaj Packages */
    std::ifstream pf(pkgs, std::ios::binary);
    if (!pf.is_open()) return std::nullopt;
    std::ostringstream buf;
    buf << pf.rdbuf();

    /* Wczytaj InRelease jeśli istnieje */
    std::string inrelease;
    std::string ir_path = inrelease_path(key);
    if (fs::exists(ir_path)) {
        std::ifstream irf(ir_path);
        std::ostringstream ibuf;
        ibuf << irf.rdbuf();
        inrelease = ibuf.str();
    }

    log::debug("cache: trafienie dla " + suite + "/" + component +
               " (wiek: " + std::to_string((now - ts) / 60) + " min)");

    CacheEntry ce;
    ce.packages_content  = buf.str();
    ce.inrelease_content = std::move(inrelease);
    ce.cached_at_unix    = ts;
    ce.gpg_verified      = gpg_ok;
    return ce;
}

void IndexCache::put(const std::string& base_url,
                     const std::string& suite,
                     const std::string& component,
                     const CacheEntry& entry)
{
    std::string key  = make_key(base_url, suite, component);
    std::string pkgs = packages_path(key);
    std::string meta = meta_path(key);

    fs::create_directories(lists_dir_);

    {
        std::ofstream f(pkgs, std::ios::binary | std::ios::trunc);
        f << entry.packages_content;
    }

    write_meta(meta, entry.cached_at_unix > 0 ? entry.cached_at_unix : unix_now(),
               entry.gpg_verified);

    /* Zapisz InRelease jeśli podano */
    if (!entry.inrelease_content.empty()) {
        std::ofstream irf(inrelease_path(key), std::ios::trunc);
        irf << entry.inrelease_content;
    }

    log::debug("cache: zapisano " + suite + "/" + component);
}

void IndexCache::clear() {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(lists_dir_, ec)) {
        fs::remove(entry.path(), ec);
    }
    log::info("cache: wyczyszczono indeksy apt.");
}

void IndexCache::prune() {
    uint64_t now = unix_now();
    std::error_code ec;
    int removed = 0;

    for (auto& entry : fs::directory_iterator(lists_dir_, ec)) {
        auto path = entry.path().string();
        /* ends_with() jest C++20 -- używamy rfind jako substytut C++17 */
        static const std::string meta_sfx = ".meta";
        bool is_meta = path.size() >= meta_sfx.size() &&
                       path.compare(path.size() - meta_sfx.size(),
                                    meta_sfx.size(), meta_sfx) == 0;
        if (is_meta) {
            uint64_t ts; bool gpg_ok;
            if (read_meta(path, ts, gpg_ok) && (now - ts > max_age_s_)) {
                /* Usun powiazane pliki -- uzyj meta_sfx.size() dla spojnosci */
                std::string base = path.substr(0, path.size() - meta_sfx.size());
                fs::remove(base, ec);
                fs::remove(path, ec);
                ++removed;
            }
        }
    }

    if (removed > 0)
        log::info("cache: usunięto " + std::to_string(removed) + " wygasłych wpisów.");
}

} // namespace debostree::cache
