#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int install(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree install <pakiet> [<pakiet2>...]\n";
        return 1;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);

        auto booted = sysroot.booted_deployment();
        if (!booted) {
            log::error("Brak aktualnie zabootowanego deploymentu OSTree.");
            return 1;
        }

        /* -- 1. checkout aktualnego commita do katalogu tymczasowego -- */
        std::string base = cfg.overlay_work_dir + "/base-checkout";
        fs::remove_all(base);
        fs::create_directories(base);

        log::info("Checkout bazy: " + booted->checksum.substr(0, 12) + "...");
        sysroot.repo().checkout_commit(booted->checksum, base);

        /* -- 2. overlayfs nad bazą -- */
        OverlayManager ovl(cfg.overlay_work_dir + "/session");
        OverlaySession  ses = ovl.begin_session(base);
        ovl.bind_mount_virtual_fs(ses);

        /* -- 3. apt-get install w overlayu -- */
        std::vector<PackageLayer> resolved;
        bool   failed = false;
        std::string errmsg;
        try {
            DebLayer deb(cfg);
            deb.refresh_package_index(ses);
            resolved = deb.install_packages(ses, args);
        } catch (const std::exception& e) {
            failed = true; errmsg = e.what();
        }

        ovl.unbind_virtual_fs(ses);
        if (failed) {
            ovl.discard_session(ses);
            fs::remove_all(base);
            log::error("Instalacja nie powiodla sie: " + errmsg);
            return 1;
        }
        ovl.end_session(ses);

        /* -- 4. kopiujemy merged (lower+upper) do final-tree -- */
        std::string final_tree = cfg.overlay_work_dir + "/final-tree";
        fs::remove_all(final_tree);
        fs::create_directories(final_tree);

        /* Re-montujemy overlay by skopiowac scalony widok na plasko.
         * upper_dir zostal (end_session nie usuwa go), wiec sesja "widzi"
         * dokladnie ten sam stan co po apt-get install.                 */
        OverlaySession ro_ses = ovl.begin_session(base);
        std::error_code ec;
        fs::copy(ro_ses.merged_dir, final_tree,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        ovl.end_session(ro_ses);

        if (ec) {
            log::error("Kopiowanie drzewa: " + ec.message());
            fs::remove_all(base);
            return 1;
        }

        /* -- 5. commit do OSTree -- */
        log::info("Commituje nowe drzewo do OSTree...");
        std::string subject = "deb-ostree install:";
        for (auto& p : args) subject += " " + p;

        std::string new_csum = sysroot.repo().commit_directory(
            final_tree, booted->origin_refspec, subject);

        /* -- 6. scal liste pakietow i deploy -- */
        std::vector<PackageLayer> all_pkgs = booted->layered_packages;
        for (auto& np : resolved) {
            bool dup = false;
            for (auto& ep : all_pkgs) if (ep.name == np.name) { dup = true; break; }
            if (!dup) all_pkgs.push_back(np);
        }

        auto res = sysroot.deploy_commit(
            new_csum, cfg.osname, booted->origin_refspec, all_pkgs);

        fs::remove_all(base);
        fs::remove_all(final_tree);

        if (!res.success) {
            log::error("Deploy nie powiodl sie: " + res.error_message);
            return 1;
        }

        std::cout << "Zainstalowano:";
        for (auto& a : args) std::cout << " " << a;
        std::cout << "\nNowy deployment: " << new_csum.substr(0, 12) << "\n";
        std::cout << "Wykonaj reboot, aby zmiany weszly w zycie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("install: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
