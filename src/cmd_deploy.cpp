#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int deploy(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree deploy <registry/obraz:tag>\n";
        return 1;
    }
    std::string image_ref = args[0];

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);

        log::info("Inicjalny deployment: " + image_ref);
        OciPuller puller(cfg.overlay_work_dir + "/oci-pull");
        std::string rootfs = puller.pull_and_unpack(image_ref);

        std::string refspec = "deb-ostree-oci:" + image_ref;
        std::string csum = sysroot.repo().commit_directory(
            rootfs, refspec, "deb-ostree initial deploy: " + image_ref);

        fs::remove_all(rootfs);

        auto res = sysroot.deploy_commit(csum, cfg.osname, refspec, {});
        if (!res.success) {
            log::error("Deploy nie powiodl sie: " + res.error_message); return 1;
        }

        std::cout << "Deployment " << csum.substr(0, 12) << " zarejestrowany.\n";
        std::cout << "Skonfiguruj bootloader i wykonaj reboot.\n";
        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("deploy: ") + e.what()); return 1;
    }
}

} // namespace debostree::cmd
