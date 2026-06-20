#include "../cmd/state_store.h"
#include "../cmd/hk_parser.h"
#include "../cmd/logging.h"

namespace debostree::state {

Config load_config(const std::string& path) {
    Config cfg; /* wartosci domyslne z types.h */

    debostree::hk::HkDocument doc;
    try {
        doc = debostree::hk::HkDocument::loadFile(path);
    } catch (const std::exception& e) {
        log::debug("Brak lub blad pliku " + path + " (" + e.what() +
                  ") -- uzywam wartosci domyslnych.");
        return cfg;
    }

    cfg.sysroot_path     = doc.getOr("sysroot", "path",       cfg.sysroot_path);
    cfg.ostree_repo_path = doc.getOr("ostree",  "repo_path",  cfg.ostree_repo_path);
    cfg.osname           = doc.getOr("system",  "osname",     cfg.osname);
    cfg.overlay_work_dir = doc.getOr("overlay", "work_dir",   cfg.overlay_work_dir);
    cfg.apt_lists_path   = doc.getOr("apt",     "lists_path", cfg.apt_lists_path);

    log::debug("Konfiguracja wczytana z " + path);
    return cfg;
}

} // namespace debostree::state
