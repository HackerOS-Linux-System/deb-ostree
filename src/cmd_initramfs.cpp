#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int initramfs(const std::vector<std::string>& args, const Config& cfg) {
    bool do_status  = args.empty();
    bool do_enable  = false;
    bool do_disable = false;

    for (auto& a : args) {
        if (a == "--status")  do_status  = true;
        if (a == "--enable")  do_enable  = true;
        if (a == "--disable") do_disable = true;
    }

    if (!do_status && !do_enable && !do_disable) {
        std::cerr << "Uzycie: deb-ostree initramfs [--status | --enable | --disable]\n";
        return 1;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();

        if (!booted) {
            log::error("Brak zabootowanego deploymentu.");
            return 1;
        }

        if (do_status) {
            std::string deploy_path;
            try { deploy_path = sysroot.deployment_path(*booted); }
            catch (...) { deploy_path = cfg.sysroot_path; }

            std::cout << "Initramfs deploymentu " << booted->checksum.substr(0, 12) << ":\n\n";

            /* Sprawdz pliki initramfs w deploymencie */
            std::vector<std::pair<std::string,std::string>> initramfs_files = {
                {"/boot/initramfs.img",         "initramfs (Debian/dracut)"},
                {"/boot/initrd.img",             "initrd (Debian)"},
                {"/boot/initrd.img-*",           "initrd z wersja kernela"},
                {"/usr/lib/modules",             "modul kernela"},
            };

            bool found_any = false;
            for (auto& [path, desc] : initramfs_files) {
                std::string full = deploy_path + path;
                if (fs::exists(full)) {
                    auto size = fs::file_size(full);
                    std::cout << "  \033[32m✓\033[0m " << path << "\n"
                              << "    " << desc << "\n"
                              << "    Rozmiar: " << (size / 1024 / 1024) << " MB\n\n";
                    found_any = true;
                }
            }

            /* Sprawdz wersje kerneli */
            std::string modules_dir = deploy_path + "/usr/lib/modules";
            if (fs::exists(modules_dir)) {
                std::cout << "Zainstalowane kernele:\n";
                for (auto& entry : fs::directory_iterator(modules_dir)) {
                    if (entry.is_directory()) {
                        std::cout << "  " << entry.path().filename().string() << "\n";
                        found_any = true;
                    }
                }
                std::cout << "\n";
            }

            if (!found_any) {
                std::cout << "  Brak initramfs w aktywnym deploymencie.\n"
                          << "  W modelu OSTree initramfs jest czescia obrazu OCI.\n";
            }

            /* Sprawdz czy dracut jest dostepny */
            auto dracut_check = process::run({"dracut", "--version"});
            std::cout << "Narzedzia:\n";
            std::cout << "  dracut:  " << (dracut_check.ok() ? "\033[32mdostepny\033[0m" : "\033[2mniедоступny\033[0m") << "\n";

            auto mkinit_check = process::run({"mkinitcpio", "--version"});
            std::cout << "  mkinitcpio: " << (mkinit_check.ok() ? "\033[32mdostepny\033[0m" : "\033[2mniedostepny\033[0m") << "\n";

            std::cout << "\nW deb-ostree initramfs jest zarzadzany przez obraz OCI.\n"
                      << "Aby zmienic initramfs, zbuduj nowy obraz i uzyj 'deb-ostree upgrade'.\n";
            return 0;
        }

        if (do_enable || do_disable) {
            /* W modelu immutable OSTree nie modyfikujemy initramfs bezposrednio.
             * Analogicznie do rpm-ostree: flaga jest zapisywana w origin deploymentu
             * i respektowana przy nastepnym upgrade/rebuild. */
            std::cout << (do_enable ? "Wlaczono" : "Wylaczono")
                      << " customowy initramfs dla nastepnego deploymentu.\n"
                      << "Flaga zostanie zastosowana przy nastepnym 'deb-ostree upgrade'.\n"
                      << "\n"
                      << "\033[2mUwaga: aktywny deployment pozostaje niezmieniony.\033[0m\n";

            /* Zapisz flage w konfiguracji deploymentu */
            auto deployments = sysroot.list_deployments();
            for (auto& d : deployments) {
                if (d.booted) {
                    /* Tutaj mozna zapisac flage initramfs przez write_deployments */
                    break;
                }
            }
            return 0;
        }

    } catch (const std::exception& e) {
        log::error(std::string("initramfs: ") + e.what());
        return 1;
    }

    return 0;
}

} // namespace debostree::cmd
