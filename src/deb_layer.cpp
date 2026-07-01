#include "../cmd/deb_layer.h"
#include "../cmd/apt_repo_index.h"
#include "../cmd/solv_pool.h"
#include "../cmd/deb_archive.h"
#include "../cmd/status_db.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace debostree {

DebLayer::DebLayer(Config cfg) : cfg_(std::move(cfg)) {}

void DebLayer::refresh_package_index(const OverlaySession& /*session*/) {
    log::debug("refresh_package_index: indeksy zostaną pobrane przy następnym "
              "install/remove (deb-ostree nie cache'uje indeksów na dysku, patrz ROADMAP)");
}

namespace {

/* Buduje SolvPool wypełnioną indeksami ze wszystkich cfg.apt_sources.
 * Wspólna logika używana przez install_packages i remove_packages. */
solv::SolvPool build_pool_from_sources(const Config& cfg, deb::DebFetcher& fetcher,
                                       progress::ProgressBar& bar) {
    solv::SolvPool pool = solv::SolvPool::create();

    if (cfg.apt_sources.empty()) {
        throw std::runtime_error(
            "DebLayer: brak skonfigurowanych apt_sources w konfiguracji -- "
            "dodaj przynajmniej jedną linię 'apt_source' w deb-ostree.hk "
            "(sekcja [apt], np. apt_source => \"deb http://deb.debian.org/debian bookworm main\")");
    }

    int total = 0;
    for (auto& src_line : cfg.apt_sources) {
        deb::AptSource src = deb::parse_apt_source_line(src_line);
        total += static_cast<int>(src.components.size());
    }

    int done = 0;
    int repo_idx = 0;
    for (auto& source_line : cfg.apt_sources) {
        deb::AptSource source = deb::parse_apt_source_line(source_line);

        for (auto& component : source.components) {
            std::string repo_name = source.suite + "-" + component;
            bar.tick(done, total, repo_name);

            std::string index_content = fetcher.fetch_packages_index(source, component);
            apt::RepoIndex index = apt::RepoIndex::parse(index_content);

            pool.add_repo_from_index(index, repo_name + "-" + std::to_string(repo_idx++));
            ++done;
            bar.tick(done, total, repo_name + " (" +
                     std::to_string(index.entries().size()) + " pkgs)");
        }
    }

    return pool;
}

/* Zwraca base_url mirrora używany do pobrania pliku .deb. */
std::string primary_mirror_base_url(const Config& cfg) {
    if (cfg.apt_sources.empty()) return "";
    deb::AptSource source = deb::parse_apt_source_line(cfg.apt_sources.front());
    return source.base_url;
}

/* Formatuje rozmiar pliku do czytelnej postaci (np. "12.3 MB"). */
std::string format_size(uint64_t bytes) {
    if (bytes < 1024)       return std::to_string(bytes) + " B";
    if (bytes < 1024*1024)  return std::to_string(bytes/1024) + " KB";
    std::ostringstream oss;
    oss.precision(1);
    oss << std::fixed << (static_cast<double>(bytes) / (1024*1024)) << " MB";
    return oss.str();
}

} // namespace

std::vector<PackageLayer> DebLayer::install_packages(const OverlaySession& session,
                                                      const std::vector<std::string>& names) {
    if (names.empty()) return {};

    /* Obliczamy liczbę etapów: pobieranie indeksów + rozwiązywanie +
     * pobieranie pakietów + instalacja = 4 etapy */
    int total_stages = 4;

    /* Tytuł: lista pakietów lub skrócona forma */
    std::string title = "Instalacja: ";
    if (names.size() <= 3) {
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) title += ", ";
            title += names[i];
        }
    } else {
        title += names[0] + " i " + std::to_string(names.size() - 1) + " innych";
    }

    progress::ProgressBar bar(title, total_stages);

    /* ── Etap 1: Pobieranie indeksów ── */
    bar.begin_stage("Pobieranie indeksów repozytorium");
    solv::SolvPool pool = [&]() {
        try {
            return build_pool_from_sources(cfg_, fetcher_, bar);
        } catch (...) {
            bar.fail("Błąd pobierania indeksów");
            throw;
        }
    }();
    bar.end_stage();

    /* ── Etap 2: Rozwiązywanie zależności (libsolv SAT) ── */
    bar.begin_stage("Rozwiązywanie zależności");
    bar.spin("libsolv SAT solver...");

    std::vector<solv::ResolvedPackage> resolved;
    try {
        resolved = pool.resolve_install(names);
    } catch (const solv::SolvError& e) {
        bar.fail("Nierozwiązywalne zależności");
        throw std::runtime_error(std::string("Rozwiązywanie zależności nie powiodło się:\n") + e.what());
    }

    /* Oblicz łączny rozmiar do pobrania */
    uint64_t total_size = 0;
    for (auto& p : resolved) total_size += p.size;
    bar.end_stage(std::to_string(resolved.size()) + " pakietów, " + format_size(total_size));

    /* ── Etap 3: Pobieranie pakietów .deb ── */
    bar.begin_stage("Pobieranie pakietów (" + std::to_string(resolved.size()) + ")");
    std::string mirror_base = primary_mirror_base_url(cfg_);
    std::string download_dir = session.upper_dir + "/../deb-download-cache";
    fs::create_directories(download_dir);

    int pkg_idx = 0;
    for (auto& pkg : resolved) {
        bar.tick(pkg_idx, static_cast<int>(resolved.size()),
                 pkg.name + " " + pkg.version + " " + format_size(pkg.size));

        if (pkg.filename.empty()) {
            log::warn("Pakiet " + pkg.name + " nie ma Filename w indeksie -- pominięto");
            ++pkg_idx;
            continue;
        }

        std::string deb_path = download_dir + "/" + fs::path(pkg.filename).filename().string();
        fetcher_.fetch_deb_package(mirror_base, pkg.filename, deb_path, pkg.sha256);
        ++pkg_idx;
    }
    bar.tick(pkg_idx, pkg_idx);
    bar.end_stage(format_size(total_size) + " pobrano");

    /* ── Etap 4: Instalacja (rozpakowywanie + skrypty) ── */
    bar.begin_stage("Instalacja i konfiguracja");
    std::vector<PackageLayer> installed;
    pkg_idx = 0;

    for (auto& pkg : resolved) {
        bar.tick(pkg_idx, static_cast<int>(resolved.size()), pkg.name);

        if (pkg.filename.empty()) { ++pkg_idx; continue; }

        std::string deb_path = download_dir + "/" + fs::path(pkg.filename).filename().string();

        deb::DebArchive archive = deb::DebArchive::open(deb_path);
        archive.extract_data_to(session.merged_dir);

        std::vector<std::string> files = archive.list_data_files();

        std::string postinst = archive.read_maintainer_script("postinst");
        if (!postinst.empty()) {
            run_maintainer_script(session, pkg.name, postinst, "postinst");
        }

        statusdb::InstalledPackage status_entry;
        status_entry.name    = pkg.name;
        status_entry.version = pkg.version;
        status_entry.files   = files;
        statusdb::upsert(session.merged_dir, status_entry);

        PackageLayer pl;
        pl.name    = pkg.name;
        pl.version = pkg.version;
        pl.op      = LayerOp::Install;
        installed.push_back(std::move(pl));

        ++pkg_idx;
    }

    bar.tick(pkg_idx, pkg_idx);
    bar.end_stage(std::to_string(installed.size()) + " pakietów zainstalowano");

    /* Sprzątamy pobrane .deb po instalacji */
    std::error_code ec;
    fs::remove_all(download_dir, ec);

    bar.finish("Instalacja zakończona pomyślnie");
    return installed;
}

