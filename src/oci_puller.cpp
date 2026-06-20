#include "../cmd/oci_puller.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace debostree {

OciPuller::OciPuller(std::string work_dir)
    : work_dir_(std::move(work_dir))
{
    fs::create_directories(work_dir_);
}

std::string OciPuller::pull_and_unpack(const std::string& image_ref) {
    std::string oci_dir   = work_dir_ + "/oci-image";
    std::string rootfs    = work_dir_ + "/rootfs";
    std::string tar_path  = work_dir_ + "/export.tar";

    fs::remove_all(oci_dir);
    fs::remove_all(rootfs);
    fs::remove(tar_path);
    fs::create_directories(rootfs);

    log::info("Sciagam obraz OCI: " + image_ref);

    /* --- krok 1: skopeo copy do lokalnego OCI layout --- */
    auto r = process::run({"skopeo", "copy",
                           "docker://" + image_ref,
                           "oci:" + oci_dir + ":latest"});
    if (!r.ok())
        throw std::runtime_error("skopeo copy nie powiodlo sie dla " + image_ref +
                                 ":\n" + r.stderr_data);

    /* --- krok 2: podman create (scala warstwy bez uruchamiania) --- */
    const std::string cname = "deb-ostree-unpack-tmp";
    process::run({"podman", "rm", "-f", cname}); /* ignorujemy blad jesli nie istnieje */

    auto cr = process::run({"podman", "create", "--name", cname,
                            "oci:" + oci_dir + ":latest"});
    if (!cr.ok())
        throw std::runtime_error("podman create nie powiodlo sie:\n" + cr.stderr_data);

    /* --- krok 3: podman export -> tar ze scalonym rootfs --- */
    auto er = process::run({"podman", "export", cname});
    process::run({"podman", "rm", "-f", cname}); /* sprzatamy kontener */

    if (!er.ok())
        throw std::runtime_error("podman export nie powiodlo sie:\n" + er.stderr_data);

    /* Zapisujemy tar do pliku tymczasowego -- tar stdin przyjmuje stdin,
     * ale bezpieczniej jest miec plik (latwy retry, diagnostyka rozmiaru). */
    {
        std::ofstream tf(tar_path, std::ios::binary);
        if (!tf)
            throw std::runtime_error("Nie mozna zapisac " + tar_path);
        tf.write(er.stdout_data.data(),
                 static_cast<std::streamsize>(er.stdout_data.size()));
    }

    /* --- krok 4: rozpakowujemy tar do rootfs (z zachowaniem uprawnien) --- */
    auto xr = process::run({"tar", "-xpf", tar_path,
                            "-C", rootfs,
                            "--same-owner",
                            "--xattrs",
                            "--xattrs-include=*"});
    fs::remove(tar_path);

    if (!xr.ok())
        throw std::runtime_error("Rozpakowywanie tar rootfs nie powiodlo sie:\n" +
                                 xr.stderr_data);

    log::info("Obraz rozpakowany do " + rootfs);
    return rootfs;
}

} // namespace debostree
