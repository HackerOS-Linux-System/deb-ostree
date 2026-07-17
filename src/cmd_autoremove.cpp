#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/tree_export.h"
#include "../cmd/solv_pool.h"
#include "../cmd/pool_builder.h"
#include "../cmd/status_db.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int autoremove(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) { log::error("Brak zabootowanego deploymentu."); return 1; }

        std::string rootfs_path;
        try { rootfs_path = sysroot.deployment_path(*booted); }
        catch (...) { rootfs_path = cfg.sysroot_path; }

        auto installed = statusdb::load(rootfs_path);
        if (installed.empty()) {
            std::cout << "Brak zainstalowanych pakietow warstwowych.\n";
            return 0;
        }

        /* Buduj SolvPool */
        {
            progress::ScopedSpinner sp("Ladowanie indeksow i analiza zaleznosci");
            (void)sp;
        }

        progress::ProgressBar bar("Autoremove",
                                  static_cast<int>(cfg.apt_sources.size()));
        bar.begin_stage("Ladowanie indeksow");
        solv::SolvPool pool = [&]() {
            try { return build_solv_pool(cfg, bar, rootfs_path); }
            catch (...) { bar.fail("Blad indeksow"); throw; }
        }();
        bar.end_stage();

        /* Znajdz osierocon e zaleznosci */
        bar.begin_stage("Analiza osierocon ych zaleznosci");
        bar.spin("libsolv CLEANDEPS...");

        std::vector<std::string> to_remove;
        try {
            to_remove = pool.resolve_autoremove(installed, booted->layered_packages);
        } catch (const solv::SolvError& e) {
            bar.fail("Blad resolvera");
            log::error("autoremove resolver: " + std::string(e.what()));
            return 1;
        }
        bar.end_stage(std::to_string(to_remove.size()) + " do usuniecia");
        bar.finish("Analiza zakonczona");

        if (to_remove.empty()) {
            std::cout << "\nBrak osierocon ych zaleznosci do usuniecia.\n";
            return 0;
        }

        /* Wyswietl liste z WERSJAMI (#15) */
        std::cout << "\nOsierocon e zaleznosci do usuniecia ("
                  << to_remove.size() << "):\n\n";

        for (auto& pkg_name : to_remove) {
            std::string ver;
            for (auto& p : installed)
                if (p.name == pkg_name) { ver = p.version; break; }

            std::cout << "  \033[1;31m-\033[0m "
                      << std::left << std::setw(32) << pkg_name
                      << "  \033[2m" << ver << "\033[0m\n";
        }
        std::cout << "\n";

        /* Zapytaj o potwierdzenie */
        std::cout << "Kontynuowac? [T/n]: ";
        std::string ans;
        std::getline(std::cin, ans);
        if (!ans.empty() &&
            ans[0] != 'T' && ans[0] != 't' &&
            ans[0] != 'Y' && ans[0] != 'y') {
            std::cout << "Anulowano.\n";
            return 0;
        }

        /* Wykonaj usuniecie przez DebLayer bezposrednio (#15) */
        {
            progress::ScopedSpinner sp_co("Checkout bazy OSTree " +
                                          booted->checksum.substr(0, 12));
            std::string base = cfg.overlay_work_dir + "/base-checkout";
            fs::remove_all(base);
            fs::create_directories(base);
            sysroot.repo().checkout_commit(booted->checksum, base);
            sp_co.done();
        }

        std::string base = cfg.overlay_work_dir + "/base-checkout";
        OverlayManager ovl(cfg.overlay_work_dir + "/session");
        OverlaySession ses = ovl.begin_session(base);
        ovl.bind_mount_virtual_fs(ses);

        bool failed = false; std::string errmsg;
        try {
            DebLayer deb(cfg);
            deb.remove_packages(ses, to_remove);
        } catch (const std::exception& e) { failed = true; errmsg = e.what(); }

        ovl.unbind_virtual_fs(ses);
        if (failed) {
            ovl.discard_session(ses);
            fs::remove_all(base);
            log::error("autoremove nie powiodlo sie: " + errmsg);
            return 1;
        }
        ovl.end_session(ses);

        /* Eksport i commit */
        std::string final_tree = cfg.overlay_work_dir + "/final-tree";
        {
            progress::ScopedSpinner sp("Eksport drzewa plikow");
            fs::remove_all(final_tree);
            tree::export_overlay_merged(ses.merged_dir, final_tree);
            sp.done();
        }

        std::string new_csum;
        {
            progress::ScopedSpinner sp("Commit do OSTree");
            new_csum = sysroot.repo().commit_directory(
                final_tree, booted->origin_refspec,
                "deb-ostree autoremove: " + std::to_string(to_remove.size()) + " pkgs");
            sp.done(new_csum.substr(0, 12));
        }

        {
            progress::ScopedSpinner sp("Rejestracja deploymentu");
            std::vector<PackageLayer> remaining;
            for (auto& ep : booted->layered_packages) {
                bool removed = false;
                for (auto& n : to_remove) if (ep.name == n) { removed = true; break; }
                if (!removed) remaining.push_back(ep);
            }
            auto res = sysroot.deploy_commit(
                new_csum, cfg.osname, booted->origin_refspec, remaining);
            fs::remove_all(base);
            fs::remove_all(final_tree);
            if (!res.success) {
                sp.fail("Deploy: " + res.error_message);
                return 1;
            }
            sp.done(new_csum.substr(0, 12));
        }

        std::cout << "\nUsunieto " << to_remove.size()
                  << " pakietow. Wykonaj reboot aby zmiany weszly w zycie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("autoremove: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
