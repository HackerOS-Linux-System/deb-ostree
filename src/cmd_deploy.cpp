#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int deploy(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Użycie: deb-ostree deploy <registry/obraz:tag>\n";
        return 1;
    }
    std::string image_ref = args[0];

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);

        progress::ProgressBar bar("Inicjalny deployment: " + image_ref, 3);

        /* ── Etap 1: Pull OCI ── */
        bar.begin_stage("Pobieranie obrazu OCI");
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

        /* ── Etap 2: Commit do OSTree ── */
        bar.begin_stage("Commit do repozytorium OSTree");
        bar.spin("obliczanie sumy kontrolnej...");
        std::string refspec = "deb-ostree-oci:" + image_ref;
        std::string csum;
        try {
            csum = sysroot.repo().commit_directory(
                rootfs, refspec, "deb-ostree initial deploy: " + image_ref);
        } catch (const std::exception& e) {
            bar.fail("Błąd commit: " + std::string(e.what()));
            fs::remove_all(rootfs);
            return 1;
        }
        fs::remove_all(rootfs);
        bar.end_stage(csum.substr(0, 12));

        /* ── Etap 3: Deploy ── */
        bar.begin_stage("Rejestracja deploymentu i aktualizacja bootloadera");
        bar.spin("zapisywanie...");
        auto res = sysroot.deploy_commit(csum, cfg.osname, refspec, {});
        if (!res.success) {
            bar.fail("Deploy nie powiódł się: " + res.error_message);
            log::error("Deploy nie powiódł się: " + res.error_message);
            return 1;
        }
        bar.end_stage(csum.substr(0, 12));
        bar.finish("Deployment zarejestrowany");

        std::cout << "\nDeployment " << csum.substr(0, 12) << " zarejestrowany.\n";
        std::cout << "Skonfiguruj bootloader i wykonaj reboot.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("deploy: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
