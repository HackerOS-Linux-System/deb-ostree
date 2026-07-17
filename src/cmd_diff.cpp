#include "../cmd/commands.h"
#include "../cmd/solv_pool.h"
#include "../cmd/pool_builder.h"
#include "../cmd/index_cache.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <algorithm>

namespace debostree::cmd {

int diff(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree diff <pakiet> [pakiet2...]\n"
                  << "Wyswietla pliki ktore zainstaluje pakiet (przed instalacja).\n";
        return 1;
    }

    try {
        /* Wczytaj indeksy (z cache) */
        progress::ProgressBar bar("Analizowanie: " + args[0],
                                  static_cast<int>(cfg.apt_sources.size()));

        bar.begin_stage("Ladowanie indeksow apt");
        solv::SolvPool pool = build_solv_pool(cfg, bar);
        bar.end_stage();

        /* Resolvuj zależności */
        bar.begin_stage("Rozwiazywanie zaleznosci");
        bar.spin("libsolv...");

        std::vector<solv::ResolvedPackage> resolved;
        try {
            resolved = pool.resolve_install(args);
        } catch (const solv::SolvError& e) {
            bar.fail("Blad resolvera");
            log::error(std::string(e.what()));
            return 1;
        }
        bar.end_stage(std::to_string(resolved.size()) + " pakietow");
        bar.finish("Analiza zakonczona");

        /* Wyświetl wyniki -- analogicznie do rpm-ostree ex */
        std::cout << "\nPakiety do zainstalowania (" << resolved.size() << "):\n\n";

        uint64_t total_size = 0;
        for (auto& pkg : resolved) {
            total_size += pkg.size;
            std::cout << "  \033[1;32m+\033[0m " << pkg.name
                      << "  \033[2m" << pkg.version << "\033[0m";
            if (pkg.size > 0) {
                if (pkg.size < 1024*1024)
                    std::cout << "  \033[2m" << (pkg.size/1024) << " KB\033[0m";
                else
                    std::cout << "  \033[2m" << (pkg.size/1024/1024) << " MB\033[0m";
            }
            std::cout << "\n";
        }

        std::cout << "\nSuma: " << resolved.size() << " pakietow";
        if (total_size > 0) {
            std::cout << ", " << (total_size / 1024 / 1024) << " MB do pobrania";
        }
        std::cout << "\n";

        /* Informacja o flagach użytkownika */
        bool has_args = false;
        for (auto& a : args) if (a == "--verbose" || a == "-v") has_args = true;
        if (!has_args) {
            std::cout << "\n\033[2mUzyj 'deb-ostree install " << args[0]
                      << "' aby zainstalowac.\033[0m\n";
        }

        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("diff: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
