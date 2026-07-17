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

int deploy(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree deploy <registry/obraz:tag>\n";
        return 1;
    }
    std::string image_ref = args[0];

    /* Waliduj referencje obrazu OCI -- wymagana org/image:tag */
    try { oci::validate_or_throw(image_ref); }
    catch (const std::exception& e) {
        std::cerr << "\033[1;31mBlad:\033[0m " << e.what() << "\n";
        return 1;
    }

    /* Early-check narzedzi OCI (#4) */
    try { OciPuller::check_tools_available(); }
    catch (const std::exception& e) { log::error(e.what()); return 1; }

    /* Sprawdzenie miejsca (#15) -- obrazy OCI moga byc duze (500MB+) */
    fs::create_directories(cfg.overlay_work_dir);
    {
        struct statvfs st{};
        if (::statvfs(cfg.overlay_work_dir.c_str(), &st) == 0) {
            uint64_t free_bytes = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
            if (free_bytes < 4ULL * 1024 * 1024 * 1024) {
                std::cerr << "OSTRZEZENIE: Malo miejsca na dysku ("
                          << free_bytes / (1024*1024) << " MB). "
                          << "Obraz OCI wymaga zwykle 1-4 GB.\n";
            }
        }
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        progress::ProgressBar bar("Inicjalny deployment: " + image_ref, 3);

        /* Etap 1: Pull OCI */
        bar.begin_stage("Pobieranie obrazu OCI");
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

        /* Etap 2: Commit (OciPuller::pull_and_unpack juz uzywa cp -a wewnatrz
         * -- rootfs jest poprawnie wypelniony z zachowaniem xattrow/uprawnien) */
        bar.begin_stage("Commit do repozytorium OSTree");
        bar.spin("obliczanie sumy kontrolnej...");
        std::string refspec = "deb-ostree-oci:" + image_ref;
        std::string csum;
        try {
            csum = sysroot.repo().commit_directory(
                rootfs, refspec, "deb-ostree initial deploy: " + image_ref);
        } catch (const std::exception& e) {
            bar.fail("Blad commit: " + std::string(e.what()));
            fs::remove_all(rootfs);
            return 1;
        }
        fs::remove_all(rootfs);
        bar.end_stage(csum.substr(0, 12));

        /* Etap 3: Deploy */
        bar.begin_stage("Rejestracja deploymentu i aktualizacja bootloadera");
        bar.spin("zapisywanie...");
        auto res = sysroot.deploy_commit(csum, cfg.osname, refspec, {});
        if (!res.success) {
            bar.fail("Deploy: " + res.error_message);
            log::error("Deploy: " + res.error_message);
            return 1;
        }
        bar.end_stage(csum.substr(0, 12));
        bar.finish("Deployment zarejestrowany");

        std::cout << "\nDeployment " << csum.substr(0, 12) << " zarejestrowany.\n"
                  << "Skonfiguruj bootloader i wykonaj reboot.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("deploy: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
