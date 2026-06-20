#include "../cmd/ostree_repo.h"
#include "../cmd/logging.h"

#include <gio/gio.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>

namespace debostree::ostree {

/* ---- Repo::open --------------------------------------------------------- */

Repo Repo::open(const std::string& path) {
    GFile* gf = g_file_new_for_path(path.c_str());
    ::OstreeRepo* r = ostree_repo_new(gf);
    g_object_unref(gf);

    GErrorGuard err;
    if (!ostree_repo_open(r, nullptr, err.ptr())) {
        g_object_unref(r);
        err.check_throw("Nie mozna otworzyc repo OSTree w " + path);
    }
    log::debug("Otworzono repo OSTree: " + path);
    return Repo(r);
}

/* ---- Repo::create ------------------------------------------------------- */

Repo Repo::create(const std::string& path) {
    GFile* gf = g_file_new_for_path(path.c_str());
    ::OstreeRepo* r = ostree_repo_new(gf);
    g_object_unref(gf);

    GErrorGuard err;
    /* bare-user: zachowuje uid/gid/xattrs bez wymagania dokladnych uprawnien
     * na poziomie systemu plikow hosta. Wlasciwy tryb dla obrazow OCI/bootc. */
    if (!ostree_repo_create(r, OSTREE_REPO_MODE_BARE_USER, nullptr, err.ptr())) {
        g_object_unref(r);
        err.check_throw("Nie mozna utworzyc repo OSTree w " + path);
    }
    log::info("Utworzono repo OSTree (bare-user): " + path);
    return Repo(r);
}

/* ---- Repo::commit_directory --------------------------------------------- */

std::string Repo::commit_directory(const std::string& dir_path,
                                   const std::string& refspec,
                                   const std::string& subject,
                                   const std::string& body)
{
    GErrorGuard err;

    if (!ostree_repo_prepare_transaction(repo_.get(), nullptr, nullptr, err.ptr()))
        err.check_throw("prepare_transaction");

    /* OstreeMutableTree buduje wirtualne drzewo w pamieci zanim trafi na dysk. */
    OstreeMutableTree* mtree = ostree_mutable_tree_new();
    OstreeRepoCommitModifier* mod = ostree_repo_commit_modifier_new(
        OSTREE_REPO_COMMIT_MODIFIER_FLAGS_NONE, nullptr, nullptr, nullptr);

    GFile* root_gf = g_file_new_for_path(dir_path.c_str());

    if (!ostree_repo_write_directory_to_mtree(
            repo_.get(), root_gf, mtree, mod, nullptr, err.ptr())) {
        g_object_unref(root_gf);
        g_object_unref(mtree);
        ostree_repo_commit_modifier_unref(mod);
        ostree_repo_abort_transaction(repo_.get(), nullptr, nullptr);
        err.check_throw("write_directory_to_mtree: " + dir_path);
    }
    g_object_unref(root_gf);
    ostree_repo_commit_modifier_unref(mod);

    GFile* tree_obj = nullptr;
    if (!ostree_repo_write_mtree(repo_.get(), mtree, &tree_obj, nullptr, err.ptr())) {
        g_object_unref(mtree);
        ostree_repo_abort_transaction(repo_.get(), nullptr, nullptr);
        err.check_throw("write_mtree");
    }
    g_object_unref(mtree);

    char* checksum = nullptr;
    if (!ostree_repo_write_commit(
            repo_.get(),
            nullptr,                        /* parent -- nullptr = niezalezny commit  */
            subject.c_str(),
            body.empty() ? nullptr : body.c_str(),
            nullptr,                        /* metadata GVariant -- przyszlosc        */
            OSTREE_REPO_FILE(tree_obj),
            &checksum, nullptr, err.ptr())) {
        g_object_unref(tree_obj);
        ostree_repo_abort_transaction(repo_.get(), nullptr, nullptr);
        err.check_throw("write_commit");
    }
    g_object_unref(tree_obj);

    ostree_repo_transaction_set_ref(
        repo_.get(), nullptr, refspec.c_str(), checksum);

    if (!ostree_repo_commit_transaction(repo_.get(), nullptr, nullptr, err.ptr())) {
        g_free(checksum);
        err.check_throw("commit_transaction");
    }

    std::string result(checksum);
    g_free(checksum);
    log::info("Commit " + result.substr(0, 12) + " -> ref " + refspec);
    return result;
}

/* ---- Repo::resolve_ref -------------------------------------------------- */

std::optional<std::string> Repo::resolve_ref(const std::string& refspec) {
    char* checksum = nullptr;
    GErrorGuard err;
    /* allow_noent = TRUE: brak refa nie jest bledem (np. pierwsze uzycie). */
    if (!ostree_repo_resolve_rev(
            repo_.get(), refspec.c_str(), TRUE, &checksum, err.ptr()))
        err.check_throw("resolve_rev: " + refspec);

    if (!checksum) return std::nullopt;
    std::string r(checksum);
    g_free(checksum);
    return r;
}

/* ---- Repo::checkout_commit ---------------------------------------------- */

void Repo::checkout_commit(const std::string& checksum, const std::string& dest) {
    GErrorGuard err;

    OstreeRepoCheckoutAtOptions opts{};
    opts.mode           = OSTREE_REPO_CHECKOUT_MODE_USER;
    opts.overwrite_mode = OSTREE_REPO_CHECKOUT_OVERWRITE_NONE;
    opts.process_whiteouts = TRUE; /* OCI-style whiteouts dla usunietych plikow */

    if (!ostree_repo_checkout_at(
            repo_.get(), &opts, AT_FDCWD, dest.c_str(),
            checksum.c_str(), nullptr, err.ptr()))
        err.check_throw("checkout_at dla " + checksum.substr(0, 12) + " -> " + dest);

    log::info("Checkout " + checksum.substr(0, 12) + " -> " + dest);
}

/* ---- Repo::read_commit_info --------------------------------------------- */

CommitInfo Repo::read_commit_info(const std::string& checksum) {
    GErrorGuard err;
    GVariant* cv = nullptr;

    if (!ostree_repo_load_variant(
            repo_.get(), OSTREE_OBJECT_TYPE_COMMIT,
            checksum.c_str(), &cv, err.ptr()))
        err.check_throw("load_variant dla " + checksum.substr(0, 12));

    CommitInfo info;
    info.checksum = checksum;

    /* GVariant layout commita OSTree: (a{sv} aay s s s aay tay)
     * idx 3 = subject, idx 4 = body, idx 5 = timestamp (u64 big-endian) */
    const char* subject = nullptr;
    const char* body    = nullptr;
    g_variant_get_child(cv, 3, "&s", &subject);
    g_variant_get_child(cv, 4, "&s", &body);

    GVariant* ts = g_variant_get_child_value(cv, 5);
    info.timestamp = static_cast<int64_t>(GUINT64_FROM_BE(g_variant_get_uint64(ts)));
    g_variant_unref(ts);

    info.subject = subject ? subject : "";
    info.body    = body    ? body    : "";

    g_variant_unref(cv);
    return info;
}

} // namespace debostree::ostree
