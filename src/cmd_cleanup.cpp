#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <iostream>
#include <stdexcept>

namespace debostree::cmd {

int cleanup(const std::vector<std::string>& args, const Config& cfg) {
    int keep = 2; /* domyslnie: aktualny + jeden do rollbacku */

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--keep" || args[i] == "-k") && i + 1 < args.size()) {
            try { keep = std::stoi(args[++i]); }
            catch (...) { std::cerr << "Niepoprawna wartosc --keep\n"; return 1; }
        }
    }

    if (keep < 1) {
        std::cerr << "Wartosc --keep musi byc >= 1 (nie mozna usunac wszystkich deploymentow).\n";
        return 1;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto res = sysroot.cleanup(keep);
        if (!res.success) {
            log::error("Cleanup nie powiodl sie: " + res.error_message); return 1;
        }
        std::cout << "Cleanup zakonczony. Zachowano " << keep
                  << " ostatnie deploymenty.\n";
        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("cleanup: ") + e.what()); return 1;
    }
}

} // namespace debostree::cmd
