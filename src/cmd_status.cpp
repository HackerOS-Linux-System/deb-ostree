#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/status_db.h"
#include "../cmd/logging.h"

#include <iostream>
#include <iomanip>
#include <ctime>

namespace debostree::cmd {

namespace {
std::string short_hash(const std::string& s) {
    return s.size() > 12 ? s.substr(0, 12) : s;
}
} // namespace

int status(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto deps = sysroot.list_deployments();

        if (deps.empty()) {
            std::cout << "Brak zarejestrowanych deploymentow.\n"
                      << "Uzyj 'deb-ostree deploy <obraz:tag>' aby zainicjalizowac system.\n";
            return 0;
        }

        std::cout << "State: idle\n\n";
        std::cout << "Deployments:\n";

        for (size_t i = 0; i < deps.size(); ++i) {
            const auto& d = deps[i];

            std::string bullet = "  ";
            if      (d.booted) bullet = "\u25cf "; /* ● */
            else if (d.staged) bullet = "\u2191 "; /* ↑ */

            std::string origin = d.origin_refspec.empty()
                                 ? "(brak refspec)" : d.origin_refspec;
            std::string flags;
            if (d.pinned) flags += " [pin]";
            if (d.staged) flags += " [staged -- aktywny po reboot]";

            std::cout << bullet << origin << flags << "\n";
            std::cout << "  " << std::setw(18) << std::left << "Checksum:"
                      << short_hash(d.checksum)
                      << (d.booted ? " (booted)" : "") << "\n";
            std::cout << "  " << std::setw(18) << std::left << "OSName:"  << d.osname  << "\n";
            if (d.timestamp > 0) {
                /* Formatuj timestamp jako czytelna data (#6) */
                time_t ts = static_cast<time_t>(d.timestamp);
                std::tm tm_buf{};
                ::gmtime_r(&ts, &tm_buf);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm_buf);
                std::cout << "  " << std::setw(18) << std::left << "Timestamp:" << buf << "\n";
            }
            std::cout << "  " << std::setw(18) << "Serial:"  << d.serial  << "\n";

            /* Pakiety warstwowe z wersjami (#11) */
            if (!d.layered_packages.empty()) {
                std::cout << "  " << std::setw(18) << "LayeredPkgs:" << "\n";

                /* Jesli to aktywny deployment, pobierz wersje z status_db */
                std::string rootfs_path;
                if (d.booted) {
                    try { rootfs_path = sysroot.deployment_path(d); }
                    catch (...) {}
                }

                for (auto& pkg : d.layered_packages) {
                    std::cout << "    " << std::setw(30) << std::left << pkg.name;
                    /* Wersja z PackageLayer */
                    if (!pkg.version.empty()) {
                        std::cout << "  " << pkg.version;
                    } else if (!rootfs_path.empty()) {
                        /* Fallback: pobierz wersje z status_db */
                        if (statusdb::is_installed(rootfs_path, pkg.name)) {
                            auto all = statusdb::load(rootfs_path);
                            for (auto& p : all) {
                                if (p.name == pkg.name) {
                                    std::cout << "  " << p.version;
                                    break;
                                }
                            }
                        }
                    }
                    std::cout << "\n";
                }
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
