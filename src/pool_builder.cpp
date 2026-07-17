#include "../cmd/pool_builder.h"
#include "../cmd/deb_fetcher.h"
#include "../cmd/index_cache.h"
#include "../cmd/gpg_verifier.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/status_db.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace debostree {

solv::SolvPool build_solv_pool(const Config& cfg,
                                progress::ProgressBar& bar,
                                const std::string& rootfs_path)
{
    if (cfg.apt_sources.empty())
        throw std::runtime_error(
            "Brak skonfigurowanych apt_sources -- dodaj source_N w deb-ostree.hk\n"
            "Przyklad: -> source_1 => deb http://deb.debian.org/debian bookworm main");

    solv::SolvPool pool = solv::SolvPool::create(cfg.arch);
    cache::IndexCache idx_cache(cfg.apt_lists_path);
    gpg::GpgVerifier  verifier(cfg.keyring_dir);

    /* Katalog tymczasowy dla gpgv */
    std::string gpg_tmp = cfg.overlay_work_dir + "/gpg-tmp";
    fs::create_directories(gpg_tmp);

    deb::DebFetcher fetcher(cfg.overlay_work_dir + "/fetch-tmp");

    /* Policz komponenty do wyświetlenia w progress barze */
    int total = 0;
    for (auto& sl : cfg.apt_sources) {
        try {
            deb::AptSource src = deb::parse_apt_source_line(sl);
            total += static_cast<int>(src.components.size());
        } catch (...) {}
    }

    int done = 0, repo_idx = 0;
    for (auto& source_line : cfg.apt_sources) {
        deb::AptSource source;
        try {
            source = deb::parse_apt_source_line(source_line);
        } catch (const std::exception& e) {
            log::warn("Nieprawidlowa linia apt_source: " + source_line + " -- " + e.what());
            continue;
        }

        /* Pobierz i zweryfikuj InRelease dla całego suite */
        std::string inrelease;
        std::unordered_map<std::string, std::string> release_checksums;
        bool gpg_ok = false;
        try {
            inrelease = fetcher.fetch_inrelease(source);
            auto vr   = verifier.verify_inrelease(inrelease, gpg_tmp);
            gpg_ok    = vr.ok;
            if (!vr.ok && !vr.error_message.empty())
                log::warn("GPG " + source.suite + ": " + vr.error_message);
            release_checksums = gpg::GpgVerifier::parse_release_checksums(inrelease);
        } catch (const std::exception& e) {
            log::warn("InRelease " + source.suite + ": " + e.what() +
                      " -- kontynuuje bez weryfikacji GPG");
        }

        for (auto& component : source.components) {
            std::string label = source.suite + "/" + component;
            bar.tick(done, total, label);

            std::string packages_content;

            /* Sprawdź cache */
            auto cached = idx_cache.get(source.base_url, source.suite, component);
            if (cached) {
                packages_content = cached->packages_content;
                bar.tick(done, total, label + " (cache)");
            } else {
                bar.spin("pobieranie " + label + "...");
                try {
                    /* Weryfikacja SHA256 skompresowanego indeksu przed dekompresją */
                    packages_content = fetcher.fetch_packages_index_with_release_verify(
                        source, component, release_checksums, cfg.arch);
                } catch (const std::exception& e) {
                    log::warn("Pomijam " + label + ": " + e.what());
                    ++done;
                    continue;
                }

                cache::CacheEntry ce;
                ce.packages_content  = packages_content;
                ce.inrelease_content = inrelease;
                ce.gpg_verified      = gpg_ok;
                idx_cache.put(source.base_url, source.suite, component, ce);
            }

            apt::RepoIndex index = apt::RepoIndex::parse(packages_content);
            pool.add_repo_from_index(index, label + "-" + std::to_string(repo_idx++));
            ++done;
            bar.tick(done, total,
                     label + " (" + std::to_string(index.entries().size()) + " pkg)");
        }
    }

    /* @System: zainstalowane pakiety do detekcji konfliktów (#8) */
    if (!rootfs_path.empty()) {
        auto installed = statusdb::load_all(rootfs_path);
        if (!installed.empty()) {
            pool.add_installed_packages(installed);
            log::debug("SolvPool @System: " + std::to_string(installed.size()) +
                       " pkg z /var/lib/dpkg/status (bazowe + warstwowe)");
        }
    }

    return pool;
}

} // namespace debostree
