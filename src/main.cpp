#include "../cmd/commands.h"
#include "../cmd/state_store.h"
#include "../cmd/logging.h"
#include "../cmd/transaction_lock.h"
#include "../cmd/oci_ref.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>

static const char* VERSION = "0.2.0";

/* ── ANSI helpers ── */
namespace ansi {
    static bool tty() { return isatty(STDOUT_FILENO); }
    static std::string c(const char* code, const std::string& s) {
        if (!tty()) return s;
        return std::string("\033[") + code + "m" + s + "\033[0m";
    }
    static std::string bold  (const std::string& s) { return c("1",     s); }
    static std::string dim   (const std::string& s) { return c("2",     s); }
    static std::string cyan  (const std::string& s) { return c("36",    s); }
    static std::string bcyan (const std::string& s) { return c("1;36",  s); }
    static std::string green (const std::string& s) { return c("32",    s); }
    static std::string yellow(const std::string& s) { return c("33",    s); }
    static std::string byellow(const std::string& s){ return c("1;33",  s); }
    static std::string bred  (const std::string& s) { return c("1;31",  s); }
    static std::string bwhite(const std::string& s) { return c("1;97",  s); }
}

static void print_version() {
    using namespace ansi;
    std::cout
        << bcyan("deb-ostree") << " " << bwhite(VERSION) << "\n"
        << dim("rpm-ostree dla Debiana -- OSTree + OCI + libsolv (bez apt/dpkg)\n")
        << dim("(c) 2025 -- licencja LGPL-2.1+\n");
}

