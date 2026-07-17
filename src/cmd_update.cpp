#include "../cmd/commands.h"
#include "../cmd/deb_fetcher.h"
#include "../cmd/index_cache.h"
#include "../cmd/gpg_verifier.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int update(const std::vector<std::string>& /*args*/, const Config& cfg) {
    if (cfg.apt_sources.empty()) {
        std::cerr << "Brak skonfigurowanych apt_sources w deb-ostree.hk.\n"
                  << "Dodaj: -> source_1 => deb http://deb.debian.org/debian bookworm main\n";
        return 1;
    }

    /* Wyczysc stary cache przed odswiezeniem */
    cache::IndexCache idx_cache(cfg.apt_lists_path);
    idx_cache.clear();
    fs::create_directories(cfg.overlay_work_dir + "/gpg-tmp");

    deb::DebFetcher  fetcher(cfg.overlay_work_dir + "/fetch-tmp");
    gpg::GpgVerifier verifier(cfg.keyring_dir);

    int total = 0;
    for (auto& sl : cfg.apt_sources) {
        deb::AptSource src = deb::parse_apt_source_line(sl);
        total += static_cast<int>(src.components.size());
    }

    progress::ProgressBar bar("Aktualizacja indeksow apt", total);
    int done = 0, errors = 0;

    for (auto& source_line : cfg.apt_sources) {
        deb::AptSource source = deb::parse_apt_source_line(source_line);

        /* Pobierz i zweryfikuj InRelease */
        std::string inrelease;
        std::unordered_map<std::string, std::string> release_checksums;
        bool gpg_ok = false;

        bar.begin_stage(source.suite);
        bar.spin("pobieranie InRelease...");
        try {
            inrelease = fetcher.fetch_inrelease(source);
            auto vr = verifier.verify_inrelease(
                inrelease, cfg.overlay_work_dir + "/gpg-tmp");
            gpg_ok = vr.ok;
            if (!vr.ok && !vr.error_message.empty())
                log::warn("GPG " + source.suite + ": " + vr.error_message);
            release_checksums = gpg::GpgVerifier::parse_release_checksums(inrelease);
        } catch (const std::exception& e) {
            log::warn("InRelease " + source.suite + ": " + e.what());
        }

        int comp_idx = 0;
        for (auto& component : source.components) {
            std::string label = source.suite + "/" + component;
            bar.tick(done, total, label);

            try {
                std::string packages_content =
                    fetcher.fetch_packages_index_with_release_verify(
                        source, component, release_checksums, cfg.arch);

                apt::RepoIndex index = apt::RepoIndex::parse(packages_content);

                cache::CacheEntry ce;
                ce.packages_content  = std::move(packages_content);
                ce.inrelease_content = inrelease;
                ce.gpg_verified      = gpg_ok;
                idx_cache.put(source.base_url, source.suite, component, ce);

                bar.tick(done + 1, total,
                         label + " (" + std::to_string(index.entries().size()) + " pkg)");
                ++comp_idx;
            } catch (const std::exception& e) {
                log::warn("Blad pobierania " + label + ": " + e.what());
                ++errors;
            }
            ++done;
        }
        bar.end_stage(std::to_string(comp_idx) + "/" +
                      std::to_string(source.components.size()) + " komponentow");
    }

    if (errors > 0) {
        bar.fail(std::to_string(errors) + " bledow pobierania");
        std::cerr << errors << " komponentow nie zostalo pobranych.\n";
        return 1;
    }

    bar.finish("Indeksy aktualne");
    std::cout << "Pobrano indeksy dla " << done << " komponentow.\n";
    return 0;
}

} // namespace debostree::cmd
