#include "../cmd/state_store.h"
#include "../cmd/hk_parser.h"
#include "../cmd/logging.h"

namespace debostree::state {

Config load_config(const std::string& path) {
    Config cfg; /* wartości domyślne z types.h */

    debostree::hk::HkDocument doc;
    try {
        doc = debostree::hk::HkDocument::loadFile(path);
    } catch (const std::exception& e) {
        log::debug("Brak lub błąd pliku " + path + " (" + e.what() +
                  ") -- używam wartości domyślnych.");
        return cfg;
    }

    cfg.sysroot_path     = doc.getOr("sysroot", "path",       cfg.sysroot_path);
    cfg.ostree_repo_path = doc.getOr("ostree",  "repo_path",  cfg.ostree_repo_path);
    cfg.osname           = doc.getOr("system",  "osname",     cfg.osname);
    cfg.overlay_work_dir = doc.getOr("overlay", "work_dir",   cfg.overlay_work_dir);
    cfg.apt_lists_path   = doc.getOr("apt",     "lists_path", cfg.apt_lists_path);
    cfg.arch             = doc.getOr("apt",     "arch",       cfg.arch);

    /* Parsowanie apt_sources: próbujemy source_1 .. source_32.
     * Zatrzymujemy się przy pierwszej luce (brak source_N oznacza koniec listy).
     * Maksimum 32 źródła to praktyczny limit -- standardowe konfiguracje
     * Debiana mają 2-6 źródeł (main + security + backports + contrib/non-free). */
    for (int i = 1; i <= 32; ++i) {
        std::string key = "source_" + std::to_string(i);
        if (!doc.has("apt", key)) break;
        std::string src = doc.getOr("apt", key, "");
        if (!src.empty()) {
            cfg.apt_sources.push_back(src);
            log::debug("apt_source[" + std::to_string(i) + "]: " + src);
        }
    }

    if (cfg.apt_sources.empty()) {
        log::warn("Brak sekcji [apt] -> source_N w " + path +
                  " -- deb-ostree nie będzie mógł instalować pakietów.\n"
                  "  Dodaj np.: -> source_1 => deb http://deb.debian.org/debian bookworm main");
    } else {
        log::debug("Załadowano " + std::to_string(cfg.apt_sources.size()) +
                  " źródeł apt z " + path);
    }

    log::debug("Konfiguracja wczytana z " + path);
    return cfg;
}

} // namespace debostree::state
