#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int rebase(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree rebase <registry/obraz:tag>\n";
        std::cerr << "Przyklad: deb-ostree rebase ghcr.io/mojorg/debian-bootc:trixie\n";
        return 1;
    }

    std::string image_ref = args[0];

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();

        /* Informacja o porzucanych pakietach warstwowych -- tak jak rpm-ostree. */
        if (booted && !booted->layered_packages.empty()) {
            std::cout << "UWAGA: Nastepujace pakiety warstwowe NIE beda automatycznie\n"
                         "przeniesione na nowy obraz bazowy:\n";
            for (auto& p : booted->layered_packages)
                std::cout << "  - " << p.name << "\n";
            std::cout << "Zainstaluj je ponownie po reboot uzywajac 'deb-ostree install'.\n\n";
        }

        log::info("Sciagam nowy obraz bazowy: " + image_ref);
        OciPuller puller(cfg.overlay_work_dir + "/oci-pull");
        std::string rootfs = puller.pull_and_unpack(image_ref);

        std::string new_refspec = "deb-ostree-oci:" + image_ref;
        std::string new_csum = sysroot.repo().commit_directory(
            rootfs, new_refspec, "deb-ostree rebase: " + image_ref);

        fs::remove_all(rootfs);

        /* Nowy deployment bez pakietow warstwowych (czysty rebase). */
        auto res = sysroot.deploy_commit(new_csum, cfg.osname, new_refspec, {});
        if (!res.success) {
            log::error("Deploy nie powiodl sie: " + res.error_message); return 1;
        }

        std::cout << "Rebase na " << image_ref << " przygotowany.\n";
        std::cout << "Wykonaj reboot, aby nowy obraz bazowy wszedl w zycie.\n";
        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("rebase: ") + e.what()); return 1;
    }
}

} // namespace debostree::cmd
