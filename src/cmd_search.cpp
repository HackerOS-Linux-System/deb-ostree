#include "../cmd/commands.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/deb_fetcher.h"
#include "../cmd/index_cache.h"
#include "../cmd/gpg_verifier.h"
#include "../cmd/progress.h"
#include "../cmd/logging.h"

#include <iostream>
#include <algorithm>
#include <regex>

namespace debostree::cmd {

int search(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Użycie: deb-ostree search <wzorzec> [--exact]\n";
        std::cerr << "Przykład: deb-ostree search vim\n";
        return 1;
    }

    bool exact = false;
    std::string pattern = args[0];
    for (auto& a : args) if (a == "--exact") exact = true;

    if (cfg.apt_sources.empty()) {
        log::error("Brak skonfigurowanych apt_sources -- dodaj source_N w deb-ostree.hk");
        return 1;
    }

    cache::IndexCache idx_cache(cfg.apt_lists_path);
    deb::DebFetcher   fetcher(cfg.overlay_work_dir + "/fetch-tmp");
    gpg::GpgVerifier  verifier;

    std::vector<std::pair<std::string,std::string>> matches; /* name, version */

    progress::ProgressBar bar("Szukanie: " + pattern,
                              static_cast<int>(cfg.apt_sources.size()));

    int src_idx = 0;
    for (auto& source_line : cfg.apt_sources) {
        deb::AptSource source = deb::parse_apt_source_line(source_line);
        ++src_idx;

        for (auto& component : source.components) {
            bar.begin_stage(source.suite + "/" + component);

            std::string packages_content;

            /* Sprawdź cache */
            auto cached = idx_cache.get(source.base_url, source.suite, component);
            if (cached) {
                packages_content = cached->packages_content;
                bar.tick(1, 1, "z cache");
            } else {
                bar.spin("pobieranie indeksu...");
                std::string inrelease = fetcher.fetch_inrelease(source);
                auto verify_result = verifier.verify_inrelease(
                    inrelease, cfg.overlay_work_dir + "/gpg-tmp");
                if (!verify_result.ok) {
                    bar.end_stage("błąd GPG -- pominięto");
                    continue;
                }
                packages_content = fetcher.fetch_packages_index(source, component);
                cache::CacheEntry ce;
                ce.packages_content  = packages_content;
                ce.inrelease_content = inrelease;
                ce.gpg_verified      = verify_result.ok;
                idx_cache.put(source.base_url, source.suite, component, ce);
                bar.tick(1, 1, "pobrano");
            }

            apt::RepoIndex index = apt::RepoIndex::parse(packages_content);

            /* Szukaj */
            for (auto& entry : index.entries()) {
                bool hit = false;
                if (exact) {
                    hit = (entry.package == pattern);
                } else {
                    /* Szukaj w nazwie (case-insensitive) */
                    std::string name_lower = entry.package;
                    std::string pat_lower  = pattern;
                    std::transform(name_lower.begin(), name_lower.end(),
                                   name_lower.begin(), ::tolower);
                    std::transform(pat_lower.begin(),  pat_lower.end(),
                                   pat_lower.begin(),  ::tolower);
                    hit = (name_lower.find(pat_lower) != std::string::npos);
                }

                if (hit) {
                    /* Unikaty -- nie dodajemy jeśli już jest (wiele repo) */
                    bool dup = false;
                    for (auto& [n, v] : matches)
                        if (n == entry.package) { dup = true; break; }
                    if (!dup)
                        matches.push_back({entry.package, entry.version});
                }
            }

            bar.end_stage(std::to_string(index.entries().size()) + " pakietów");
        }
    }

    bar.finish("Wyszukiwanie zakończone");

    if (matches.empty()) {
        std::cout << "Nie znaleziono pakietów pasujących do: " << pattern << "\n";
        return 1;
    }

    /* Sortuj alfabetycznie */
    std::sort(matches.begin(), matches.end());

    std::cout << "\nZnalezione pakiety (" << matches.size() << "):\n";
    for (auto& [name, version] : matches) {
        std::cout << "  " << name << "  " << version << "\n";
    }

    return 0;
}

} // namespace debostree::cmd
