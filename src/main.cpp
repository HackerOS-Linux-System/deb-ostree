#include "../cmd/commands.h"
#include "../cmd/state_store.h"
#include "../cmd/logging.h"

#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

static const char* VERSION = "0.0.1";

void print_usage() {
    std::cout <<
"deb-ostree " << VERSION << " -- zarzadzanie systemem Debian w modelu immutable/image-based\n"
"             (odpowiednik rpm-ostree dla ekosystemu .deb/apt + OCI/bootc)\n"
"\n"
"Uzycie: deb-ostree [opcje] <komenda> [argumenty]\n"
"\n"
"Komendy:\n"
"  status                   Wyswietl aktywne i poprzednie deploymenty\n"
"  install   <pkg...>       Naloz pakiet(y) .deb jako warstwa na obraz bazowy\n"
"  uninstall <pkg...>       Usun pakiet(y) warstwowe\n"
"  upgrade                  Aktualizuj obraz bazowy (OCI) + re-layer pakietow\n"
"  rollback                 Wroc do poprzedniego deploymentu\n"
"  rebase    <obraz:tag>    Przelacz na inny obraz bazowy OCI\n"
"  deploy    <obraz:tag>    Inicjalny deployment (bootstrap systemu)\n"
"  cleanup   [--keep N]     Usun stare deploymenty (domyslnie: keep=2)\n"
"  initramfs --status       Informacje o initramfs aktualnego deploymentu\n"
"\n"
"Opcje globalne:\n"
"  -v, --verbose            Wlacz logi DEBUG\n"
"  -c, --config <plik>      Sciezka do pliku konfiguracyjnego\n"
"                           (domyslnie: /etc/deb-ostree/deb-ostree.hk)\n"
"  -V, --version            Wyswietl wersje i wyjdz\n"
"  -h, --help               Wyswietl ta pomoc i wyjdz\n"
"\n"
"Przyklady:\n"
"  sudo deb-ostree deploy ghcr.io/mojorg/debian-bootc:bookworm\n"
"  deb-ostree status\n"
"  sudo deb-ostree install vim htop\n"
"  sudo deb-ostree upgrade\n"
"  sudo deb-ostree rollback\n"
"  sudo deb-ostree cleanup --keep 3\n"
"\n"
"Wszystkie komendy modyfikujace sysroot wymagaja uprawnien roota.\n"
"Zmiany wchodza w zycie po nastepnym reboot (system nie jest modyfikowany\n"
"w miejscu -- kazda operacja tworzy nowy deployment OSTree).\n";
}

/* Komendy ktore wymagaja root (wszystkie poza status/initramfs). */
bool needs_root(const std::string& cmd) {
    return cmd == "install"   || cmd == "uninstall" ||
           cmd == "upgrade"   || cmd == "rollback"  ||
           cmd == "rebase"    || cmd == "deploy"    ||
           cmd == "cleanup";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> all(argv + 1, argv + argc);

    std::string config_path = "/etc/deb-ostree/deb-ostree.hk";
    bool verbose = false;
    std::vector<std::string> remaining;

    for (size_t i = 0; i < all.size(); ++i) {
        const auto& a = all[i];
        if (a == "-v" || a == "--verbose") {
            verbose = true;
        } else if ((a == "-c" || a == "--config") && i + 1 < all.size()) {
            config_path = all[++i];
        } else if (a == "-V" || a == "--version") {
            std::cout << "deb-ostree " << VERSION << "\n"; return 0;
        } else if (a == "-h" || a == "--help" || a == "help") {
            print_usage(); return 0;
        } else {
            remaining.push_back(a);
        }
    }

    debostree::log::set_verbose(verbose);

    if (remaining.empty()) {
        print_usage();
        return 1;
    }

    std::string command = remaining[0];
    std::vector<std::string> cmd_args(remaining.begin() + 1, remaining.end());

    if (needs_root(command) && geteuid() != 0) {
        std::cerr << "deb-ostree: komenda '" << command
                  << "' wymaga uprawnien roota (sudo).\n";
        return 1;
    }

    debostree::Config cfg = debostree::state::load_config(config_path);

    using namespace debostree::cmd;
    if (command == "status")    return status   (cmd_args, cfg);
    if (command == "install")   return install  (cmd_args, cfg);
    if (command == "uninstall") return uninstall(cmd_args, cfg);
    if (command == "upgrade")   return upgrade  (cmd_args, cfg);
    if (command == "rollback")  return rollback (cmd_args, cfg);
    if (command == "rebase")    return rebase   (cmd_args, cfg);
    if (command == "deploy")    return deploy   (cmd_args, cfg);
    if (command == "cleanup")   return cleanup  (cmd_args, cfg);
    if (command == "initramfs") return initramfs(cmd_args, cfg);

    std::cerr << "deb-ostree: nieznana komenda '" << command << "'\n\n";
    print_usage();
    return 1;
}
