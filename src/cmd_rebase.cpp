#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int rebase(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Użycie: deb-ostree rebase <registry/obraz:tag>\n";
        std::cerr << "Przykład: deb-ostree rebase ghcr.io/mojorg/debian-bootc:trixie\n";
        return 1;
    }

    std::string image_ref = args[0];

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();

        if (booted && !booted->layered_packages.empty()) {
            std::cout << "UWAGA: Następujące pakiety warstwowe NIE będą automatycznie\n"
                         "przeniesione na nowy obraz bazowy:\n";
            for (auto& p : booted->layered_packages)
                std::cout << "  - " << p.name << "\n";
            std::cout << "Zainstaluj je ponownie po reboot używając 'deb-ostree install'.\n\n";
        }

        progress::ProgressBar bar("Rebase: " + image_ref, 3);

        /* ── Etap 1: Pull OCI ── */
        bar.begin_stage("Pobieranie nowego obrazu bazowego");
        bar.spin("łączenie z registry...");

        OciPuller puller(cfg.overlay_work_dir + "/oci-pull");
        std::string rootfs;
        try {
            rootfs = puller.pull_and_unpack(image_ref);
        } catch (const std::exception& e) {
            bar.fail("Błąd pobierania: " + std::string(e.what()));
            return 1;
        }
        bar.end_stage(image_ref);

        /* ── Etap 2: Commit ── */
        bar.begin_stage("Commit do OSTree");
        bar.spin("obliczanie sumy kontrolnej...");
        std::string new_refspec = "deb-ostree-oci:" + image_ref;
        std::string new_csum;
        try {
            new_csum = sysroot.repo().commit_directory(
                rootfs, new_refspec, "deb-ostree rebase: " + image_ref);
        } catch (const std::exception& e) {
            bar.fail("Błąd commit: " + std::string(e.what()));
            fs::remove_all(rootfs);
            return 1;
        }
        fs::remove_all(rootfs);
        bar.end_stage(new_csum.substr(0, 12));

        /* ── Etap 3: Deploy ── */
        bar.begin_stage("Rejestracja nowego deploymentu");
        bar.spin("aktualizacja bootloadera...");
        auto res = sysroot.deploy_commit(new_csum, cfg.osname, new_refspec, {});
        if (!res.success) {
            bar.fail("Deploy nie powiódł się: " + res.error_message);
            log::error("Deploy nie powiódł się: " + res.error_message);
            return 1;
        }
        bar.end_stage(new_csum.substr(0, 12));
        bar.finish("Rebase gotowy -- wykonaj reboot");

        std::cout << "\nRebase na " << image_ref << " przygotowany.\n";
        std::cout << "Wykonaj reboot, aby nowy obraz bazowy wszedł w życie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("rebase: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
