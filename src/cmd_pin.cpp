#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <iostream>

namespace debostree::cmd {

int pin(const std::vector<std::string>& args, const Config& cfg) {
    bool do_unpin   = false;
    bool do_list    = false;
    std::string target;

    for (auto& a : args) {
        if (a == "--unpin" || a == "-u") { do_unpin = true; }
        else if (a == "--list" || a == "-l") { do_list = true; }
        else if (!a.empty() && a[0] != '-')  { target = a; }
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto deployments = sysroot.list_deployments();

        if (do_list) {
            bool found = false;
            for (auto& d : deployments) {
                if (d.pinned) {
                    found = true;
                    std::cout << (d.booted ? "* " : "  ")
                              << d.checksum.substr(0, 12) << "."
                              << d.serial << "  " << d.origin_refspec << "\n";
                }
            }
            if (!found) std::cout << "Brak przypiętych deploymentów.\n";
            return 0;
        }

        if (target.empty()) {
            std::cerr << "Użycie: deb-ostree pin <prefix_checksum>\n";
            std::cerr << "        deb-ostree pin --unpin <prefix_checksum>\n";
            std::cerr << "        deb-ostree pin --list\n";
            return 1;
        }

        /* Znajdź deployment pasujący do prefiksu */
        Deployment* found = nullptr;
        for (auto& d : deployments) {
            if (d.checksum.rfind(target, 0) == 0 || d.id.find(target) != std::string::npos) {
                found = &d;
                break;
            }
        }

        if (!found) {
            std::cerr << "Nie znaleziono deploymentu: " << target << "\n";
            std::cerr << "Dostępne: użyj 'deb-ostree list --deployments'\n";
            return 1;
        }

        /* Zmień flagę pinned i zapisz deploymenty */
        found->pinned = !do_unpin;
        auto res = sysroot.write_deployments(deployments);

        if (!res.success) {
            log::error("pin: " + res.error_message);
            return 1;
        }

        std::cout << (do_unpin ? "Odpięto: " : "Przypięto: ")
                  << found->checksum.substr(0, 12) << "\n";
        if (!do_unpin)
            std::cout << "Ten deployment nie zostanie usunięty przez 'deb-ostree cleanup'.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("pin: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
