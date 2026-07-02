#include "../cmd/tree_export.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cerrno>

namespace fs = std::filesystem;

namespace debostree::tree {

/* ── Whiteout helpers ── */

/* OCI/overlayfs whiteout: plik .wh.<oryginal> oznacza usunięcie <oryginal> */
static bool is_whiteout(const std::string& name) {
    return name.rfind(".wh.", 0) == 0;
}

/* Opaque whiteout: .wh..wh..opq oznacza "ten katalog jest od nowa" */
static bool is_opaque_whiteout(const std::string& name) {
    return name == ".wh..wh..opq";
}

static std::string whiteout_original(const std::string& name) {
    return name.substr(4); /* usuń prefix ".wh." */
}

/* ── cp -a based copy ── */

static bool try_cp_a(const std::string& src, const std::string& dst) {
    /* cp -a = --archive (zachowuje wszystko: linki, urządzenia, xattry, uprawnienia) */
    auto r = process::run({"cp", "-a", src + "/.", dst + "/"});
    if (!r.ok()) {
        log::debug("tree_export: cp -a nie powiodło się: " + r.stderr_data
                   + " -- przechodzę na fallback");
        return false;
    }
    return true;
}

/* ── Recursive fallback copy ── */

static void copy_xattrs(const std::string& src_path, const std::string& dst_path) {
    /* Pobierz listę xattrów */
    ssize_t list_len = llistxattr(src_path.c_str(), nullptr, 0);
    if (list_len <= 0) return;

    std::string list_buf(static_cast<size_t>(list_len), '\0');
    llistxattr(src_path.c_str(), list_buf.data(), static_cast<size_t>(list_len));

    /* Kopiuj każdy xattr */
    size_t pos = 0;
    while (pos < list_buf.size()) {
        std::string attr_name = list_buf.data() + pos;
        pos += attr_name.size() + 1;

        ssize_t val_len = lgetxattr(src_path.c_str(), attr_name.c_str(), nullptr, 0);
        if (val_len <= 0) continue;

        std::string val_buf(static_cast<size_t>(val_len), '\0');
        lgetxattr(src_path.c_str(), attr_name.c_str(), val_buf.data(),
                  static_cast<size_t>(val_len));
        lsetxattr(dst_path.c_str(), attr_name.c_str(), val_buf.data(),
                  static_cast<size_t>(val_len), 0);
    }
}

static void copy_entry_recursive(const std::string& src_root,
                                  const std::string& dst_root,
                                  const std::string& rel_path)
{
    std::string src_path = src_root + rel_path;
    std::string dst_path = dst_root + rel_path;

    struct stat st{};
    if (::lstat(src_path.c_str(), &st) < 0) {
        log::warn("tree_export: lstat(" + src_path + "): " + std::strerror(errno));
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        /* Katalog -- utwórz i iteruj */
        ::mkdir(dst_path.c_str(), st.st_mode & 07777);

        DIR* d = ::opendir(src_path.c_str());
        if (!d) return;

        struct dirent* de;
        while ((de = ::readdir(d)) != nullptr) {
            std::string name = de->d_name;
            if (name == "." || name == "..") continue;

            /* Obsługa whiteoutów OCI */
            if (is_opaque_whiteout(name)) continue; /* znacznik katalogu -- ignoruj */
            if (is_whiteout(name)) {
                /* Usuń odpowiadający plik w dst (może nie istnieć -- OK) */
                std::string target = dst_root + rel_path + "/" + whiteout_original(name);
                std::error_code ec;
                fs::remove_all(target, ec);
                continue;
            }

            copy_entry_recursive(src_root, dst_root, rel_path + "/" + name);
        }
        ::closedir(d);

        /* Ustaw uprawnienia po rekurencji (żeby móc pisać do katalogu) */
        ::chmod(dst_path.c_str(), st.st_mode & 07777);
        ::lchown(dst_path.c_str(), st.st_uid, st.st_gid);

    } else if (S_ISLNK(st.st_mode)) {
        /* Dowiązanie symboliczne */
        char link_target[4096];
        ssize_t len = ::readlink(src_path.c_str(), link_target, sizeof(link_target) - 1);
        if (len > 0) {
            link_target[len] = '\0';
            ::symlink(link_target, dst_path.c_str());
            ::lchown(dst_path.c_str(), st.st_uid, st.st_gid);
        }

    } else if (S_ISREG(st.st_mode)) {
        /* Plik regularny -- kopiuj przez sendfile lub read/write */
        int src_fd = ::open(src_path.c_str(), O_RDONLY | O_NOFOLLOW);
        int dst_fd = ::open(dst_path.c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
                            st.st_mode & 07777);
        if (src_fd >= 0 && dst_fd >= 0) {
            char buf[65536];
            ssize_t n;
            while ((n = ::read(src_fd, buf, sizeof(buf))) > 0) {
                ::write(dst_fd, buf, static_cast<size_t>(n));
            }
            ::fchown(dst_fd, st.st_uid, st.st_gid);
            ::fchmod(dst_fd, st.st_mode & 07777);
        }
        if (src_fd >= 0) ::close(src_fd);
        if (dst_fd >= 0) ::close(dst_fd);

    } else if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode) || S_ISFIFO(st.st_mode)) {
        /* Pliki specjalne -- mknod wymaga CAP_MKNOD (root) */
        if (::mknod(dst_path.c_str(), st.st_mode, st.st_rdev) < 0) {
            log::debug("tree_export: mknod(" + dst_path + "): " + std::strerror(errno)
                       + " -- pominięto (wymaga root)");
            return;
        }
        ::lchown(dst_path.c_str(), st.st_uid, st.st_gid);
    }

    /* Kopiuj xattry (capabilities, SELinux labels itd.) */
    copy_xattrs(src_path, dst_path);
}

/* ── Public API ── */

void copy_tree(const std::string& src_dir, const std::string& dst_dir) {
    std::error_code ec;
    fs::create_directories(dst_dir, ec);

    /* Najpierw cp -a -- szybkie i kompletne */
    if (try_cp_a(src_dir, dst_dir)) {
        log::debug("tree_export: cp -a zakończone pomyślnie");
        return;
    }

    /* Fallback: własna iteracja */
    log::debug("tree_export: używam fallbacku (iteracja rekurencyjna)");
    copy_entry_recursive(src_dir, dst_dir, "");
}

void export_overlay_merged(const std::string& overlay_merged_dir,
                            const std::string& dst_dir)
{
    /* merged_dir overlayfs automatycznie scala lower+upper i tłumaczy
     * whiteouty -- z perspektywy odczytującego wygląda jak normalny FS.
     * Używamy copy_tree bezpośrednio na merged_dir. */
    copy_tree(overlay_merged_dir, dst_dir);
}

} // namespace debostree::tree
