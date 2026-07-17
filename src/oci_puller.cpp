#include "../cmd/oci_puller.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"
#include "../cmd/progress.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <stdexcept>

namespace fs = std::filesystem;

namespace debostree {

OciPuller::OciPuller(std::string work_dir)
    : work_dir_(std::move(work_dir))
{
    fs::create_directories(work_dir_);
}

void OciPuller::check_tools_available() {
    auto skopeo = process::run({"skopeo", "--version"});
    if (!skopeo.ok())
        throw std::runtime_error(
            "OciPuller: 'skopeo' niedostepny.\n"
            "Zainstaluj: sudo apt-get install skopeo");

    auto podman = process::run({"podman", "--version"});
    if (!podman.ok())
        throw std::runtime_error(
            "OciPuller: 'podman' niedostepny.\n"
            "Zainstaluj: sudo apt-get install podman");

    log::debug("OciPuller: skopeo=" + skopeo.stdout_data.substr(0,20) +
               " podman=" + podman.stdout_data.substr(0,20));
}

std::string OciPuller::pull_and_unpack(const std::string& image_ref) {
    check_tools_available();

    std::string rootfs = work_dir_ + "/rootfs";
    fs::remove_all(rootfs);
    fs::create_directories(rootfs);

    log::info("Sciagam obraz OCI: " + image_ref);

    /* Krok 1: skopeo pull do storage podmana */
    {
        progress::ScopedSpinner sp("skopeo: pull " + image_ref);
        auto r = process::run({"skopeo", "copy",
                               "--quiet",
                               "docker://" + image_ref,
                               "containers-storage:" + image_ref});
        if (!r.ok()) {
            sp.fail("skopeo copy: " + r.stderr_data.substr(0, 120));
            throw std::runtime_error(
                "skopeo copy nie powiodlo sie dla " + image_ref +
                ":\n" + r.stderr_data);
        }
        sp.done();
    }

    /* Krok 2: podman image mount wewnatrz podman unshare -- zachowuje
     * xattry i capabilities (#14).
     *
     * Strategia: podman unshare sh -c "podman image mount ... && cp -a ... && umount"
     * cp -a jest rownowazne rsync -a: zachowuje hardlinki, uprawnienia, xattry,
     * symlinki, pliki specjalne. Lepsze niz podman export ktory tworzy flat tar
     * bez xattrow. */
    {
        progress::ScopedSpinner sp("podman: montowanie i eksport warstw");

        /* Timeout (#14): jeśli podman image mount zawiesi się (corrupted storage),
         * alarm(2) wyśle SIGALRM po 300 sekundach co przerwie process::run(). */
        ::alarm(300);

        std::string script =
            "set -e\n"
            "mnt=$(podman image mount " + image_ref + ")\n"
            "cp -a \"$mnt/.\" " + rootfs + "/\n"
            "podman image umount " + image_ref + "\n";

        auto r = process::run({"podman", "unshare", "sh", "-c", script});
        ::alarm(0); /* Anuluj alarm po zakończeniu */
        if (!r.ok()) {
            sp.fail("podman unshare: " + r.stderr_data.substr(0, 120));
            /* Sprzatamy storage podmana */
            process::run({"podman", "image", "rm", "-f", image_ref});
            throw std::runtime_error(
                "Eksport warstw OCI nie powiodl sie:\n" + r.stderr_data +
                "\nUpewnij sie ze podman ma dostep do fuse-overlayfs lub kernel overlayfs.");
        }
        sp.done();
    }

    /* Krok 3: usun obraz z lokalnego storage podmana (nie potrzebny po eksporcie) */
    {
        auto r = process::run({"podman", "image", "rm", "-f", image_ref});
        if (!r.ok())
            log::warn("Nie mozna usunac obrazu z podman storage: " + r.stderr_data);
    }

    log::info("Obraz OCI rozpakowany do " + rootfs +
              " (z zachowaniem xattrow i capabilities)");
    return rootfs;
}

} // namespace debostree
