#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <iostream>
#include <iomanip>

namespace debostree::cmd {

namespace {
/* Formatuje checksum do czytelnej, krotszej postaci: pierwsze 10 znakow. */
std::string short_hash(const std::string& s) {
    return s.size() > 10 ? s.substr(0, 10) : s;
}
} // namespace

int status(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto deps = sysroot.list_deployments();

        if (deps.empty()) {
            std::cout
                << "Brak zarejestrowanych deploymentow.\n"
                << "Uzyj 'deb-ostree deploy <obraz:tag>' aby zainicjalizowac system.\n";
            return 0;
        }

        std::cout << "State: idle\n\n";
        std::cout << "Deployments:\n";

        for (size_t i = 0; i < deps.size(); ++i) {
            const auto& d = deps[i];

            /* Bullet analogiczny do rpm-ostree: ● = booted, ↑ = staged */
            std::string bullet = "  ";
            if      (d.booted) bullet = "● ";
            else if (d.staged) bullet = "↑ ";

            std::string origin = d.origin_refspec.empty()
                                 ? "(brak refspec)" : d.origin_refspec;

            std::cout << bullet << origin << "\n";
            std::cout << "  " << std::setw(18) << std::left << "Checksum:"
                      << short_hash(d.checksum)
                      << (d.booted ? " (booted)" : "")
                      << (d.staged ? " (staged -- aktywny po reboot)" : "")
                      << "\n";
            std::cout << "  " << std::setw(18) << "OSName:"   << d.osname   << "\n";
            std::cout << "  " << std::setw(18) << "Serial:"   << d.serial   << "\n";

            if (!d.layered_packages.empty()) {
                std::cout << "  " << std::setw(18) << "LayeredPkgs:";
                for (size_t j = 0; j < d.layered_packages.size(); ++j) {
                    std::cout << d.layered_packages[j].name;
                    if (j + 1 < d.layered_packages.size()) std::cout << ", ";
                }
                std::cout << "\n";
            } else {
                std::cout << "  " << std::setw(18) << "LayeredPkgs:" << "(brak)\n";
            }

            if (i + 1 < deps.size()) std::cout << "\n";
        }

        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("status: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
