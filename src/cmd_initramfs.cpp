#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <iostream>

namespace debostree::cmd {

int initramfs(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty() || args[0] != "--status") {
        std::cout << "Uzycie: deb-ostree initramfs --status\n\n";
        std::cout << "Regeneracja initramfs odbywa sie automatycznie podczas\n"
                     "'install' i 'upgrade', gdy wykryto zmiany w modulach jadra.\n"
                     "(ROADMAP: automatyczne wykrywanie pakietow linux-image-*)\n";
        return args.empty() ? 1 : 0;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) { log::error("Brak zabootowanego deploymentu."); return 1; }

        std::cout << "Aktualny deployment: " << booted->checksum.substr(0, 12) << "\n";
        std::cout << "Initramfs: zarzadzany automatycznie przez deb-ostree.\n";
        std::cout << "(Szczegoly: ROADMAP -- deb-ostree initramfs --status -v)\n";
        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("initramfs: ") + e.what()); return 1;
    }
}

} // namespace debostree::cmd