static void print_usage() {
    using namespace ansi;
    bool t = tty();

    /* Logo / naglowek */
    if (t) {
        std::cout
            << "\n"
            << "  " << bcyan("╔════════════════════════════════════════╗") << "\n"
            << "  " << bcyan("║") << "  " << bwhite("deb-ostree") << " " << cyan(VERSION)
            << "                    " << bcyan("║") << "\n"
            << "  " << bcyan("║") << "  "
            << dim("immutable Debian z OSTree + OCI + apt  ")
            << bcyan("║") << "\n"
            << "  " << bcyan("╚══════════════════════════════════════╝") << "\n\n";
    } else {
        std::cout << "deb-ostree " << VERSION
                  << " -- immutable Debian z OSTree + OCI + apt\n\n";
    }

    /* Uzycie */
    std::cout << bold("UZYCIE") << "\n"
              << "  " << cyan("deb-ostree") << " "
              << yellow("[opcje]") << " "
              << green("<komenda>") << " "
              << dim("[argumenty...]") << "\n\n";

    /* Komendy -- podzielone na sekcje */
    auto section = [&](const char* title) {
        std::cout << bold(title) << "\n";
    };
    auto cmd = [&](const char* name, const char* args, const char* desc) {
        std::string n = green(name);
        std::string a = args[0] ? " " + yellow(args) : "";
        std::string line = "  " + n + a;
        /* Wyrownanie do 34 znakow (bez kodow ANSI: len(name)+len(args)+3) */
        int raw_len = 2 + (int)std::strlen(name)
                        + (args[0] ? 1 + (int)std::strlen(args) : 0);
        int pad = std::max(1, 36 - raw_len);
        std::cout << line << std::string(pad, ' ') << dim(desc) << "\n";
    };
    auto note = [&](const char* text) {
        std::cout << "  " << dim(text) << "\n";
    };

    /* ── Sekcja: System ── */
    section("SYSTEM (wymagaja root)");
    cmd("deploy",    "<obraz:tag>",    "Inicjalny deployment z obrazu OCI");
    cmd("upgrade",   "",               "Aktualizuj obraz bazowy + pakiety warstwowe");
    cmd("rollback",  "",               "Wróc do poprzedniego deploymentu");
    cmd("rebase",    "<obraz:tag>",    "Przelacz na inny obraz bazowy OCI");
    cmd("cleanup",   "[--keep N]",     "Usun stare deploymenty (domyslnie: keep=2)");
    cmd("pin",       "<csum>",         "Przypnij deployment przed cleanup");
    std::cout << "\n";

    /* ── Sekcja: Pakiety ── */
    section("PAKIETY (wymagaja root)");
    cmd("update",      "",               "Odswiez cache indeksow apt (jak apt-get update)");
    cmd("install",     "<pkg...>",       "Zainstaluj pakiety warstwowe");
    cmd("uninstall",   "<pkg...>",       "Usun pakiety warstwowe");
    cmd("autoremove",  "",               "Usun osierocon e zaleznosci");
    std::cout << "\n";

    /* ── Sekcja: Informacja ── */
    section("INFORMACJA");
    cmd("status",    "",                "Wyswietl deploymenty i pakiety warstwowe");
    cmd("search",    "<wzorzec>",       "Szukaj pakietow w indeksach repozytorium");
    cmd("list",      "[--deployments]", "Wyswietl zainstalowane pakiety lub deploymenty");
    note("  list --upgradeable   Pokaz dostepne aktualizacje pakietow");
    note("  list --files <pkg>   Wyswietl pliki nalezace do pakietu");
    std::cout << "\n";

    /* ── Sekcja: Opcje ── */
    section("OPCJE GLOBALNE");
    auto opt = [&](const char* flags, const char* desc) {
        std::string line = "  " + yellow(flags);
        int raw_len = 2 + (int)std::strlen(flags);
        int pad = std::max(1, 36 - raw_len);
        std::cout << line << std::string(pad, ' ') << dim(desc) << "\n";
    };
    opt("-v, --verbose",          "Wlacz logi DEBUG");
    opt("-c, --config <plik>",    "Plik .hk (domyslnie: /etc/deb-ostree/deb-ostree.hk)");
    opt("--arch <amd64|arm64>",   "Nadpisz architekture z konfiguracji");
    opt("--log-file <plik>",      "Zapisuj logi do pliku (append)");
    opt("--dry-run",              "Symuluj operacje bez zmian w systemie");
    opt("-V, --version",          "Wyswietl wersje");
    opt("-h, --help",             "Wyswietl te pomoc");
    std::cout << "\n";

    /* ── Przykladowe uzycie ── */
    section("PRZYKLADY");
    auto ex = [&](const char* cmd_str, const char* comment) {
        std::cout << "  " << cyan("$") << " " << bwhite(cmd_str);
        if (comment[0])
            std::cout << "  " << dim(std::string("# ") + comment);
        std::cout << "\n";
    };
    std::cout << "  " << dim("WYMAGANY format obrazu OCI: registry/organizacja/obraz:tag") << "\n";
    std::cout << "  " << dim("Przyklad: ghcr.io/twoja-org/debian-bootc:bookworm") << "\n\n";
    ex("sudo deb-ostree deploy ghcr.io/mojorg/debian-bootc:bookworm",
       "inicjalny setup");
    ex("deb-ostree status",                "podglad systemu");
    ex("sudo deb-ostree update",           "odswiez indeksy");
    ex("deb-ostree search vim",            "szukaj pakietu");
    ex("deb-ostree list --upgradeable",    "sprawdz aktualizacje");
    ex("sudo deb-ostree install vim htop", "instalacja");
    ex("sudo deb-ostree upgrade",          "aktualizacja systemu");
    ex("sudo deb-ostree rollback",         "cofniecie zmian");
    std::cout << "\n";

    if (t) {
        std::cout
            << dim("  Dokumentacja: ") << cyan("man deb-ostree")
            << dim("  |  Zglaszanie bledow: ")
            << cyan("https://github.com/TwojaOrg/deb-ostree/issues") << "\n\n";
    }
}

static bool needs_root(const std::string& cmd) {
    return cmd == "install"    || cmd == "uninstall" || cmd == "remove"     ||
           cmd == "upgrade"    || cmd == "rollback"  ||
           cmd == "rebase"     || cmd == "deploy"    ||
           cmd == "cleanup"    || cmd == "pin"       ||
           cmd == "update"     || cmd == "autoremove";
}

static bool needs_lock(const std::string& cmd) {
    return needs_root(cmd);
}

