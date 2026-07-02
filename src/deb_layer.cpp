#include "../cmd/deb_layer.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/solv_pool.h"
#include "../cmd/deb_archive.h"
#include "../cmd/status_db.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"
#include "../cmd/index_cache.h"
#include "../cmd/gpg_verifier.h"
#include "../cmd/dpkg_status.h"
#include "../cmd/maintainer_scripts.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace debostree {

DebLayer::DebLayer(Config cfg) : cfg_(std::move(cfg)) {}

void DebLayer::refresh_package_index(const OverlaySession& /*session*/) {}

namespace {

std::string format_size(uint64_t bytes) {
    if (bytes < 1024)       return std::to_string(bytes) + " B";
    if (bytes < 1024*1024)  return std::to_string(bytes / 1024) + " KB";
    std::ostringstream oss;
    oss.precision(1);
    oss << std::fixed << (static_cast<double>(bytes) / (1024*1024)) << " MB";
    return oss.str();
}

std::string primary_mirror_base_url(const Config& cfg) {
    if (cfg.apt_sources.empty()) return "";
    deb::AptSource src = deb::parse_apt_source_line(cfg.apt_sources.front());
    return src.base_url;
}

/* Buduje SolvPool z cache + GPG + @System.
 * Poprawka SHA256 (#3): weryfikujemy teraz skompresowaną formę (.xz/.gz)
 * przed dekompresją -- deb_fetcher pobiera i weryfikuje osobno. */
solv::SolvPool build_pool(const Config& cfg,
                          deb::DebFetcher& fetcher,
                          progress::ProgressBar& bar,
                          const std::string& rootfs_path = "")
{
    if (cfg.apt_sources.empty())
        throw std::runtime_error(
            "DebLayer: brak apt_sources -- dodaj source_N w deb-ostree.hk");

    solv::SolvPool pool = solv::SolvPool::create(cfg.arch);
    cache::IndexCache idx_cache(cfg.apt_lists_path);
    gpg::GpgVerifier  verifier;

    int total = 0;
    for (auto& sl : cfg.apt_sources) {
        deb::AptSource src = deb::parse_apt_source_line(sl);
        total += static_cast<int>(src.components.size());
    }

    int done = 0, repo_idx = 0;
    for (auto& source_line : cfg.apt_sources) {
        deb::AptSource source = deb::parse_apt_source_line(source_line);

        /* InRelease + GPG */
        std::string inrelease;
        std::unordered_map<std::string, std::string> release_checksums;
        bool gpg_ok = false;
        try {
            inrelease = fetcher.fetch_inrelease(source);
            auto vr = verifier.verify_inrelease(
                inrelease, cfg.overlay_work_dir + "/gpg-tmp");
            gpg_ok = vr.ok;
            if (!gpg_ok)
                throw std::runtime_error("GPG: " + vr.error_message);
            release_checksums = gpg::GpgVerifier::parse_release_checksums(inrelease);
        } catch (const std::exception& e) {
            log::warn("GPG/InRelease " + source.suite + ": " + e.what());
        }

        for (auto& component : source.components) {
            std::string label = source.suite + "/" + component;
            bar.tick(done, total, label);

            std::string packages_content;
            auto cached = idx_cache.get(source.base_url, source.suite, component);
            if (cached) {
                packages_content = cached->packages_content;
                bar.tick(done, total, label + " (cache)");
            } else {
                bar.spin("pobieranie " + label + "...");
                try {
                    /* fetch_packages_index_with_release_verify (#3):
                     * pobiera .xz, weryfikuje SHA256 skompresowanej formy
                     * względem sumy z InRelease, potem dekompresuje. */
                    packages_content = fetcher.fetch_packages_index_with_release_verify(
                        source, component, release_checksums, cfg.arch);
                } catch (const std::exception& e) {
                    log::warn("Pomijam " + label + ": " + e.what());
                    ++done; continue;
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

    /* @System -- zainstalowane pakiety do detekcji konfliktów */
    if (!rootfs_path.empty()) {
        auto installed = statusdb::load(rootfs_path);
        if (!installed.empty()) {
            pool.add_installed_packages(installed);
            log::debug("SolvPool @System: " + std::to_string(installed.size()) + " pkg");
        }
    }

    return pool;
}

} // namespace

/* ── install_packages ── */

std::vector<PackageLayer> DebLayer::install_packages(
    const OverlaySession& session,
    const std::vector<std::string>& names)
{
    if (names.empty()) return {};

    std::string title = "Instalacja: ";
    if (names.size() <= 3)
        for (size_t i = 0; i < names.size(); ++i) { if (i) title += ", "; title += names[i]; }
    else
        title += names[0] + " i " + std::to_string(names.size() - 1) + " innych";

    progress::ProgressBar bar(title, 5);

    /* ── Etap 1: Indeksy ── */
    bar.begin_stage("Pobieranie indeksów (GPG + cache)");
    fs::create_directories(cfg_.overlay_work_dir + "/gpg-tmp");
    deb::DebFetcher fetcher(cfg_.overlay_work_dir + "/fetch-tmp");
    solv::SolvPool pool = [&]() {
        try { return build_pool(cfg_, fetcher, bar, session.merged_dir); }
        catch (...) { bar.fail("Blad indeksow"); throw; }
    }();
    bar.end_stage();

    /* ── Etap 2: Resolver ── */
    bar.begin_stage("Rozwiazywanie zaleznosci (libsolv SAT)");
    bar.spin("obliczanie...");
    std::vector<solv::ResolvedPackage> resolved;
    try {
        resolved = pool.resolve_install(names);
    } catch (const solv::SolvError& e) {
        bar.fail("Nierozwiazywalne zaleznosci");
        throw std::runtime_error(
            std::string("Rozwiazywanie zaleznosci nie powiodlo sie:\n") + e.what());
    }
    uint64_t total_size = 0;
    for (auto& p : resolved) total_size += p.size;
    bar.end_stage(std::to_string(resolved.size()) + " pkg, " + format_size(total_size));

    /* ── Etap 3: Pobieranie ── */
    bar.begin_stage("Pobieranie pakietow (" + std::to_string(resolved.size()) + ")");
    std::string mirror_base  = primary_mirror_base_url(cfg_);
    std::string download_dir = cfg_.overlay_work_dir + "/deb-download";
    fs::create_directories(download_dir);

    int pkg_idx = 0;
    for (auto& pkg : resolved) {
        bar.tick(pkg_idx, static_cast<int>(resolved.size()),
                 pkg.name + " " + pkg.version + " " + format_size(pkg.size));
        if (!pkg.filename.empty()) {
            std::string dest = download_dir + "/" +
                               fs::path(pkg.filename).filename().string();
            fetcher.fetch_deb_package(mirror_base, pkg.filename, dest, pkg.sha256);
        }
        ++pkg_idx;
    }
    bar.end_stage(format_size(total_size) + " pobrano");

    /* ── Etap 4: preinst + rozpakowywanie ── */
    bar.begin_stage("Rozpakowywanie (preinst -> data.tar)");
    std::vector<deb::ControlInfo> all_ctrl;
    pkg_idx = 0;

    for (auto& pkg : resolved) {
        bar.tick(pkg_idx, static_cast<int>(resolved.size()), pkg.name);
        if (pkg.filename.empty()) { ++pkg_idx; continue; }

        std::string deb_path = download_dir + "/" +
                               fs::path(pkg.filename).filename().string();
        deb::DebArchive archive = deb::DebArchive::open(deb_path);

        /* Zapisz skrypty maintainer do info/ (#1) */
        for (auto& stype : {"preinst", "postinst", "prerm", "postrm"}) {
            std::string sc = archive.read_maintainer_script(stype);
            if (!sc.empty())
                maintscripts::save_script(session.merged_dir, pkg.name, stype, sc);
        }

        /* preinst przed rozpakowaniem */
        std::string preinst = archive.read_maintainer_script("preinst");
        if (!preinst.empty())
            run_maintainer_script(session, pkg.name, preinst, "preinst");

        archive.extract_data_to(session.merged_dir);

        try { all_ctrl.push_back(archive.read_control()); } catch (...) {}
        ++pkg_idx;
    }
    bar.end_stage();

    /* ── Etap 5: postinst + status_db + dpkg compat ── */
    bar.begin_stage("Konfiguracja (postinst + status_db + dpkg/status)");
    std::vector<PackageLayer> installed;
    pkg_idx = 0;

    for (auto& pkg : resolved) {
        bar.tick(pkg_idx, static_cast<int>(resolved.size()), pkg.name);
        if (pkg.filename.empty()) { ++pkg_idx; continue; }

        std::string deb_path = download_dir + "/" +
                               fs::path(pkg.filename).filename().string();
        deb::DebArchive archive = deb::DebArchive::open(deb_path);

        std::vector<std::string> files = archive.list_data_files();

        /* postinst */
        std::string postinst = archive.read_maintainer_script("postinst");
        if (!postinst.empty())
            run_maintainer_script(session, pkg.name, postinst, "postinst");

        /* status_db */
        statusdb::InstalledPackage entry;
        entry.name    = pkg.name;
        entry.version = pkg.version;
        entry.files   = files;
        statusdb::upsert(session.merged_dir, entry);

        PackageLayer pl;
        pl.name    = pkg.name;
        pl.version = pkg.version;
        pl.op      = LayerOp::Install;
        installed.push_back(pl);
        ++pkg_idx;
    }

    /* Sync dpkg/status (#5) */
    auto all_installed = statusdb::load(session.merged_dir);
    try {
        dpkg_compat::sync_dpkg_status(session.merged_dir, all_installed, all_ctrl);
    } catch (const std::exception& e) {
        log::warn("dpkg_compat: " + std::string(e.what()));
    }

    bar.end_stage(std::to_string(installed.size()) + " zainstalowano");

    std::error_code ec;
    fs::remove_all(download_dir, ec);
    bar.finish("Instalacja zakonczona pomyslnie");
    return installed;
}

/* ── remove_packages ── */

void DebLayer::remove_packages(const OverlaySession& session,
                               const std::vector<std::string>& names)
{
    if (names.empty()) return;

    std::string title = "Usuwanie: ";
    for (size_t i = 0; i < names.size() && i < 4; ++i) {
        if (i) title += ", ";
        title += names[i];
    }
    if (names.size() > 4) title += " ...";

    progress::ProgressBar bar(title, 3);

    /* ── Etap 1: Indeksy ── */
    bar.begin_stage("Ladowanie indeksow");
    deb::DebFetcher fetcher(cfg_.overlay_work_dir + "/fetch-tmp");
    solv::SolvPool pool = [&]() {
        try { return build_pool(cfg_, fetcher, bar, session.merged_dir); }
        catch (...) { bar.fail("Blad indeksow"); throw; }
    }();
    bar.end_stage();

    /* ── Etap 2: Resolver ── */
    bar.begin_stage("Analiza zaleznosci do usuniecia");
    bar.spin("libsolv...");
    std::vector<std::string> to_remove;
    try {
        to_remove = pool.resolve_remove(names);
    } catch (const solv::SolvError& e) {
        bar.fail("Blad resolvera");
        throw std::runtime_error(
            std::string("Rozwiazywanie usuniecia nie powiodlo sie:\n") + e.what());
    }
    bar.end_stage(std::to_string(to_remove.size()) + " pkg do usuniecia");

    /* ── Etap 3: prerm + usuwanie plikow + postrm (#1) ── */
    bar.begin_stage("Usuwanie (" + std::to_string(to_remove.size()) + " pkg)");
    auto installed_list = statusdb::load(session.merged_dir);
    int idx = 0;

    for (auto& pkg_name : to_remove) {
        bar.tick(idx, static_cast<int>(to_remove.size()), pkg_name);

        const statusdb::InstalledPackage* found = nullptr;
        for (auto& p : installed_list)
            if (p.name == pkg_name) { found = &p; break; }

        if (!found) {
            log::warn("Pakiet " + pkg_name + " nie ma wpisu w status_db -- pomijam");
            ++idx;
            continue;
        }

        /* prerm -- ze skryptów zapisanych przy instalacji (#1) */
        std::string prerm = maintscripts::load_script(
            session.merged_dir, pkg_name, "prerm");
        if (!prerm.empty()) {
            run_maintainer_script(session, pkg_name, prerm, "prerm");
        } else {
            log::debug("prerm " + pkg_name + ": brak skryptu -- pomijam");
        }

        /* Usuwanie plikow */
        for (auto& file_path : found->files) {
            std::string full = session.merged_dir + file_path;
            std::error_code ec;
            fs::remove(full, ec);
            if (ec) log::debug("rm " + full + ": " + ec.message());
        }

        /* postrm -- ze skryptow zapisanych przy instalacji (#1) */
        std::string postrm = maintscripts::load_script(
            session.merged_dir, pkg_name, "postrm");
        if (!postrm.empty()) {
            run_maintainer_script(session, pkg_name, postrm, "postrm");
        } else {
            log::debug("postrm " + pkg_name + ": brak skryptu -- pomijam");
        }

        /* Usun skrypty z info/ */
        maintscripts::remove_scripts(session.merged_dir, pkg_name);

        /* Aktualizuj status_db */
        statusdb::remove(session.merged_dir, pkg_name);

        /* Aktualizuj /var/lib/dpkg/status (#5) */
        dpkg_compat::remove_from_dpkg_status(session.merged_dir, pkg_name);

        ++idx;
    }

    bar.tick(idx, idx);
    bar.end_stage(std::to_string(to_remove.size()) + " pkg usunieto");
    bar.finish("Usuwanie zakonczone pomyslnie");
}

bool DebLayer::is_installed(const std::string& rootfs_path,
                            const std::string& package_name) {
    return statusdb::is_installed(rootfs_path, package_name);
}

void DebLayer::run_maintainer_script(const OverlaySession& session,
                                     const std::string& package_name,
                                     const std::string& script_content,
                                     const std::string& script_name)
{
    std::string tmp_name = "/tmp-deb-ostree-" + package_name + "-" + script_name;
    std::string host_path = session.merged_dir + tmp_name;

    {
        std::ofstream out(host_path, std::ios::trunc);
        out << "#!/bin/sh\nset -e\n" << script_content;
    }
    fs::permissions(host_path,
        fs::perms::owner_all  | fs::perms::group_read  |
        fs::perms::group_exec | fs::perms::others_read |
        fs::perms::others_exec);

    auto result = process::run({
        "env", "-i",
        "HOME=/root", "PATH=/usr/bin:/bin:/usr/sbin:/sbin",
        "DEBIAN_FRONTEND=noninteractive",
        "DEBCONF_NONINTERACTIVE_SEEN=true",
        "chroot", session.merged_dir, tmp_name, "configure"
    });

    fs::remove(host_path);

    if (!result.ok()) {
        log::warn("Skrypt " + script_name + " " + package_name +
                  " exit=" + std::to_string(result.exit_code) +
                  " -- kontynuuje\n" + result.stderr_data);
    }
}

} // namespace debostree
