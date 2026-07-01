#pragma once
/*
 * deb-ostree -- apt_repo_index.h
 * Parser indeksow pakietow w formacie APT "Packages" (RFC822-like, pola
 * "Klucz: wartosc" rozdzielone pustymi liniami miedzy pakietami).
 *
 * Ten parser jest JEDYNYM miejscem ktore "rozumie" format Packages -- nie
 * wywolujemy apt-get/apt-cache do niczego. Plik Packages.gz/Packages.xz
 * jest sciagany z mirrora (przez DebFetcher) i dekompresowany (przez
 * compress_util), a tutaj parsowany do struktur uzywanych przez SolvPool
 * do zbudowania puli libsolv.
 *
 * Format pliku Packages (przyklad jednego wpisu):
 *
 *   Package: vim
 *   Version: 2:9.0.1378-2
 *   Architecture: amd64
 *   Depends: vim-common (= 2:9.0.1378-2), libc6 (>= 2.34), libacl1 (>= 2.2.23)
 *   Pre-Depends: dpkg (>= 1.14.0)
 *   Conflicts: vim-tiny
 *   Provides: editor
 *   Filename: pool/main/v/vim/vim_9.0.1378-2_amd64.deb
 *   Size: 1234567
 *   SHA256: a1b2c3...
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree::apt {

/*
 * Pojedynczy wpis pakietu w indeksie Packages. Pola odpowiadaja 1:1 polom
 * RFC822 najbardziej istotnym dla rozwiazywania zaleznosci i pobierania.
 * Pola ktorych SolvPool/DebFetcher nie uzywaja (np. Description) sa
 * ignorowane podczas parsowania -- nie zachowujemy calego RFC822 w pamieci.
 */
struct PackageEntry {
    std::string package;        /* "Package:"      */
    std::string version;        /* "Version:"      */
    std::string architecture;   /* "Architecture:"  */
    std::string filename;       /* "Filename:" -- sciezka relatywna w mirror */
    std::string sha256;         /* "SHA256:" -- suma kontrolna pliku .deb    */
    uint64_t    size = 0;       /* "Size:" w bajtach                         */

    /* Surowe linie zaleznosci, BEZ parsowania -- SolvPool (poprzez
     * solv_parse_deps z libsolv) parsuje skladnie "pkg (>= wersja), pkg2"
     * dokladnie tak jak prawdziwy libapt-pkg, wiec nie reimplementujemy
     * tego parsera samodzielnie. */
    std::string depends;        /* "Depends:"      */
    std::string pre_depends;    /* "Pre-Depends:"   */
    std::string recommends;     /* "Recommends:"   */
    std::string conflicts;      /* "Conflicts:"    */
    std::string provides;       /* "Provides:"     */
    std::string replaces;       /* "Replaces:"     */
    std::string breaks;         /* "Breaks:"       */
};

/*
 * RepoIndex to sparsowana zawartosc jednego pliku Packages -- lista wszystkich
 * wpisow w porzadku wystepowania w pliku (porzadek nie ma znaczenia dla
 * libsolv, ale zachowujemy go dla powtarzalnosci/debugowania).
 */
class RepoIndex {
public:
    /* Parsuje zawartosc pliku Packages (juz zdekompresowanego -- patrz
     * compress_util dla gz/xz) z podanego stringa. */
    static RepoIndex parse(const std::string& content);

    /* Wczytuje i parsuje plik Packages z dysku (np. po sciagnieciu i
     * zdekompresowaniu przez DebFetcher::download_and_decompress_index). */
    static RepoIndex load_file(const std::string& path);

    const std::vector<PackageEntry>& entries() const { return entries_; }

private:
    std::vector<PackageEntry> entries_;
};

} // namespace debostree::apt
