#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <iostream>

namespace debostree::cmd {

int rollback(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto deployments = sysroot.list_deployments();

        if (deployments.size() < 2) {
            std::cerr << "Brak poprzedniego deploymentu do rollback.\n"
                      << "Dostepny tylko jeden deployment:\n";
            if (!deployments.empty())
                std::cerr << "  " << deployments[0].checksum.substr(0, 12) << "\n";
            return 1;
        }

        /* Znajdz aktywny (booted) i poprzedni */
        const Deployment* booted   = nullptr;
        const Deployment* previous = nullptr;

        for (auto& d : deployments) {
            if (d.booted) { booted = &d; break; }
        }

        /* Poprzedni = pierwszy deployment ktory nie jest booted.
         * OSTree przechowuje deploymenty w kolejnosci: [staged/booted], [previous], ... */
        for (auto& d : deployments) {
            if (!d.booted && !d.staged) { previous = &d; break; }
        }

        if (!booted) {
            log::error("Nie mozna znalezc aktywnego deploymentu.");
            return 1;
        }

        if (!previous) {
            std::cerr << "Brak poprzedniego deploymentu. "
                      << "Rollback wymaga co najmniej 2 deploymentow.\n";
            return 1;
        }

        std::cout << "Rollback:\n"
                  << "  Aktywny:    " << booted->checksum.substr(0, 12)
                  << "  " << booted->origin_refspec << "\n"
                  << "  Poprzedni:  " << previous->checksum.substr(0, 12)
                  << "  " << previous->origin_refspec << "\n\n";

        /* OSTree rollback: ustaw poprzedni deployment jako domyslny (index 0).
         * Robimy to przez write_deployments z przestawiona kolejnoscia. */
        std::vector<Deployment> reordered;
        reordered.push_back(*previous); /* poprzedni staje sie pierwszym (domyslnym) */
        for (auto& d : deployments) {
            if (d.checksum != previous->checksum) reordered.push_back(d);
        }

        auto res = sysroot.write_deployments(reordered);
        if (!res.success) {
            log::error("Rollback nie powiodl sie: " + res.error_message);
            return 1;
        }

        if (!previous->layered_packages.empty()) {
            std::cout << "Pakiety warstwowe po rollback:\n";
            for (auto& p : previous->layered_packages)
                std::cout << "  " << p.name << "  " << p.version << "\n";
            std::cout << "\n";
        }

        std::cout << "Rollback przygotowany. Wykonaj reboot aby wejsc w zycie.\n"
                  << "Deployment po reboot: " << previous->checksum.substr(0, 12) << "\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("rollback: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
