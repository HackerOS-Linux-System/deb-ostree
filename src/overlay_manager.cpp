#include "../cmd/overlay_manager.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace fs = std::filesystem;

namespace debostree {

OverlayManager::OverlayManager(std::string work_root)
    : work_root_(std::move(work_root))
{
    fs::create_directories(work_root_);
}

OverlaySession OverlayManager::begin_session(const std::string& lower_dir) {
    OverlaySession s;
    s.lower_dir  = lower_dir;
    s.upper_dir  = work_root_ + "/upper";
    s.work_dir   = work_root_ + "/work";
    s.merged_dir = work_root_ + "/merged";

    /* Czyscimy pozostalosci po ewentualnie przerwanej poprzedniej sesji. */
    fs::remove_all(s.upper_dir);
    fs::remove_all(s.work_dir);
    fs::remove_all(s.merged_dir);
    fs::create_directories(s.upper_dir);
    fs::create_directories(s.work_dir);
    fs::create_directories(s.merged_dir);

    std::ostringstream opts;
    opts << "lowerdir="  << s.lower_dir
         << ",upperdir=" << s.upper_dir
         << ",workdir="  << s.work_dir;

    /* Early-check: czy overlayfs jest dostepny w kernelu (#4).
     * Sprawdzamy /proc/filesystems -- jesli "overlay" nie ma wpisu,
     * probujemy modprobe overlay przed wlasciwym mount(). */
    {
        std::ifstream proc_fs("/proc/filesystems");
        std::string line;
        bool overlay_available = false;
        while (std::getline(proc_fs, line)) {
            if (line.find("overlay") != std::string::npos) {
                overlay_available = true;
                break;
            }
        }
        if (!overlay_available) {
            log::debug("overlayfs nie znaleziony w /proc/filesystems -- probuje modprobe");
            auto mp = process::run({"modprobe", "overlay"});
            if (!mp.ok()) {
                throw std::runtime_error(
                    "overlayfs niedostepny w tym kernelu.\n"
                    "Sprawdz:\n"
                    "  cat /proc/filesystems | grep overlay\n"
                    "  modprobe overlay\n"
                    "W kontenerach Docker wymagana flaga --privileged lub "
                    "--cap-add SYS_ADMIN.");
            }
        }
    }

    auto res = process::run({"mount", "-t", "overlay", "overlay",
                             "-o", opts.str(), s.merged_dir});
    if (!res.ok())
        throw std::runtime_error("Montowanie overlayfs nie powiodlo sie:\n" +
                                 res.stderr_data);

    s.mounted = true;
    log::info("Overlay zamontowany: " + s.lower_dir + " -> " + s.merged_dir);
    return s;
}

void OverlayManager::bind_mount_virtual_fs(const OverlaySession& s) {
    /* Para {zrodlo, cel_wewnatrz_merged}. */
    const std::vector<std::pair<std::string, std::string>> binds = {
        {"/proc",     s.merged_dir + "/proc"},
        {"/sys",      s.merged_dir + "/sys"},
        {"/dev",      s.merged_dir + "/dev"},
        {"/dev/pts",  s.merged_dir + "/dev/pts"},
    };

    for (auto& [src, dst] : binds) {
        fs::create_directories(dst);
        auto r = process::run({"mount", "--rbind", src, dst});
        if (!r.ok())
            log::warn("bind mount " + src + " -> " + dst + " nie powiodl sie: "
                      + r.stderr_data);
    }

    /* resolv.conf potrzebny gdy dpkg postinst odpytuje DNS (rzadko, ale bywa). */
    std::string rdst = s.merged_dir + "/etc/resolv.conf";
    if (fs::exists("/etc/resolv.conf")) {
        std::error_code ec;
        fs::copy_file("/etc/resolv.conf", rdst,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) log::warn("Kopiowanie resolv.conf nie powiodlo sie: " + ec.message());
    }
}

void OverlayManager::unbind_virtual_fs(const OverlaySession& s) {
    /* Odmontowujemy w odwrotnej kolejnosci -- /dev/pts przed /dev. */
    for (auto& m : {s.merged_dir + "/dev/pts",
                    s.merged_dir + "/dev",
                    s.merged_dir + "/sys",
                    s.merged_dir + "/proc"}) {
        process::run({"umount", "-l", m}); /* lazy -- ignorujemy bledy */
    }
}

void OverlayManager::end_session(OverlaySession& s) {
    if (!s.mounted) return;

    auto r = process::run({"umount", s.merged_dir});
    if (!r.ok()) {
        log::warn("Zwykly umount nie powiodl sie, probuje lazy: " + r.stderr_data);
        process::run({"umount", "-l", s.merged_dir});
    }
    s.mounted = false;
    log::debug("Sesja overlay zakonczona: " + s.merged_dir);
}

void OverlayManager::discard_session(OverlaySession& s) {
    end_session(s);
    fs::remove_all(s.upper_dir);
    fs::remove_all(s.work_dir);
    log::info("Sesja overlay odrzucona -- zmiany wyczyszczone.");
}

} // namespace debostree
