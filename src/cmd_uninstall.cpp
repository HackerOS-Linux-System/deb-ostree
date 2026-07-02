#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/tree_export.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int uninstall(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree uninstall <pakiet> [<pakiet2>...]\n";
        return 1;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) { log::error("Brak zabootowanego deploymentu."); return 1; }

        /* Etap 1: checkout bazy */
        {
            progress::ScopedSpinner sp("Checkout bazy OSTree " +
                                       booted->checksum.substr(0, 12));
            std::string base = cfg.overlay_work_dir + "/base-checkout";
            fs::remove_all(base);
            fs::create_directories(base);
            sysroot.repo().checkout_commit(booted->checksum, base);
            sp.done();
        }

        std::string base = cfg.overlay_work_dir + "/base-checkout";
        OverlayManager ovl(cfg.overlay_work_dir + "/session");
        OverlaySession ses = ovl.begin_session(base);
        ovl.bind_mount_virtual_fs(ses);

        /* Etap 2-4: DebLayer::remove_packages (z wbudowanym progress barem) */
        bool failed = false; std::string errmsg;
        try {
            DebLayer deb(cfg);
            deb.remove_packages(ses, args);
        } catch (const std::exception& e) { failed = true; errmsg = e.what(); }

        ovl.unbind_virtual_fs(ses);
        if (failed) {
            ovl.discard_session(ses);
            fs::remove_all(base);
            log::error("Usuniecie nie powiodlo sie: " + errmsg);
            return 1;
        }
        ovl.end_session(ses);

        /* Etap 5: eksport scalonego drzewa (tree_export zamiast fs::copy) */
        std::string final_tree = cfg.overlay_work_dir + "/final-tree";
        {
            progress::ScopedSpinner sp("Scalanie drzewa plikow");
            fs::remove_all(final_tree);
            try {
                tree::export_overlay_merged(ses.merged_dir, final_tree);
                sp.done();
            } catch (const std::exception& e) {
                sp.fail("Blad eksportu: " + std::string(e.what()));
                fs::remove_all(base);
                return 1;
            }
        }

        /* Etap 6: commit do OSTree */
        std::string new_csum;
        {
            progress::ScopedSpinner sp("Commit do OSTree");
            std::string subject = "deb-ostree uninstall:";
            for (auto& p : args) subject += " " + p;
            new_csum = sysroot.repo().commit_directory(
                final_tree, booted->origin_refspec, subject);
            sp.done(new_csum.substr(0, 12));
        }

        /* Etap 7: deploy */
        {
            progress::ScopedSpinner sp("Rejestracja deploymentu");

            std::vector<PackageLayer> remaining;
            for (auto& ep : booted->layered_packages) {
                bool removed = false;
                for (auto& n : args) if (ep.name == n) { removed = true; break; }
                if (!removed) remaining.push_back(ep);
            }

            auto res = sysroot.deploy_commit(
                new_csum, cfg.osname, booted->origin_refspec, remaining);

            fs::remove_all(base);
            fs::remove_all(final_tree);

            if (!res.success) {
                sp.fail("Deploy: " + res.error_message);
                log::error("Deploy: " + res.error_message);
                return 1;
            }
            sp.done(new_csum.substr(0, 12));
        }

        std::cout << "Usunieto pakiet(y). Wykonaj reboot, aby zmiany weszly w zycie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("uninstall: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
