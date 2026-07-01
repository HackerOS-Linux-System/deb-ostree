#pragma once
/*
 * deb-ostree -- deb_archive.h
 * Wlasny parser formatu pakietu .deb -- zastepuje "dpkg -x"/"dpkg -e".
 *
 * Format .deb to archiwum "ar" (ten sam format co statyczne biblioteki .a)
 * zawierajace trzy wpisy w ustalonej kolejnosci:
 *
 *   1. "debian-binary"            -- tekst "2.0\n" (wersja formatu .deb)
 *   2. "control.tar" (gz/xz/zst)  -- metadane pakietu (plik "control",
 *                                     skrypty preinst/postinst/prerm/postrm,
 *                                     md5sums, conffiles)
 *   3. "data.tar" (gz/xz/zst)     -- faktyczna zawartosc pakietu (pliki
 *                                     ktore trafiaja do systemu)
 *
 * Naglowek archiwum ar (globalny, 8 bajtow): "!<arch>\n"
 * Naglowek kazdego wpisu (60 bajtow):
 *   offset 0,  16B: nazwa pliku (padded spacjami)
 *   offset 16, 12B: mtime (tekst dziesietny)
 *   offset 28, 6B:  uid
 *   offset 34, 6B:  gid
 *   offset 40, 8B:  mode (oktal)
 *   offset 48, 10B: rozmiar danych (tekst dziesietny)
 *   offset 58, 2B:  magic konca naglowka 0x60 0x0A
 * Dane wpisu nastepuja zaraz po naglowku, wyrownane do parzystej liczby
 * bajtow (padding '\n' jesli rozmiar nieparzysty).
 *
 * Ten parser deleguje dekompresje do compress_util i rozpakowywanie tar do
 * tar_extractor -- deb_archive.cpp "rozumie" tylko format ar i sklada
 * pozostale moduly w calosc.
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree::deb {

/* Pole "control" z control.tar -- parsowane RFC822-podobnie tak jak
 * apt::PackageEntry, ale dla pojedynczego pakietu (nie indeksu repo). */
struct ControlInfo {
    std::string package;
    std::string version;
    std::string architecture;
    std::string depends;
    std::string pre_depends;
    std::string provides;
    std::string conflicts;
    std::string replaces;
    std::string breaks;
    std::string maintainer;
    std::string description;
};

/*
 * DebArchive otwiera plik .deb z dysku i udostepnia jego skladniki: control
 * info, skrypty maintainer (preinst/postinst/prerm/postrm) i metode
 * rozpakowania danych (data.tar.*) do katalogu docelowego.
 */
class DebArchive {
public:
    /* Wczytuje caly plik .deb do pamieci i parsuje strukture ar (NIE
     * dekompresuje jeszcze control.tar/data.tar -- to nastepuje na
     * zadanie przez read_control()/extract_data_to()). */
    static DebArchive open(const std::string& deb_path);

    /* Parsuje plik "control" z control.tar.* i zwraca ControlInfo. */
    ControlInfo read_control() const;

    /* Zwraca tresc skryptu maintainer-a (np. "postinst", "preinst") jako
     * string, lub pusty string jesli pakiet go nie ma. */
    std::string read_maintainer_script(const std::string& script_name) const;

    /* Rozpakowuje data.tar.* (faktyczna zawartosc pakietu) do dest_dir.
     * Odpowiednik tego co "dpkg -x pkg.deb dest_dir" robil wczesniej --
     * teraz cala logika jest wewnatrz procesu deb-ostree. */
    void extract_data_to(const std::string& dest_dir) const;

    /* Lista plikow ktore trafilyby do systemu (z data.tar) bez ich
     * rozpakowywania -- uzywane do raportowania/podgladu przed instalacja. */
    std::vector<std::string> list_data_files() const;

private:
    std::vector<uint8_t> control_tar_;  /* juz zdekompresowane */
    std::vector<uint8_t> data_tar_raw_; /* NIE zdekompresowane -- dekompresja
                                         * danych pakietu (moze byc duza)
                                         * odkladana do faktycznej instalacji */

    static std::vector<uint8_t> read_whole_file(const std::string& path);
};

} // namespace debostree::deb
