#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/oci_puller.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/deb_fetcher.h"
#include "../cmd/solv_pool.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/index_cache.h"
#include "../cmd/gpg_verifier.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"
#include "../cmd/tree_export.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int upgrade(const std::vector<std::string>& /*args*/, const Config& cfg) {
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) { log::error("Brak zabootowanego deploymentu."); return 1; }

        if (booted->origin_refspec.empty()) {
            log::error("Deployment nie ma ustawionego refspec -- uzyj 'rebase'.");
            return 1;
        }

        std::string image_ref = booted->origin_refspec;
        const std::string prefix = "deb-ostree-oci:";
        if (image_ref.rfind(prefix, 0) == 0)
            image_ref = image_ref.substr(prefix.size());

        /* Zbierz nazwy pakietow warstwowych -- do selektywnego upgrade */
        std::vector<std::string> layer_names;
        for (auto& p : booted->layered_packages)
            layer_names.push_back(p.name);

        bool has_layers = !layer_names.empty();

        /* Jesli sa warstwy sprawdz dostepnosc narzedzi OCI z gory (#4) */
        try {
            OciPuller::check_tools_available();
        } catch (const std::exception& e) {
            log::error(std::string(e.what()));
            return 1;
        }

        int stages = has_layers ? 4 : 3;
        progress::ProgressBar bar("Upgrade: " + image_ref, stages);

        /* ── Etap 1: Pull nowego obrazu OCI ── */
        bar.begin_stage("Pobieranie obrazu OCI");
        bar.spin("laczenie z registry...");

        OciPuller puller(cfg.overlay_work_dir + "/oci-pull");
        std::string new_base;
        try {
            new_base = puller.pull_and_unpack(image_ref);
        } catch (const std::exception& e) {
            bar.fail("Blad pobierania: " + std::string(e.what()));
            return 1;
        }
        bar.end_stage(image_ref);

        /* ── Etap 2: Commit bazy do OSTree ── */
        bar.begin_stage("Commit bazy do OSTree");
        bar.spin("obliczanie sumy kontrolnej...");
        std::string base_csum;
        try {
            base_csum = sysroot.repo().commit_directory(
                new_base, booted->origin_refspec,
                "deb-ostree upgrade (base): " + image_ref);
        } catch (const std::exception& e) {
            bar.fail("Blad commit: " + std::string(e.what()));
            fs::remove_all(new_base);
            return 1;
        }
        bar.end_stage(base_csum.substr(0, 12));

        std::string final_csum = base_csum;
        std::vector<PackageLayer> final_pkgs = booted->layered_packages;

        /* ── Etap 3: Selektywny upgrade pakietow warstwowych (#6) ── */
        if (has_layers) {
            bar.begin_stage("Upgrade warstw (" +
                            std::to_string(layer_names.size()) + " pkg)");

            /* Budujemy SolvPool z nowych indeksow (po aktualizacji bazy)
             * i uzywamy resolve_upgrade() -- zwraca TYLKO pakiety z nowsza
             * wersja niz zainstalowana. Nie pobieramy aktualnych pakietow. */
            deb::DebFetcher fetcher(cfg.overlay_work_dir + "/fetch-tmp");
            cache::IndexCache idx_cache(cfg.apt_lists_path);
            gpg::GpgVerifier  verifier;
            solv::SolvPool pool = solv::SolvPool::create(cfg.arch);

            fs::create_directories(cfg.overlay_work_dir + "/gpg-tmp");

            for (auto& source_line : cfg.apt_sources) {
                deb::AptSource source = deb::parse_apt_source_line(source_line);

                std::string inrelease;
                std::unordered_map<std::string, std::string> release_checksums;
                try {
                    inrelease = fetcher.fetch_inrelease(source);
                    auto vr = verifier.verify_inrelease(
                        inrelease, cfg.overlay_work_dir + "/gpg-tmp");
                    if (vr.ok)
                        release_checksums =
                            gpg::GpgVerifier::parse_release_checksums(inrelease);
                } catch (const std::exception& e) {
                    log::warn("GPG " + source.suite + ": " + e.what());
                }

                for (auto& component : source.components) {
                    std::string packages_content;
                    auto cached = idx_cache.get(
                        source.base_url, source.suite, component);
                    if (cached) {
                        packages_content = cached->packages_content;
                    } else {
                        bar.spin("indeks " + source.suite + "/" + component);
                        try {
                            packages_content =
                                fetcher.fetch_packages_index_with_release_verify(
                                    source, component, release_checksums, cfg.arch);
                            cache::CacheEntry ce;
                            ce.packages_content  = packages_content;
                            ce.inrelease_content = inrelease;
                            ce.gpg_verified      = !release_checksums.empty();
                            idx_cache.put(source.base_url, source.suite,
                                          component, ce);
                        } catch (const std::exception& e) {
                            log::warn("Pomijam " + source.suite + "/" +
                                      component + ": " + e.what());
                            continue;
                        }
                    }
                    apt::RepoIndex index = apt::RepoIndex::parse(packages_content);
                    pool.add_repo_from_index(index, source.suite + "-" + component);
                }
            }

            /* Zaladuj zainstalowane pakiety jako @System.
             * add_installed_packages przyjmuje InstalledPackage, nie PackageLayer
             * -- konwertujemy (files nie sa potrzebne solverowi). */
            std::vector<statusdb::InstalledPackage> installed_for_solver;
            installed_for_solver.reserve(booted->layered_packages.size());
            for (auto& pl : booted->layered_packages) {
                statusdb::InstalledPackage ip;
                ip.name    = pl.name;
                ip.version = pl.version;
                installed_for_solver.push_back(std::move(ip));
            }
            pool.add_installed_packages(installed_for_solver);

            /* resolve_upgrade: tylko te z nowsza wersja (#6) */
            std::vector<solv::ResolvedPackage> to_upgrade;
            try {
                to_upgrade = pool.resolve_upgrade(layer_names);
            } catch (const solv::SolvError& e) {
                log::warn("resolve_upgrade: " + std::string(e.what()) +
                          " -- pomijam upgrade warstw");
            }

            if (to_upgrade.empty()) {
                bar.end_stage("warstwy aktualne -- nic do zrobienia");
            } else {
                bar.tick(0, static_cast<int>(to_upgrade.size()),
                         std::to_string(to_upgrade.size()) + " pkg do upgrade");

                /* Instalacja uaktualnien przez overlay na nowej bazie */
                OverlayManager ovl(cfg.overlay_work_dir + "/session");
                OverlaySession ses = ovl.begin_session(new_base);
                ovl.bind_mount_virtual_fs(ses);

                std::vector<std::string> upgrade_names;
                for (auto& p : to_upgrade) upgrade_names.push_back(p.name);

                bool failed = false; std::string errmsg;
                try {
                    DebLayer deb(cfg);
                    /* Instalujemy tylko te ktore mają nowszą wersję */
                    final_pkgs = deb.install_packages(ses, upgrade_names);
                    /* Dodaj pakiety ktore nie wymagaly upgrade */
                    for (auto& orig : booted->layered_packages) {
                        bool upgraded = false;
                        for (auto& up : final_pkgs)
                            if (up.name == orig.name) { upgraded = true; break; }
                        if (!upgraded) final_pkgs.push_back(orig);
                    }
                } catch (const std::exception& e) {
                    failed = true; errmsg = e.what();
                }

                ovl.unbind_virtual_fs(ses);
                if (failed) {
                    ovl.discard_session(ses);
                    fs::remove_all(new_base);
                    bar.fail("Upgrade warstw nie powiodl sie: " + errmsg);
                    log::error(errmsg);
                    return 1;
                }
                ovl.end_session(ses);

                /* Eksport scalonego drzewa */
                std::string final_tree = cfg.overlay_work_dir + "/final-tree";
                fs::remove_all(final_tree);
                tree::export_overlay_merged(ses.merged_dir, final_tree);

                final_csum = sysroot.repo().commit_directory(
                    final_tree, booted->origin_refspec,
                    "deb-ostree upgrade + re-layer: " + image_ref);

                fs::remove_all(final_tree);

                bar.end_stage(std::to_string(to_upgrade.size()) +
                              " pkg zaktualizowano, commit=" +
                              final_csum.substr(0, 12));
            }
        }

        fs::remove_all(new_base);

        /* ── Etap 4: Deploy ── */
        bar.begin_stage("Rejestracja deploymentu");
        bar.spin("aktualizacja bootloadera...");
        auto res = sysroot.deploy_commit(
            final_csum, cfg.osname, booted->origin_refspec, final_pkgs);

        if (!res.success) {
            bar.fail("Deploy nie powiodl sie: " + res.error_message);
            log::error("Deploy: " + res.error_message);
            return 1;
        }
        bar.end_stage(final_csum.substr(0, 12));
        bar.finish("Upgrade gotowy -- wykonaj reboot");

        std::cout << "\nNowy deployment: " << final_csum.substr(0, 12)
                  << "\nWykonaj reboot, aby nowy obraz wszedl w zycie.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("upgrade: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
