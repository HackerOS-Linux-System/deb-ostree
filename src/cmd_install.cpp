#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int install(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Użycie: deb-ostree install <pakiet> [<pakiet2>...]\n";
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
        {
            progress::ScopedSpinner sp("Checkout bazy OSTree " +
                                       booted->checksum.substr(0, 12));
            std::string base = cfg.overlay_work_dir + "/base-checkout";
            fs::remove_all(base);
            fs::create_directories(base);
            sysroot.repo().checkout_commit(booted->checksum, base);
            sp.done("gotowe");
        }

        std::string base = cfg.overlay_work_dir + "/base-checkout";

        /* -- 2. overlayfs nad bazą -- */
        OverlayManager ovl(cfg.overlay_work_dir + "/session");
        OverlaySession  ses = ovl.begin_session(base);
        ovl.bind_mount_virtual_fs(ses);

        /* -- 3. Instalacja przez DebLayer (z własnym ProgressBar wewnątrz) -- */
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
            log::error("Instalacja nie powiodła się: " + errmsg);
            return 1;
        }
        ovl.end_session(ses);

        /* -- 4. Kopiujemy merged (lower+upper) do final-tree -- */
        {
            progress::ScopedSpinner sp("Scalanie drzewa plików");
            std::string final_tree = cfg.overlay_work_dir + "/final-tree";
            fs::remove_all(final_tree);
            fs::create_directories(final_tree);

            OverlaySession ro_ses = ovl.begin_session(base);
            std::error_code ec;
            fs::copy(ro_ses.merged_dir, final_tree,
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            ovl.end_session(ro_ses);

            if (ec) {
                sp.fail("Błąd kopiowania: " + ec.message());
                log::error("Kopiowanie drzewa: " + ec.message());
                fs::remove_all(base);
                return 1;
            }
            sp.done();
        }

        /* -- 5. commit do OSTree -- */
        std::string final_tree = cfg.overlay_work_dir + "/final-tree";
        std::string new_csum;
        {
            progress::ScopedSpinner sp("Commit do OSTree");
            std::string subject = "deb-ostree install:";
            for (auto& p : args) subject += " " + p;

            new_csum = sysroot.repo().commit_directory(
                final_tree, booted->origin_refspec, subject);
            sp.done(new_csum.substr(0, 12));
        }

        /* -- 6. Scal listę pakietów i deploy -- */
        {
            progress::ScopedSpinner sp("Rejestracja deploymentu");

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
                sp.fail("Deploy nie powiódł się: " + res.error_message);
                log::error("Deploy nie powiódł się: " + res.error_message);
                return 1;
            }
            sp.done(new_csum.substr(0, 12));
        }

        std::cout << "\nZainstalowano:";
        for (auto& a : args) std::cout << " " << a;
        std::cout << "\nNowy deployment: " << new_csum.substr(0, 12) << "\n";
        std::cout << "Wykonaj reboot, aby zmiany weszły w życie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("install: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
