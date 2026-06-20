#include "../cmd/sysroot.h"
#include "../cmd/logging.h"

#include <gio/gio.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace debostree {

/* ---- Sysroot::open ------------------------------------------------------ */

Sysroot Sysroot::open(const std::string& path) {
    GFile* gf = g_file_new_for_path(path.c_str());
    ::OstreeSysroot* sysroot = ostree_sysroot_new(gf);
    g_object_unref(gf);

    GErrorGuard err;
    if (!ostree_sysroot_load(sysroot, nullptr, err.ptr())) {
        g_object_unref(sysroot);
        err.check_throw("Nie mozna wczytac sysroot: " + path);
    }

    /* Repo jest wbudowane w sysroot, ale otwieramy je przez nasz wlasny
     * ostree::Repo::open() (osobna sciezka /ostree/repo) zamiast pobierac
     * surowy wskaznik przez ostree_sysroot_get_repo(), zeby zachowac
     * jednolite zarzadzanie wlasnoscia obiektu (GObjPtr RAII) w calym kodzie. */
    std::string repo_path = path;
    if (!repo_path.empty() && repo_path.back() == '/')
        repo_path.pop_back();
    repo_path += "/ostree/repo";

    ostree::Repo repo = ostree::Repo::open(repo_path);
    log::debug("Otworzono sysroot: " + path);
    return Sysroot(sysroot, std::move(repo));
}

/* ---- to_deployment_struct (private) ------------------------------------- */

Deployment Sysroot::to_deployment_struct(::OstreeDeployment* dep,
                                          bool is_booted) const
{
    Deployment d;
    d.osname    = ostree_deployment_get_osname(dep);
    d.checksum  = ostree_deployment_get_csum(dep);
    d.serial    = ostree_deployment_get_deployserial(dep);
    d.booted    = is_booted;

    /* Origin file (GKeyFile) trzyma refspec i liste naszych pakietow. */
    GKeyFile* origin = ostree_deployment_get_origin(dep);
    if (origin) {
        char* refspec = g_key_file_get_string(origin, "origin", "refspec", nullptr);
        if (refspec) { d.origin_refspec = refspec; g_free(refspec); }

        gsize n = 0;
        char** pkgs = g_key_file_get_string_list(
            origin, "deb-ostree", "layered-packages", &n, nullptr);
        if (pkgs) {
            for (gsize i = 0; i < n; ++i) {
                PackageLayer pl;
                pl.name = pkgs[i];
                pl.op   = LayerOp::Install;
                d.layered_packages.push_back(pl);
            }
            g_strfreev(pkgs);
        }
    }

    d.id = d.osname + "-" + d.checksum.substr(0, 10) + "."
         + std::to_string(d.serial);
    return d;
}

/* ---- Sysroot::list_deployments ------------------------------------------ */

std::vector<Deployment> Sysroot::list_deployments() const {
    GPtrArray* arr    = ostree_sysroot_get_deployments(sysroot_.get());
    ::OstreeDeployment* booted = ostree_sysroot_get_booted_deployment(sysroot_.get());

    std::vector<Deployment> result;
    result.reserve(arr->len);

    for (guint i = 0; i < arr->len; ++i) {
        auto* dep = static_cast<::OstreeDeployment*>(g_ptr_array_index(arr, i));
        bool ib = booted && ostree_deployment_equal(dep, booted);
        Deployment d = to_deployment_struct(dep, ib);
        if (i == 0) d.staged = !ib; /* #0 jest staged jesli != booted */
        result.push_back(std::move(d));
    }
    g_ptr_array_unref(arr);
    return result;
}

/* ---- Sysroot::booted_deployment ----------------------------------------- */

std::optional<Deployment> Sysroot::booted_deployment() const {
    ::OstreeDeployment* b = ostree_sysroot_get_booted_deployment(sysroot_.get());
    if (!b) return std::nullopt;
    return to_deployment_struct(b, true);
}

/* ---- Sysroot::deploy_commit --------------------------------------------- */

