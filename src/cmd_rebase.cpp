#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"
#include "../cmd/oci_ref.h"

#include <iostream>
#include <filesystem>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace debostree::cmd {

int rebase(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree rebase <registry/obraz:tag>\n"
                  << "Przyklad: deb-ostree rebase ghcr.io/mojorg/debian-bootc:trixie\n";
        return 1;
    }
    std::string image_ref = args[0];

    /* Waliduj referencje obrazu OCI */
    try { oci::validate_or_throw(image_ref); }
    catch (const std::exception& e) {
        std::cerr << "\033[1;31mBlad:\033[0m " << e.what() << "\n";
        return 1;
    }

    try { OciPuller::check_tools_available(); }
    catch (const std::exception& e) { log::error(e.what()); return 1; }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();

        if (booted && !booted->layered_packages.empty()) {
            std::cout << "UWAGA: Pakiety warstwowe NIE beda przenoszone:\n";
            for (auto& p : booted->layered_packages)
                std::cout << "  - " << p.name << "\n";
            std::cout << "Zainstaluj je ponownie po reboot.\n\n";
        }

        progress::ProgressBar bar("Rebase: " + image_ref, 3);

        bar.begin_stage("Pobieranie nowego obrazu bazowego");
        bar.spin("laczenie z registry...");
        OciPuller puller(cfg.overlay_work_dir + "/oci-pull");
        std::string rootfs;
        try {
            rootfs = puller.pull_and_unpack(image_ref);
        } catch (const std::exception& e) {
            bar.fail("Blad pobierania: " + std::string(e.what()));
            return 1;
        }
        bar.end_stage(image_ref);

        bar.begin_stage("Commit do OSTree");
        bar.spin("obliczanie sumy kontrolnej...");
        std::string new_refspec = "deb-ostree-oci:" + image_ref;
        std::string new_csum;
        try {
            new_csum = sysroot.repo().commit_directory(
                rootfs, new_refspec, "deb-ostree rebase: " + image_ref);
        } catch (const std::exception& e) {
            bar.fail("Blad commit: " + std::string(e.what()));
            fs::remove_all(rootfs);
            return 1;
        }
        fs::remove_all(rootfs);
        bar.end_stage(new_csum.substr(0, 12));

        bar.begin_stage("Rejestracja nowego deploymentu");
        bar.spin("aktualizacja bootloadera...");
        auto res = sysroot.deploy_commit(new_csum, cfg.osname, new_refspec, {});
        if (!res.success) {
            bar.fail("Deploy: " + res.error_message);
            log::error("Deploy: " + res.error_message);
            return 1;
        }
        bar.end_stage(new_csum.substr(0, 12));
        bar.finish("Rebase gotowy -- wykonaj reboot");

        std::cout << "\nRebase na " << image_ref << " przygotowany.\n"
                  << "Wykonaj reboot, aby nowy obraz bazowy wszedl w zycie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("rebase: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
