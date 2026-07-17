#pragma once
/*
 * deb-ostree -- index_cache.h  [NOWY v0.1.0]
 * Cache indeksów Packages apt na dysku -- odpowiednik /var/lib/apt/lists/.
 *
 * Cache przechowuje zdekompresowane indeksy Packages + metadane walidacji
 * (timestamp pobrania, sha256 z InRelease) w katalogu apt_lists_path.
 * Plik indeksu jest ważny przez max_age_seconds (domyślnie 24h).
 *
 * Struktura katalogów:
 *   <lists_path>/
 *     <host>_<suite>_<component>_Packages      -- zdekompresowany Packages
 *     <host>_<suite>_<component>_Packages.meta -- JSON z timestamp/sha256
 *     <host>_<suite>_InRelease                 -- podpisany plik InRelease
 *
 * Wersja: 0.2.0
 */

#include <string>
#include <optional>
#include <cstdint>

namespace debostree::cache {

struct CacheEntry {
    std::string packages_content;  /* zdekompresowana treść Packages */
    std::string inrelease_content; /* treść InRelease (do weryfikacji) */
    uint64_t    cached_at_unix = 0;/* timestamp pobrania (unix seconds) */
    bool        gpg_verified   = false;
};

class IndexCache {
public:
    /* lists_dir: ścieżka do katalogu cache (Config.apt_lists_path).
     * max_age_s:  czas ważności wpisu w sekundach (domyślnie 24h). */
    explicit IndexCache(std::string lists_dir, uint64_t max_age_s = 86400);

    /* Zwraca cached Packages jeśli istnieje i nie wygasł, nullopt w p.p. */
    std::optional<CacheEntry> get(const std::string& base_url,
                                  const std::string& suite,
                                  const std::string& component) const;

    /* Zapisuje nowy wpis do cache. */
    void put(const std::string& base_url,
             const std::string& suite,
             const std::string& component,
             const CacheEntry& entry);

    /* Usuwa wszystkie wpisy cache (odpowiednik "apt-get clean"). */
    void clear();

    /* Usuwa wpisy starsze niż max_age_s. */
    void prune();

private:
    std::string lists_dir_;
    uint64_t    max_age_s_;

    /* Buduje bezpieczną nazwę pliku z URL i nazw sekcji. */
    std::string make_key(const std::string& base_url,
                         const std::string& suite,
                         const std::string& component) const;

    std::string packages_path(const std::string& key) const;
    std::string meta_path(const std::string& key) const;
    std::string inrelease_path(const std::string& key) const;

    uint64_t current_unix_time() const;
};

} // namespace debostree::cache
