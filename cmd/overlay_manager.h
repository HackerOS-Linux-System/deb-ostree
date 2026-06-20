#pragma once
/*
 * deb-ostree -- overlay_manager.h
 * Montuje overlayfs nad rozpakowanym (checkout) drzewem OSTree, zeby dpkg/apt
 * mogly "zainstalowac" pakiet bez modyfikowania niemutowalnych plikow z repo.
 *
 * Schemat warstw:
 *   lowerdir  = checkout OSTree (read-only, "the base image")
 *   upperdir  = pusty scratch dir (tu trafiaja WSZYSTKIE zapisy dpkg/apt)
 *   workdir   = wymagany przez kernel overlayfs (wewnetrzny, nie uzywany wprost)
 *   mergeddir = punkt montowania widoczny przez dpkg/chroot (lower + upper)
 *
 * Po zakonczeniu instalacji upperdir zawiera CALA DELTE wzgledem lower:
 *   - nowe pliki dodane przez dpkg
 *   - zmodyfikowane pliki (kopia calego pliku, nie patch)
 *   - usuniecia jako pliki ".wh.<nazwa>" (OCI-style whiteouts)
 *
 * Tę deltę (lower + upper = scalona warstwa) commitujemy do OSTree jako
 * nowy commit -- read-only, bezpieczny, z pelnym rollbackiem.
 *
 * Wersja: 0.0.1
 */

#include <string>

namespace debostree {

/* Stan aktywnej sesji overlay. Przekazywany do bind_mount_virtual_fs i end_session. */
struct OverlaySession {
    std::string lower_dir;
    std::string upper_dir;
    std::string work_dir;
    std::string merged_dir;
    bool        mounted = false;
};

class OverlayManager {
public:
    /*
     * work_root: katalog roboczy (np. /var/lib/deb-ostree/overlay-work/session).
     * Wszystkie podkatalogi (upper/work/merged) tworzone sa wewnatrz work_root.
     */
    explicit OverlayManager(std::string work_root);

    /*
     * Tworzy upper/work/merged w work_root, montuje overlayfs z lower_dir
     * jako baza (read-only). Wymaga CAP_SYS_ADMIN (root).
     * Rzuca std::runtime_error jesli mount() sie nie powiedzie.
     */
    OverlaySession begin_session(const std::string& lower_dir);

    /*
     * Bind-mountuje /proc, /sys, /dev, /dev/pts do merged_dir tak, ze skrypty
     * dpkg postinst/preinst maja normalnie dzialajace srodowisko (ldconfig,
     * update-initramfs, systemctl --root, dbus-uuidgen itd.).
     * Kopiuje tez /etc/resolv.conf na potrzeby potencjalnych DNS-lookupow.
     */
    void bind_mount_virtual_fs(const OverlaySession& session);

    /* Odpowiednik bind_mount_virtual_fs -- odmontowuje /proc,/sys,/dev w merged_dir.
     * Wywolywac PRZED end_session! */
    void unbind_virtual_fs(const OverlaySession& session);

    /* Odmontowuje merged_dir. upper_dir jest zachowany (do pozniejszego commit lub copy). */
    void end_session(OverlaySession& session);

    /* Odmontowuje merged_dir i usuwa upper_dir/work_dir (porzuca zmiany).
     * Wywolywac gdy operacja (apt-get install) sie nie powiodla. */
    void discard_session(OverlaySession& session);

private:
    std::string work_root_;
};

} // namespace debostree
