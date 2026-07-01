#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int upgrade(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) { log::error("Brak zabootowanego deploymentu."); return 1; }

        if (booted->origin_refspec.empty()) {
            log::error("Deployment nie ma ustawionego refspec -- użyj 'rebase' "
                       "żeby wskazać źródło obrazu OCI.");
            return 1;
        }

        std::string image_ref = booted->origin_refspec;
        const std::string prefix = "deb-ostree-oci:";
        if (image_ref.rfind(prefix, 0) == 0)
            image_ref = image_ref.substr(prefix.size());

        /* Liczymy etapy: pull OCI + commit + (opcjonalnie) re-layering + deploy */
        bool has_layers = !booted->layered_packages.empty();
        int stages = has_layers ? 4 : 3;

        progress::ProgressBar bar("Upgrade: " + image_ref, stages);

        /* ── Etap 1: Pobieranie nowego obrazu OCI ── */
        bar.begin_stage("Pobieranie obrazu OCI");
        bar.spin("łączenie z registry...");

        OciPuller puller(cfg.overlay_work_dir + "/oci-pull");
        std::string new_base;
        try {
            new_base = puller.pull_and_unpack(image_ref);
        } catch (const std::exception& e) {
            bar.fail("Błąd pobierania: " + std::string(e.what()));
            return 1;
        }
        bar.end_stage(image_ref);

        /* ── Etap 2: Commit nowego rootfs do OSTree ── */
        bar.begin_stage("Commit bazy do OSTree");
        bar.spin("obliczanie sumy kontrolnej drzewa...");
        std::string base_csum;
        try {
            base_csum = sysroot.repo().commit_directory(
                new_base, booted->origin_refspec,
                "deb-ostree upgrade (base): " + image_ref);
        } catch (const std::exception& e) {
            bar.fail("Błąd commit: " + std::string(e.what()));
            fs::remove_all(new_base);
            return 1;
        }
        bar.end_stage(base_csum.substr(0, 12));

        std::string final_csum = base_csum;
        std::vector<PackageLayer> final_pkgs = booted->layered_packages;

        /* ── Etap 3: Re-layering pakietów warstwowych (jeśli są) ── */
        if (has_layers) {
            bar.begin_stage("Re-layering " +
                            std::to_string(booted->layered_packages.size()) +
                            " pakietów warstwowych");

            OverlayManager ovl(cfg.overlay_work_dir + "/session");
            OverlaySession ses = ovl.begin_session(new_base);
            ovl.bind_mount_virtual_fs(ses);

            std::vector<std::string> names;
            for (auto& p : booted->layered_packages) names.push_back(p.name);

            bool failed = false; std::string errmsg;
            try {
                DebLayer deb(cfg);
                deb.refresh_package_index(ses);
                final_pkgs = deb.install_packages(ses, names);
            } catch (const std::exception& e) { failed = true; errmsg = e.what(); }

            ovl.unbind_virtual_fs(ses);
            if (failed) {
                ovl.discard_session(ses);
                fs::remove_all(new_base);
                bar.fail("Re-layering nie powiódł się: " + errmsg);
                log::error("Re-layering nie powiódł się: " + errmsg);
                return 1;
            }
            ovl.end_session(ses);

            std::string final_tree = cfg.overlay_work_dir + "/final-tree";
            fs::remove_all(final_tree);
            fs::create_directories(final_tree);

            OverlaySession ro = ovl.begin_session(new_base);
            std::error_code ec;
            fs::copy(ro.merged_dir, final_tree,
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            ovl.end_session(ro);

            if (ec) {
                bar.fail("Kopiowanie drzewa: " + ec.message());
                return 1;
            }

            final_csum = sysroot.repo().commit_directory(
                final_tree, booted->origin_refspec,
                "deb-ostree upgrade + re-layer: " + image_ref);

            fs::remove_all(final_tree);
            bar.end_stage(final_csum.substr(0, 12));
        }

        fs::remove_all(new_base);

        /* ── Etap końcowy: Deploy ── */
        bar.begin_stage("Rejestracja nowego deploymentu");
        bar.spin("aktualizacja bootloadera...");
        auto res = sysroot.deploy_commit(
            final_csum, cfg.osname, booted->origin_refspec, final_pkgs);

        if (!res.success) {
            bar.fail("Deploy nie powiódł się: " + res.error_message);
            log::error("Deploy nie powiódł się: " + res.error_message);
            return 1;
        }
        bar.end_stage(final_csum.substr(0, 12));
        bar.finish("Upgrade gotowy -- wykonaj reboot");

        std::cout << "\nNowy deployment: " << final_csum.substr(0, 12) << "\n";
        std::cout << "Wykonaj reboot, aby nowy obraz wszedł w życie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("upgrade: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
