#include "../cmd/dpkg_status.h"
#include "../cmd/logging.h"

namespace debostree::dpkg_compat {

void sync_dpkg_status(const std::string& rootfs_path,
                      const std::vector<statusdb::InstalledPackage>& /*packages*/,
                      const std::vector<deb::ControlInfo>& /*control_infos*/)
{
    /* NO-OP: status_db.cpp bezposrednio uzywa /var/lib/dpkg/status.
     * Synchronizacja nie jest potrzebna. */
    log::debug("dpkg_compat::sync_dpkg_status: no-op (status_db jest primary)");
    (void)rootfs_path;
}

void remove_from_dpkg_status(const std::string& rootfs_path,
                              const std::string& package_name)
{
    /* NO-OP: status_db::remove() juz aktualizuje /var/lib/dpkg/status. */
    log::debug("dpkg_compat::remove_from_dpkg_status: no-op dla " + package_name);
    (void)rootfs_path;
    (void)package_name;
}

} // namespace debostree::dpkg_compat
