#include "../cmd/state_store.h"
#include "../cmd/hk_parser.h"
#include "../cmd/sources_parser.h"
#include "../cmd/logging.h"

namespace debostree::state {

Config load_config(const std::string& path) {
    Config cfg;

    debostree::hk::HkDocument doc;
    try {
        doc = debostree::hk::HkDocument::loadFile(path);
    } catch (const std::exception& e) {
        log::debug("Brak lub blad pliku " + path + " (" + e.what() +
                  ") -- uzywam wartosci domyslnych.");
        /* Brak pliku .hk -- zrodla brane wylacznie z /etc/apt/ */
    }

    cfg.sysroot_path     = doc.getOr("sysroot", "path",       cfg.sysroot_path);
    cfg.ostree_repo_path = doc.getOr("ostree",  "repo_path",  cfg.ostree_repo_path);
    cfg.osname           = doc.getOr("system",  "osname",     cfg.osname);
    cfg.overlay_work_dir = doc.getOr("overlay", "work_dir",   cfg.overlay_work_dir);

    /* Sciezki apt -- mozna nadpisac w .hk, domyslnie identyczne z apt */
    cfg.apt_lists_path   = doc.getOr("apt", "lists_path",   cfg.apt_lists_path);
    cfg.apt_sources_list = doc.getOr("apt", "sources_list", cfg.apt_sources_list);
    cfg.apt_sources_dir  = doc.getOr("apt", "sources_dir",  cfg.apt_sources_dir);
    cfg.keyring_dir      = doc.getOr("apt", "keyring_dir",  cfg.keyring_dir);
    cfg.arch             = doc.getOr("apt", "arch",         cfg.arch);
    cfg.confext_mode     = doc.getOr("apt", "confext_mode", cfg.confext_mode);

    /* === Ladowanie zrodel apt ===
     *
     * Priorytet:
     * 1. /etc/apt/sources.list + /etc/apt/sources.list.d/  (system apt)
     * 2. source_N w .hk (fallback dla srodowisk bez apt, np. CI/builder)
     *
     * Dzieki temu deb-ostree automatycznie uzywa zrodel skonfigurowanych
     * przez administratora w standardowych plikach apt, bez powielania
     * konfiguracji. */
    cfg.apt_sources = sources::load_sources(cfg.apt_sources_list,
                                             cfg.apt_sources_dir);

    if (!cfg.apt_sources.empty()) {
        log::debug("Zaladowano " + std::to_string(cfg.apt_sources.size()) +
                   " zrodel z " + cfg.apt_sources_list);
    } else {
        /* Fallback: source_N z .hk */
        for (int i = 1; i <= 32; ++i) {
            std::string key = "source_" + std::to_string(i);
            if (!doc.has("apt", key)) break;
            std::string src = doc.getOr("apt", key, "");
            if (!src.empty()) cfg.apt_sources.push_back(src);
        }

        if (!cfg.apt_sources.empty()) {
            log::debug("Zaladowano " + std::to_string(cfg.apt_sources.size()) +
                       " zrodel z .hk (fallback -- brak /etc/apt/sources.list)");
        } else {
            log::warn("Brak zrodel apt. Dodaj do /etc/apt/sources.list lub .hk -> source_N");
        }
    }

    return cfg;
}

} // namespace debostree::state
