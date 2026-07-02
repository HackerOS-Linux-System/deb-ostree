#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/transaction_lock.h"
#include "../cmd/logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int cleanup(const std::vector<std::string>& args, const Config& cfg) {
    int keep_n = 2; /* domyślnie: zachowaj 2 ostatnie deploymenty */
    bool clean_cache = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--keep" && i + 1 < args.size())
            keep_n = std::stoi(args[i + 1]);
        if (args[i] == "--cache")
            clean_cache = true;
    }

    /* Sprawdź plik .incomplete i usuń po potwierdzeniu */
    std::string lock_dir = cfg.overlay_work_dir;
    std::string incomplete = lock_dir + "/transaction.incomplete";
    if (fs::exists(incomplete)) {
        std::cout << "Wykryto przerwana transakcje.\n"
                  << "Czyszczę pliki tymczasowe w " << lock_dir << "...\n";

        std::error_code ec;
        for (auto& name : {"upper", "work", "merged", "final-tree",
                           "base-checkout", "oci-pull", "fetch-tmp"}) {
            fs::remove_all(lock_dir + "/" + name, ec);
        }
        fs::remove(incomplete, ec);
        std::cout << "Pliki tymczasowe wyczyszczone.\n";
    }

    if (clean_cache) {
        std::string lists_dir = cfg.apt_lists_path;
        std::error_code ec;
        int removed = 0;
        if (fs::exists(lists_dir, ec)) {
            for (auto& entry : fs::directory_iterator(lists_dir, ec)) {
                fs::remove(entry.path(), ec);
                ++removed;
            }
        }
        std::cout << "Cache indeksów apt wyczyszczony (" + std::to_string(removed) + " plików).\n";
    }

    /* Cleanup deploymentów */
    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto deployments = sysroot.list_deployments();

        /* Policz nieprzypięte */
        int pinned_count  = 0;
        int booted_count  = 0;
        for (auto& d : deployments) {
            if (d.pinned) ++pinned_count;
            if (d.booted) ++booted_count;
        }

        std::cout << "Deploymentów: " << deployments.size()
                  << " (zachowuję: " << keep_n
                  << ", przypiętych: " << pinned_count << ")\n";

        if (static_cast<int>(deployments.size()) <= keep_n) {
            std::cout << "Brak deploymentów do usunięcia.\n";
            return 0;
        }

        auto res = sysroot.cleanup(keep_n);
        if (!res.success) {
            log::error("cleanup: " + res.error_message);
            return 1;
        }

        std::cout << "Cleanup zakończony. Zachowano " << keep_n
                  << " ostatnich deploymentów.\n";
        return 0;

    } catch (const std::exception& e) {
        log::error(std::string("cleanup: ") + e.what());
        return 1;
    }
}

} // namespace debostree::cmd
