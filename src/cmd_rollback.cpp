#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <iostream>

namespace debostree::cmd {

int rollback(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);

        auto deps = sysroot.list_deployments();
        if (deps.size() < 2) {
            log::error("Brak poprzedniego deploymentu do ktorego mozna wrocic.\n"
                       "Tip: 'deb-ostree status' pokaze dostepne deploymenty.");
            return 1;
        }

        /* Pokaz uzytkownikowi co sie stanie, analogicznie do rpm-ostree. */
        std::cout << "Rollback: powrot z  " << deps[0].checksum.substr(0, 10)
                  << "\n              do   " << deps[1].checksum.substr(0, 10) << "\n";

        auto res = sysroot.rollback();
        if (!res.success) {
            log::error("Rollback nie powiodl sie: " + res.error_message);
            return 1;
        }

        std::cout << "Rollback przygotowany.\n";
        std::cout << "Wykonaj reboot, aby poprzedni deployment wszedl w zycie.\n";
        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("rollback: ") + e.what()); return 1;
    }
}

} // namespace debostree::cmd
