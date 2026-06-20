#include "../cmd/commands.h"
#include "../cmd/sysroot.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/deb_layer.h"
#include "../cmd/logging.h"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace debostree::cmd {

int uninstall(const std::vector<std::string>& args, const Config& cfg) {
    if (args.empty()) {
        std::cerr << "Uzycie: deb-ostree uninstall <pakiet> [<pakiet2>...]\n";
        return 1;
    }

    try {
        Sysroot sysroot = Sysroot::open(cfg.sysroot_path);
        auto booted = sysroot.booted_deployment();
        if (!booted) { log::error("Brak zabootowanego deploymentu."); return 1; }

        std::string base = cfg.overlay_work_dir + "/base-checkout";
        fs::remove_all(base);
        fs::create_directories(base);
        sysroot.repo().checkout_commit(booted->checksum, base);

        OverlayManager ovl(cfg.overlay_work_dir + "/session");
        OverlaySession ses = ovl.begin_session(base);
        ovl.bind_mount_virtual_fs(ses);

        bool failed = false; std::string errmsg;
        try {
            DebLayer deb(cfg);
            deb.remove_packages(ses, args);
        } catch (const std::exception& e) { failed = true; errmsg = e.what(); }

        ovl.unbind_virtual_fs(ses);
        if (failed) {
            ovl.discard_session(ses); fs::remove_all(base);
            log::error("Usuniecie nie powiodlo sie: " + errmsg); return 1;
        }
        ovl.end_session(ses);

        std::string final_tree = cfg.overlay_work_dir + "/final-tree";
        fs::remove_all(final_tree);
        fs::create_directories(final_tree);

        OverlaySession ro = ovl.begin_session(base);
        std::error_code ec;
        fs::copy(ro.merged_dir, final_tree,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        ovl.end_session(ro);

        if (ec) { log::error("Kopiowanie: " + ec.message()); return 1; }

        std::string subject = "deb-ostree uninstall:";
        for (auto& p : args) subject += " " + p;

        std::string new_csum = sysroot.repo().commit_directory(
            final_tree, booted->origin_refspec, subject);

        /* Usuwamy z listy pakietow warstwowych te, ktore zostaly odinstalowane. */
        std::vector<PackageLayer> remaining;
        for (auto& ep : booted->layered_packages) {
            bool removed = false;
            for (auto& n : args) if (ep.name == n) { removed = true; break; }
            if (!removed) remaining.push_back(ep);
        }

        auto res = sysroot.deploy_commit(
            new_csum, cfg.osname, booted->origin_refspec, remaining);

        fs::remove_all(base);
        fs::remove_all(final_tree);

        if (!res.success) {
            log::error("Deploy nie powiodl sie: " + res.error_message); return 1;
        }

        std::cout << "Usunieto pakiet(y). Wykonaj reboot, aby zmiany weszly w zycie.\n";
        return 0;
    } catch (const std::exception& e) {
        log::error(std::string("uninstall: ") + e.what()); return 1;
    }
}

} // namespace debostree::cmd
