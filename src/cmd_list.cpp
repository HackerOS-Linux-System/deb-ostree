#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/status_db.h"
#include "../cmd/logging.h"

#include <iostream>
#include <iomanip>
#include <algorithm>

namespace debostree::cmd {

/* Pomocnik: wypisuje tabelę z wyrównaniem kolumn */
static void print_table(const std::vector<std::vector<std::string>>& rows,
                         const std::vector<std::string>& headers)
{
    /* Oblicz szerokości kolumn */
    std::vector<size_t> widths(headers.size(), 0);
    for (size_t i = 0; i < headers.size(); ++i)
        widths[i] = headers[i].size();
    for (auto& row : rows)
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i)
            widths[i] = std::max(widths[i], row[i].size());

    /* Linia nagłówka */
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << std::left << std::setw(static_cast<int>(widths[i]) + 2)
                  << headers[i];
    }
    std::cout << "\n";

    /* Separator */
    for (size_t i = 0; i < headers.size(); ++i)
        std::cout << std::string(widths[i] + 2, '-');
    std::cout << "\n";

    /* Dane */
    for (auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < headers.size(); ++i)
            std::cout << std::left << std::setw(static_cast<int>(widths[i]) + 2)
                      << row[i];
        std::cout << "\n";
    }
}

int list(const std::vector<std::string>& args, const Config& cfg) {
    bool show_deployments = false;
    bool show_files       = false;
    std::string filter_pkg;

    for (auto& a : args) {
        if (a == "--deployments" || a == "-d") show_deployments = true;
        if (a == "--files"       || a == "-f") show_files       = true;
        if (!a.empty() && a[0] != '-')         filter_pkg       = a;
    }

    /* ── Tryb: lista deploymentów ── */
    if (show_deployments) {
        try {
            Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
            auto deployments = sysroot.list_deployments();

            if (deployments.empty()) {
                std::cout << "Brak deploymentów OSTree.\n";
                return 0;
            }

            std::cout << "Deploymenty OSTree (" << deployments.size() << "):\n\n";

            for (size_t i = 0; i < deployments.size(); ++i) {
                auto& d = deployments[i];
                std::string status;
                if (d.booted)  status = "[aktywny] ";
                if (d.staged)  status = "[staged]  ";
                if (d.pinned)  status += "[pin] ";

                std::cout << (d.booted ? "* " : "  ")
                          << status
                          << d.checksum.substr(0, 12) << "."
                          << d.serial << "\n";
                std::cout << "    refspec: " << d.origin_refspec << "\n";

                if (!d.layered_packages.empty()) {
                    std::cout << "    pakiety: ";
                    for (size_t j = 0; j < d.layered_packages.size(); ++j) {
                        if (j) std::cout << ", ";
                        std::cout << d.layered_packages[j].name;
                    }
                    std::cout << "\n";
                }
                std::cout << "\n";
            }
        } catch (const std::exception& e) {
            log::error(std::string("list --deployments: ") + e.what());
            return 1;
        }
        return 0;
    }

    /* ── Tryb domyślny: lista pakietów warstwowych ── */
    /* Szukamy status_db z aktywnego deploymentu */
    std::string rootfs_path;
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (booted) {
            rootfs_path = sysroot.deployment_path(*booted);
        }
    } catch (...) {
        /* Brak sysroot -- użyj ścieżki z konfiguracji */
    }

    if (rootfs_path.empty()) rootfs_path = cfg.sysroot_path;

    auto packages = statusdb::load(rootfs_path);

    if (!filter_pkg.empty()) {
        /* Filtruj po nazwie */
        std::vector<statusdb::InstalledPackage> filtered;
        for (auto& p : packages)
            if (p.name.find(filter_pkg) != std::string::npos)
                filtered.push_back(p);
        packages = std::move(filtered);
    }

    if (packages.empty()) {
        if (!filter_pkg.empty())
            std::cout << "Brak pakietów pasujących do: " << filter_pkg << "\n";
        else
            std::cout << "Brak zainstalowanych pakietów warstwowych.\n"
                         "Użyj 'deb-ostree install <pakiet>' aby zainstalować.\n";
        return 0;
    }

    /* Sortuj */
    std::sort(packages.begin(), packages.end(),
              [](auto& a, auto& b) { return a.name < b.name; });

    if (show_files) {
        /* Szczegółowy widok z listą plików */
        for (auto& pkg : packages) {
            std::cout << "Pakiet: " << pkg.name << "  " << pkg.version << "\n";
            std::cout << "Pliki (" << pkg.files.size() << "):\n";
            for (auto& f : pkg.files)
                std::cout << "  " << f << "\n";
            std::cout << "\n";
        }
    } else {
        /* Tabela: Nazwa / Wersja / Liczba plików */
        std::vector<std::vector<std::string>> rows;
        for (auto& pkg : packages) {
            rows.push_back({
                pkg.name,
                pkg.version,
                std::to_string(pkg.files.size()) + " plików"
            });
        }
        std::cout << "Zainstalowane pakiety warstwowe (" << packages.size() << "):\n\n";
        print_table(rows, {"Pakiet", "Wersja", "Rozmiar"});
        std::cout << "\nUżyj 'deb-ostree list --files <pakiet>' aby zobaczyć pliki.\n";
    }

    return 0;
}

} // namespace debostree::cmd