TransactionResult Sysroot::deploy_commit(
    const std::string&              checksum,
    const std::string&              osname,
    const std::string&              refspec,
    const std::vector<PackageLayer>& packages)
{
    TransactionResult res;
    GErrorGuard err;

    /* Origin file zawiera refspec i liste nalozonych pakietow .deb. */
    GKeyFile* origin = g_key_file_new();
    g_key_file_set_string(origin, "origin", "refspec", refspec.c_str());

    if (!packages.empty()) {
        std::vector<const char*> names;
        names.reserve(packages.size());
        for (auto& p : packages) names.push_back(p.name.c_str());
        g_key_file_set_string_list(origin, "deb-ostree", "layered-packages",
                                   names.data(), names.size());
    }

    ::OstreeDeployment* new_dep = nullptr;
    if (!ostree_sysroot_deploy_tree(
            sysroot_.get(), osname.c_str(), checksum.c_str(),
            origin, nullptr, nullptr, &new_dep, nullptr, err.ptr())) {
        g_key_file_free(origin);
        res.error_message = "deploy_tree nie powiodlo sie";
        return res;
    }
    g_key_file_free(origin);

    /* simple_write_deployment:
     *   - merge /etc (3-way: bazowy vs poprzedni deployment vs biezace zmiany)
     *   - aktualizacja bootloadera (BLS entries w /boot/loader/entries)
     *   - atomowy swap symlinku "default" na nowy deployment  */
    if (!ostree_sysroot_simple_write_deployment(
            sysroot_.get(), osname.c_str(), new_dep,
            nullptr, /* merge_deployment: NULL = auto-wykryj zabootowany */
            OSTREE_SYSROOT_SIMPLE_WRITE_DEPLOYMENT_FLAGS_NONE,
            nullptr, err.ptr())) {
        g_object_unref(new_dep);
        res.error_message = "simple_write_deployment nie powiodlo sie";
        return res;
    }
    g_object_unref(new_dep);

    res.success         = true;
    res.new_checksum    = checksum;
    res.requires_reboot = true;
    log::info("Deployment " + checksum.substr(0, 12) +
              " zarejestrowany -- wymagany reboot.");
    return res;
}

/* ---- Sysroot::rollback -------------------------------------------------- */

TransactionResult Sysroot::rollback() {
    TransactionResult res;
    GPtrArray* arr = ostree_sysroot_get_deployments(sysroot_.get());

    if (arr->len < 2) {
        g_ptr_array_unref(arr);
        res.error_message = "Brak poprzedniego deploymentu do rollbacku.";
        return res;
    }

    /* Zamien #0 i #1 w liscie -- OstreeSysroot traktuje kolejnosc jako priorytet. */
    GPtrArray* reordered = g_ptr_array_new_with_free_func(g_object_unref);
    g_ptr_array_add(reordered, g_object_ref(g_ptr_array_index(arr, 1)));
    g_ptr_array_add(reordered, g_object_ref(g_ptr_array_index(arr, 0)));
    for (guint i = 2; i < arr->len; ++i)
        g_ptr_array_add(reordered, g_object_ref(g_ptr_array_index(arr, i)));
    g_ptr_array_unref(arr);

    GErrorGuard err;
    if (!ostree_sysroot_write_deployments(sysroot_.get(), reordered, nullptr, err.ptr())) {
        g_ptr_array_unref(reordered);
        res.error_message = "write_deployments (rollback) nie powiodlo sie";
        return res;
    }
    g_ptr_array_unref(reordered);

    res.success         = true;
    res.requires_reboot = true;
    log::info("Rollback przygotowany -- poprzedni deployment bedzie domyslny po reboot.");
    return res;
}

/* ---- Sysroot::cleanup --------------------------------------------------- */

TransactionResult Sysroot::cleanup(int keep_last_n) {
    TransactionResult res;
    GPtrArray* arr = ostree_sysroot_get_deployments(sysroot_.get());

    if (static_cast<int>(arr->len) <= keep_last_n) {
        log::info("Brak deploymentow do usuniecia (jest " +
                  std::to_string(arr->len) + ").");
        g_ptr_array_unref(arr);
        res.success = true;
        return res;
    }

    GPtrArray* kept = g_ptr_array_new_with_free_func(g_object_unref);
    for (int i = 0; i < keep_last_n; ++i)
        g_ptr_array_add(kept, g_object_ref(g_ptr_array_index(arr, i)));
    g_ptr_array_unref(arr);

    GErrorGuard err;
    if (!ostree_sysroot_write_deployments(sysroot_.get(), kept, nullptr, err.ptr())) {
        g_ptr_array_unref(kept);
        res.error_message = "write_deployments (cleanup) nie powiodlo sie";
        return res;
    }
    g_ptr_array_unref(kept);

    /* Fizyczne usuniecie obiektow repo ktore nie sa juz referencjonowane. */
    GErrorGuard err2;
    if (!ostree_sysroot_cleanup(sysroot_.get(), nullptr, err2.ptr())) {
        res.error_message = "sysroot_cleanup nie powiodlo sie";
        return res;
    }

    res.success = true;
    log::info("Cleanup: zachowano " + std::to_string(keep_last_n) +
              " ostatnie deploymenty.");
    return res;
}

/* ---- Sysroot::deployment_path ------------------------------------------- */

std::string Sysroot::deployment_path(const Deployment& dep) const {
    GFile* gf  = ostree_sysroot_get_path(sysroot_.get());
    char*  base = g_file_get_path(gf);
    std::string p = std::string(base) + "/ostree/deploy/" + dep.osname +
                    "/deploy/" + dep.checksum + "." + std::to_string(dep.serial);
    g_free(base);
    return p;
}

} // namespace debostree
