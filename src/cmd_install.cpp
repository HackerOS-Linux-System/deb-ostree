#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/tree_export.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"
#include "../cmd/oci_ref.h"

#include <iostream>
#include <filesystem>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace debostree::cmd {

/* Sprawdza wolne miejsce na partycji zawierajacej path.
 * Rzuca runtime_error jesli mniej niz min_bytes wolnego. */
static void check_disk_space(const std::string& path, uint64_t min_bytes,
                              const std::string& label) {
    struct statvfs st{};
    if (::statvfs(path.c_str(), &st) == 0) {
        uint64_t free_bytes = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
        if (free_bytes < min_bytes) {
            throw std::runtime_error(
                "Niewystarczajace miejsce na dysku dla " + label + ":\n"
                "  Dostepne: " + std::to_string(free_bytes / (1024*1024)) + " MB\n"
                "  Wymagane: " + std::to_string(min_bytes / (1024*1024)) + " MB\n"
                "  Sciezka:  " + path);
        }
        log::debug("Miejsce na dysku OK dla " + label + ": " +
                   std::to_string(free_bytes / (1024*1024)) + " MB dostepne");
    }
}

int install(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree install <pakiet> [<pakiet2>...]\n";
        return 1;
    }

    /* Sprawdzenie uprawnien root (#8) */
    if (::geteuid() != 0) {
        std::cerr << "deb-ostree install wymaga uprawnien root (sudo).\n";
        return 1;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) {
            log::error("Brak zabootowanego deploymentu OSTree.");
            return 1;
        }

        /* Wymagaj ustawionego obrazu bazowego OCI */
        try { oci::require_origin_refspec(booted->origin_refspec, "install"); }
        catch (const std::exception& e) {
            std::cerr << "\033[1;31mBlad:\033[0m " << e.what() << "\n";
            return 1;
        }

        /* Sprawdz wolne miejsce PRZED pobieraniem (#15):
         * overlay_work_dir musi miec min 2GB na download + checkout + export */
        fs::create_directories(cfg.overlay_work_dir);
        check_disk_space(cfg.overlay_work_dir, 2ULL * 1024 * 1024 * 1024,
                         "overlay_work_dir (pobieranie + checkout)");

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

        std::string base      = cfg.overlay_work_dir + "/base-checkout";
        std::string final_tree = cfg.overlay_work_dir + "/final-tree";

        OverlayManager ovl(cfg.overlay_work_dir + "/session");
        OverlaySession ses = ovl.begin_session(base);
        ovl.bind_mount_virtual_fs(ses);

        /* Etap 2-5: DebLayer::install_packages -- wbudowany progress bar.
         * Przy bledzie (np. nieudany postinst) -- wykonujemy rollback (#17). */
        std::vector<PackageLayer> resolved;
        bool   failed = false;
        std::string errmsg;
        try {
            DebLayer deb(cfg);
            resolved = deb.install_packages(ses, args);
        } catch (const std::exception& e) {
            failed = true; errmsg = e.what();
        }

        ovl.unbind_virtual_fs(ses);

        if (failed) {
            /* Rollback (#17): nie tworzymy nowego commitu -- poprzedni deployment
             * pozostaje aktywny po reboot. Czyścimy session overlay. */
            ovl.discard_session(ses);
            fs::remove_all(base);

            log::error("Instalacja nie powiodla sie -- zachowano poprzedni deployment:\n"
                       + errmsg);
            std::cerr << "\nInstalacja nie powiodla sie. System pozostanie bez zmian po reboot.\n"
                      << "Blad: " << errmsg << "\n";
            return 1;
        }
        ovl.end_session(ses);

        /* Etap 6: tree_export zamiast fs::copy (#1) -- zachowuje hardlinki,
         * urzadzenia, xattry, capabilities */
        {
            progress::ScopedSpinner sp("Eksport drzewa plikow (cp -a)");
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

        /* Etap 7: commit do OSTree */
        std::string new_csum;
        {
            progress::ScopedSpinner sp("Commit do OSTree");
            std::string subject = "deb-ostree install:";
            for (auto& p : args) subject += " " + p;
            new_csum = sysroot.repo().commit_directory(
                final_tree, booted->origin_refspec, subject);
            sp.done(new_csum.substr(0, 12));
        }

        /* Etap 8: deploy */
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
                sp.fail("Deploy: " + res.error_message);
                log::error("Deploy: " + res.error_message);
                return 1;
            }
            sp.done(new_csum.substr(0, 12));
        }

        std::cout << "\nZainstalowano:";
        for (auto& a : args) std::cout << " " << a;
        std::cout << "\nNowy deployment: " << new_csum.substr(0, 12)
                  << "\nWykonaj reboot, aby zmiany weszly w zycie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("install: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