void DebLayer::remove_packages(const OverlaySession& session,
                               const std::vector<std::string>& names) {
    if (names.empty()) return;

    std::string title = "Usuwanie: ";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) title += ", ";
        title += names[i];
        if (i >= 2 && names.size() > 3) { title += " ..."; break; }
    }

    progress::ProgressBar bar(title, 3);

    /* ── Etap 1: Indeksy ── */
    bar.begin_stage("Ładowanie indeksów repozytorium");
    solv::SolvPool pool = [&]() {
        try {
            return build_pool_from_sources(cfg_, fetcher_, bar);
        } catch (...) {
            bar.fail("Błąd ładowania indeksów");
            throw;
        }
    }();
    bar.end_stage();

    /* ── Etap 2: Rozwiązywanie (co usunąć) ── */
    bar.begin_stage("Analiza zależności do usunięcia");
    bar.spin("libsolv...");

    std::vector<std::string> to_remove;
    try {
        to_remove = pool.resolve_remove(names);
    } catch (const solv::SolvError& e) {
        bar.fail("Błąd rozwiązywania");
        throw std::runtime_error(std::string("Rozwiązywanie usunięcia nie powiodło się:\n") + e.what());
    }
    bar.end_stage(std::to_string(to_remove.size()) + " pakietów do usunięcia");

    /* ── Etap 3: Usuwanie plików ── */
    bar.begin_stage("Usuwanie plików pakietów");
    int idx = 0;
    for (auto& pkg_name : to_remove) {
        bar.tick(idx, static_cast<int>(to_remove.size()), pkg_name);

        auto installed_list = statusdb::load(session.merged_dir);
        const statusdb::InstalledPackage* found = nullptr;
        for (auto& p : installed_list) {
            if (p.name == pkg_name) { found = &p; break; }
        }

        if (!found) {
            log::warn("Pakiet " + pkg_name + " nie jest w status_db -- pomijam usuwanie plików");
            ++idx;
            continue;
        }

        for (auto& file_path : found->files) {
            std::string full_path = session.merged_dir + file_path;
            std::error_code ec;
            fs::remove(full_path, ec);
            if (ec) {
                log::debug("Nie można usunąć " + full_path + ": " + ec.message());
            }
        }

        statusdb::remove(session.merged_dir, pkg_name);
        ++idx;
    }
    bar.tick(idx, idx);
    bar.end_stage(std::to_string(to_remove.size()) + " pakietów usuniętych");
    bar.finish("Usuwanie zakończone pomyślnie");
}

bool DebLayer::is_installed(const std::string& rootfs_path, const std::string& package_name) {
    return statusdb::is_installed(rootfs_path, package_name);
}

void DebLayer::run_maintainer_script(const OverlaySession& session,
                                     const std::string& package_name,
                                     const std::string& script_content,
                                     const std::string& script_name) {
    std::string tmp_name = "/tmp-deb-ostree-" + package_name + "-" + script_name;
    std::string host_path = session.merged_dir + tmp_name;

    {
        std::ofstream out(host_path, std::ios::trunc);
        out << script_content;
    }
    fs::permissions(host_path, fs::perms::owner_all | fs::perms::group_read |
                              fs::perms::group_exec | fs::perms::others_read |
                              fs::perms::others_exec);

    auto result = process::run({"chroot", session.merged_dir, tmp_name, "configure"});

    fs::remove(host_path);

    if (!result.ok()) {
        log::warn("Skrypt " + script_name + " pakietu " + package_name +
                 " zakończył się kodem " + std::to_string(result.exit_code) +
                 " -- kontynuuję instalację\n" + result.stderr_data);
    }
}

} // namespace debostree