int main(int argc, char** argv) {
    std::vector<std::string> all(argv + 1, argv + argc);

    std::string config_path = "/etc/deb-ostree/deb-ostree.hk";
    bool verbose  = false;
    bool dry_run  = false;
    std::string arch_override;
    std::string log_file;
    std::vector<std::string> remaining;

    for (size_t i = 0; i < all.size(); ++i) {
        const auto& a = all[i];
        if (a == "-v" || a == "--verbose") {
            verbose = true;
        } else if ((a == "-c" || a == "--config") && i + 1 < all.size()) {
            config_path = all[++i];
        } else if (a == "--arch" && i + 1 < all.size()) {
            arch_override = all[++i];
        } else if (a == "--log-file" && i + 1 < all.size()) {
            log_file = all[++i];
        } else if (a == "--dry-run") {
            dry_run = true;
        } else if (a == "-V" || a == "--version") {
            print_version(); return 0;
        } else if (a == "-h" || a == "--help" || a == "help") {
            print_usage(); return 0;
        } else {
            remaining.push_back(a);
        }
    }

    debostree::log::set_verbose(verbose);
    if (!log_file.empty()) debostree::log::set_log_file(log_file);

    if (remaining.empty()) { print_usage(); return 1; }

    std::string command = remaining[0];
    std::vector<std::string> cmd_args(remaining.begin() + 1, remaining.end());

    if (dry_run) {
        cmd_args.push_back("--dry-run");
        std::cout << ansi::byellow("[DRY-RUN]")
                  << " Symulacja -- zadne zmiany nie zostana zapisane\n";
    }

    if (needs_root(command) && geteuid() != 0) {
        std::cerr << ansi::bred("Blad:") << " komenda '"
                  << ansi::yellow(command) << "' wymaga uprawnien root.\n"
                  << "  Sprobuj: " << ansi::cyan("sudo deb-ostree " + command) << "\n";
        return 1;
    }

    debostree::Config cfg = debostree::state::load_config(config_path);
    if (!arch_override.empty()) cfg.arch = arch_override;

    /* TransactionLock */
    std::unique_ptr<debostree::TransactionLock> lock;
    if (needs_lock(command)) {
        try {
            lock = std::make_unique<debostree::TransactionLock>(cfg.overlay_work_dir);
            if (lock->found_incomplete()) {
                std::cerr << ansi::byellow("OSTRZEZENIE:")
                          << " Wykryto przerwan a transakcje.\n"
                          << "  Uruchom: " << ansi::cyan("sudo deb-ostree cleanup")
                          << " aby oczyscic srodowisko.\n\n";
            }
        } catch (const std::exception& e) {
            std::cerr << ansi::bred("Blad blokady:") << " " << e.what() << "\n";
            return 1;
        }
    }

    using namespace debostree::cmd;
    int result = 1;

    if      (command == "status")    result = status    (cmd_args, cfg);
    else if (command == "install")   result = install   (cmd_args, cfg);
    else if (command == "uninstall" ||
             command == "remove")    result = uninstall (cmd_args, cfg);
    else if (command == "search")    result = search    (cmd_args, cfg);
    else if (command == "list")      result = list      (cmd_args, cfg);
    else if (command == "pin")       result = pin       (cmd_args, cfg);
    else if (command == "upgrade")   result = upgrade   (cmd_args, cfg);
    else if (command == "rollback")  result = rollback  (cmd_args, cfg);
    else if (command == "rebase")    result = rebase    (cmd_args, cfg);
    else if (command == "deploy")    result = deploy    (cmd_args, cfg);
    else if (command == "cleanup")   result = cleanup   (cmd_args, cfg);
    else if (command == "initramfs") result = initramfs (cmd_args, cfg);
    else if (command == "diff")     result = diff     (cmd_args, cfg);
    else if (command == "update")    result = update    (cmd_args, cfg);
    else if (command == "autoremove") result = autoremove(cmd_args, cfg);
    else {
        std::cerr << ansi::bred("Blad:") << " nieznana komenda '"
                  << ansi::yellow(command) << "'\n\n";
        print_usage();
        return 1;
    }

    if (lock && result == 0) lock->mark_complete();
    return result;
}
