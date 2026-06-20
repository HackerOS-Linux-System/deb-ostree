#pragma once
/*
 * deb-ostree -- ostree_repo.h
 * Cienka, idiomatyczna otoczka C++ na libostree (OstreeRepo*, GError*, GLib).
 *
 * CALY kod deb-ostree poza tym plikiem i sysroot.h NIE dotyka surowego C API
 * libostree ani GLib bezposrednio. Dzieki temu zarzadzanie zasobami GLib
 * (refcounting, GError) jest zlokalizowane w dwoch plikach.
 *
 * Konwencja nazw: klasa C++ to debostree::ostree::Repo, bo typ C z libostree
 * to "OstreeRepo" -- unikamy konfliktu nazw w tej samej jednostce translacji.
 *
 * Wersja: 0.0.1
 */

#include <ostree.h>
#include <glib.h>

#include <string>
#include <optional>
#include <stdexcept>

namespace debostree {

/* Wyjatek niosacy tresc GError z libostree/GLib. */
class OstreeError : public std::runtime_error {
public:
    explicit OstreeError(const std::string& what) : std::runtime_error(what) {}
};

/*
 * RAII dla GError*. Po kazdym wywolaniu C API libostree robimy:
 *   GErrorGuard err;
 *   ostree_repo_open(..., err.ptr());
 *   err.check_throw("kontekst");
 * Destruktor zwalnia err_ jesli check_throw nie zostalo wywolane (lub nie rzucilo).
 */
class GErrorGuard {
public:
    GErrorGuard() = default;
    ~GErrorGuard() { if (err_) g_error_free(err_); }

    GError** ptr()  { return &err_; }
    bool failed()   const { return err_ != nullptr; }

    void check_throw(const std::string& context) {
        if (err_) {
            std::string msg = context + ": " +
                              (err_->message ? err_->message : "nieznany blad GLib");
            g_error_free(err_);
            err_ = nullptr;
            throw OstreeError(msg);
        }
    }

private:
    GError* err_ = nullptr;
};

/*
 * RAII dla dowolnego GObject* (OstreeRepo, OstreeSysroot, OstreeDeployment...).
 * Zwalnia przez g_object_unref w destruktorze; nie kopiowalne, tylko ruchome.
 */
template <typename T>
class GObjPtr {
public:
    GObjPtr() = default;
    explicit GObjPtr(T* ptr) : ptr_(ptr) {}

    GObjPtr(const GObjPtr&)            = delete;
    GObjPtr& operator=(const GObjPtr&) = delete;

    GObjPtr(GObjPtr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    GObjPtr& operator=(GObjPtr&& o) noexcept {
        if (this != &o) { reset(); ptr_ = o.ptr_; o.ptr_ = nullptr; }
        return *this;
    }

    ~GObjPtr() { reset(); }

    void reset(T* p = nullptr) {
        if (ptr_) g_object_unref(G_OBJECT(ptr_));
        ptr_ = p;
    }

    T*       get()  const { return ptr_; }
    T*       operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

/* Metadane pojedynczego commita OSTree (uzywane przy "status"). */
struct CommitInfo {
    std::string checksum;
    std::string subject;
    std::string body;
    int64_t     timestamp    = 0;
    uint64_t    content_size = 0;
};

namespace ostree {

/*
 * Wrapper na natywny ::OstreeRepo* z libostree.
 * Odpowiedzialnosc: otwieranie/tworzenie repo, commitowanie katalogu rootfs,
 * resolwowanie refow, checkout commita do katalogu docelowego.
 */
class Repo {
public:
    /* Otwiera istniejace repo OSTree (rzuca OstreeError jesli nie istnieje). */
    static Repo open(const std::string& path);

    /* Tworzy nowe repo trybu "bare-user" (przechowuje uid/gid/xattrs bez
     * potrzeby posiadania dokladnych uprawnien do kazdego pliku na hoście). */
    static Repo create(const std::string& path);

    /*
     * Commituje caly katalog dir_path jako nowy commit OSTree pod refem
     * refspec (np. "debian/bookworm/x86_64"). Zwraca checksum (sha256).
     * Wywolywane po skopiowaniu warstwy overlayfs (lower + upper) do
     * tymczasowego katalogu final-tree.
     */
    std::string commit_directory(const std::string& dir_path,
                                 const std::string& refspec,
                                 const std::string& subject,
                                 const std::string& body = "");

    /* Rozwija ref na checksum najnowszego commita (lub nullopt gdy ref nie istnieje). */
    std::optional<std::string> resolve_ref(const std::string& refspec);

    /*
     * Checkout commita checksum do katalogu dest_dir w trybie "user"
     * (zachowuje xattrs/uid-gid przez mechanizm bare-user OSTree, bez
     * wymagania uprawnien do setuid na kazdym pliku hosta).
     */
    void checkout_commit(const std::string& checksum, const std::string& dest_dir);

    /* Pelne metadane commita (do "deb-ostree status" i historii). */
    CommitInfo read_commit_info(const std::string& checksum);

    /* Surowy wskaznik C, potrzebny przez sysroot.cpp do konstrukcji OstreeSysroot. */
    ::OstreeRepo* raw() const { return repo_.get(); }

    Repo(const Repo&)            = delete;
    Repo& operator=(const Repo&) = delete;
    Repo(Repo&&)                 = default;
    Repo& operator=(Repo&&)      = default;

private:
    explicit Repo(::OstreeRepo* r) : repo_(r) {}
    GObjPtr<::OstreeRepo> repo_;
};

} // namespace ostree
} // namespace debostree
