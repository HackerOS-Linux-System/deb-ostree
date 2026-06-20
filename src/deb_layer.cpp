#include "../cmd/deb_layer.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace debostree {

DebLayer::DebLayer(Config cfg) : cfg_(std::move(cfg)) {}

std::vector<std::string> DebLayer::apt_env_prefix() const {
    /* "env VAR=val ..." jako prefiks komendy -- zmienne dotycza tylko
     * wywolania apt-get, nie calego procesu deb-ostree.                    */
    return {
        "env",
        "DEBIAN_FRONTEND=noninteractive",
        "APT_LISTCHANGES_FRONTEND=none",
        "INITRD=No",          /* wylacza update-initramfs w hooku dpkg     */
    };
}

void DebLayer::refresh_package_index(const OverlaySession& s) {
    auto argv = apt_env_prefix();
    argv.insert(argv.end(), {
        "apt-get", "update", "-y",
        "--root=" + s.merged_dir,
    });
    auto r = process::run(argv);
    if (!r.ok())
        throw std::runtime_error("apt-get update nie powiodlo sie:\n" + r.stderr_data);
    log::info("Indeks pakietow odswiezony.");
}

std::vector<PackageLayer> DebLayer::install_packages(
    const OverlaySession& s,
    const std::vector<std::string>& names)
{
    if (names.empty()) return {};

    /* Bazowy argv dla apt-get install (bez nazw pakietow). */
    std::vector<std::string> base = apt_env_prefix();
    base.insert(base.end(), {
        "apt-get", "install", "-y",
        "--no-install-recommends",
        "--root=" + s.merged_dir,
    });

    /* --- symulacja: lista pakietow ktore faktycznie zostana zainstalowane --- */
    std::vector<std::string> sim_argv = base;
    sim_argv.push_back("--simulate");
    for (auto& n : names) sim_argv.push_back(n);

    auto sim = process::run(sim_argv);
    /* Nie rzucamy przy bledzie symulacji -- moze to byc np. brak cache apt.
     * Fallback: traktujemy podane nazwy jako wynik resolwowania.            */
    std::vector<PackageLayer> resolved;
    if (sim.ok()) {
        std::istringstream iss(sim.stdout_data);
        std::string line;
        while (std::getline(iss, line)) {
            /* Format: "Inst <pkg> (<wersja> ...)" */
            if (line.rfind("Inst ", 0) == 0) {
                std::istringstream ls(line.substr(5));
                std::string pkg, ver;
                ls >> pkg >> ver;
                /* ver to np. "(1.2.3-4" -- usuwamy nawias otwierajacy */
                if (!ver.empty() && ver.front() == '(')
                    ver = ver.substr(1);
                PackageLayer pl;
                pl.name    = pkg;
                pl.version = ver;
                pl.op      = LayerOp::Install;
                resolved.push_back(pl);
            }
        }
    }

    /* --- faktyczna instalacja --- */
    std::vector<std::string> real_argv = base;
    for (auto& n : names) real_argv.push_back(n);

    auto r = process::run(real_argv);
    if (!r.ok()) {
        std::string joined;
        for (auto& n : names) joined += n + " ";
        throw std::runtime_error("apt-get install nie powiodlo sie dla: " + joined +
                                 "\n" + r.stderr_data);
    }

    /* Fallback gdy symulacja nie dala wynikow. */
    if (resolved.empty()) {
        for (auto& n : names) {
            PackageLayer pl;
            pl.name = n;
            pl.op   = LayerOp::Install;
            resolved.push_back(pl);
        }
    }

    log::info("Zainstalowano " + std::to_string(resolved.size()) + " pakiet(ow).");
    return resolved;
}

void DebLayer::remove_packages(const OverlaySession& s,
                               const std::vector<std::string>& names)
{
    if (names.empty()) return;

    auto argv = apt_env_prefix();
    argv.insert(argv.end(), {
        "apt-get", "remove", "-y",
        "--root=" + s.merged_dir,
    });
    for (auto& n : names) argv.push_back(n);

    auto r = process::run(argv);
    if (!r.ok())
        throw std::runtime_error("apt-get remove nie powiodlo sie:\n" + r.stderr_data);

    log::info("Usunieto " + std::to_string(names.size()) + " pakiet(ow).");
}

bool DebLayer::is_installed(const std::string& rootfs, const std::string& pkg) {
    auto r = process::run({"dpkg", "--root=" + rootfs, "-s", pkg});
    return r.ok();
}

} // namespace debostree
