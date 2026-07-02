#include "../cmd/commands.h"
#include "../cmd/state_store.h"
#include "../cmd/logging.h"
#include "../cmd/transaction_lock.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

static const char* VERSION = "0.1.0";

void print_usage() {
    std::cout <<
"deb-ostree " << VERSION << " -- zarządzanie systemem Debian w modelu immutable/image-based\n"
"             (odpowiednik rpm-ostree dla ekosystemu .deb/apt + OCI/bootc)\n"
"\n"
"Użycie: deb-ostree [opcje] <komenda> [argumenty]\n"
"\n"
"Komendy:\n"
"  status                      Wyświetl aktywne i poprzednie deploymenty\n"
"  install   <pkg...>          Nałóż pakiet(y) .deb jako warstwa na obraz bazowy\n"
"  uninstall <pkg...>          Usuń pakiet(y) warstwowe (alias: remove)\n"
"  remove    <pkg...>          Alias dla uninstall\n"
"  search    <wzorzec>         Szukaj pakietów w indeksach repozytorium\n"
"  list      [--deployments]   Wyświetl zainstalowane pakiety lub deploymenty\n"
"  pin       <csum_prefix>     Przypnij deployment (ochrona przed cleanup)\n"
"  upgrade                     Aktualizuj obraz bazowy (OCI) + re-layer pakietów\n"
"  rollback                    Wróć do poprzedniego deploymentu\n"
"  rebase    <obraz:tag>       Przełącz na inny obraz bazowy OCI\n"
"  deploy    <obraz:tag>       Inicjalny deployment (bootstrap systemu)\n"
"  cleanup   [--keep N]        Usuń stare deploymenty (domyślnie: keep=2)\n"
"  initramfs --status          Informacje o initramfs aktualnego deploymentu\n"
"\n"
"Opcje globalne:\n"
"  -v, --verbose               Włącz logi DEBUG\n"
"  -c, --config <plik>         Ścieżka do pliku konfiguracyjnego\n"
"                              (domyślnie: /etc/deb-ostree/deb-ostree.hk)\n"
"  --arch <amd64|arm64|armhf>  Nadpisz architekturę z konfiguracji\n"
"  --dry-run                   Symuluj operację bez zmian w systemie\n"
"  -V, --version               Wyświetl wersję i wyjdź\n"
"  -h, --help                  Wyświetl tę pomoc i wyjdź\n"
"\n"
"Przykłady:\n"
"  sudo deb-ostree deploy ghcr.io/mojorg/debian-bootc:bookworm\n"
"  deb-ostree status\n"
"  deb-ostree search vim\n"
"  deb-ostree list\n"
"  deb-ostree list --deployments\n"
"  sudo deb-ostree install vim htop\n"
"  sudo deb-ostree upgrade\n"
"  sudo deb-ostree rollback\n"
"  sudo deb-ostree pin abc123def456\n"
"  sudo deb-ostree cleanup --keep 3\n"
"\n"
"Wszystkie komendy modyfikujące sysroot wymagają uprawnień roota.\n"
"Zmiany wchodzą w życie po następnym reboot -- każda operacja tworzy\n"
"nowy deployment OSTree. Aktywny system NIE jest modyfikowany w miejscu.\n";
}

bool needs_root(const std::string& cmd) {
    return cmd == "install"   || cmd == "uninstall" || cmd == "remove"   ||
           cmd == "upgrade"   || cmd == "rollback"  ||
           cmd == "rebase"    || cmd == "deploy"    ||
           cmd == "cleanup"   || cmd == "pin";
}

bool needs_lock(const std::string& cmd) {
    return needs_root(cmd);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> all(argv + 1, argv + argc);

    std::string config_path = "/etc/deb-ostree/deb-ostree.hk";
    bool verbose  = false;
    bool dry_run  = false;
    std::string arch_override;
    std::vector<std::string> remaining;

    for (size_t i = 0; i < all.size(); ++i) {
        const auto& a = all[i];
        if (a == "-v" || a == "--verbose") {
            verbose = true;
        } else if ((a == "-c" || a == "--config") && i + 1 < all.size()) {
            config_path = all[++i];
        } else if (a == "--arch" && i + 1 < all.size()) {
            arch_override = all[++i];
        } else if (a == "--dry-run") {
            dry_run = true;
        } else if (a == "-V" || a == "--version") {
            std::cout << "deb-ostree " << VERSION << "\n"; return 0;
        } else if (a == "-h" || a == "--help" || a == "help") {
            print_usage(); return 0;
        } else {
            remaining.push_back(a);
        }
    }

    debostree::log::set_verbose(verbose);

    if (remaining.empty()) { print_usage(); return 1; }

    std::string command  = remaining[0];
    std::vector<std::string> cmd_args(remaining.begin() + 1, remaining.end());

    if (dry_run) {
        cmd_args.push_back("--dry-run");
        debostree::log::info("[DRY-RUN] Symulacja -- żadne zmiany nie zostaną zapisane");
    }

    if (needs_root(command) && geteuid() != 0) {
        std::cerr << "deb-ostree: komenda '" << command
                  << "' wymaga uprawnień roota (sudo).\n";
        return 1;
    }

    debostree::Config cfg = debostree::state::load_config(config_path);

    /* Nadpisz architekturę jeśli podano */
    if (!arch_override.empty()) cfg.arch = arch_override;

    /* Lockfile + wykrywanie przerwanej transakcji (#8) */
    std::unique_ptr<debostree::TransactionLock> lock;
    if (needs_lock(command)) {
        try {
            lock = std::make_unique<debostree::TransactionLock>(cfg.overlay_work_dir);
            if (lock->found_incomplete()) {
                std::cerr << "OSTRZEŻENIE: Wykryto przerwaną poprzednią transakcję.\n"
                          << "Uruchom 'deb-ostree cleanup' aby oczyścić, a następnie\n"
                          << "powtórz polecenie.\n\n";
                /* Nie przerywamy -- cleanup jest osobną komendą */
            }
        } catch (const std::exception& e) {
            std::cerr << "deb-ostree: " << e.what() << "\n";
            return 1;
        }
    }

    using namespace debostree::cmd;
    int result = 1;

    if      (command == "status")    result = status   (cmd_args, cfg);
    else if (command == "install")   result = install  (cmd_args, cfg);
    else if (command == "uninstall" ||
             command == "remove")    result = uninstall(cmd_args, cfg);
    else if (command == "search")    result = search   (cmd_args, cfg);
    else if (command == "list")      result = list     (cmd_args, cfg);
    else if (command == "pin")       result = pin      (cmd_args, cfg);
    else if (command == "upgrade")   result = upgrade  (cmd_args, cfg);
    else if (command == "rollback")  result = rollback (cmd_args, cfg);
    else if (command == "rebase")    result = rebase   (cmd_args, cfg);
    else if (command == "deploy")    result = deploy   (cmd_args, cfg);
    else if (command == "cleanup")   result = cleanup  (cmd_args, cfg);
    else if (command == "initramfs") result = initramfs(cmd_args, cfg);
    else {
        std::cerr << "deb-ostree: nieznana komenda '" << command << "'\n\n";
        print_usage();
        return 1;
    }

    /* Oznacz transakcję jako zakończoną jeśli sukces */
    if (lock && result == 0) lock->mark_complete();
    return result;
}
