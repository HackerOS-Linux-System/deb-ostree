#pragma once
/*
 * deb-ostree -- tar_extractor.h
 * Wlasna implementacja rozpakowywania archiwow tar (formaty ustar/GNU/PAX
 * naglowkow, najczesciej spotykane w "data.tar" i "control.tar" wewnatrz
 * pakietow .deb). Zastepuje wywolanie zewnetrznego "tar -xpf" -- caly
 * proces instalacji pakietu .deb dzieje sie teraz wewnatrz procesu
 * deb-ostree, bez zaleznosci na zewnetrzne binarki tar/dpkg/apt.
 *
 * Wspierane funkcje:
 *   - pliki regularne, katalogi, symlinki, hardlinki
 *   - zachowanie uid/gid/mode z naglowka
 *   - dlugie nazwy plikow (GNU longname / PAX "path" extended header)
 *   - pliki >8GB (PAX rozmiar w extended header, GNU base-256 rozmiar)
 *
 * NIE wspierane (rzadkie w kontekscie pakietow .deb, nie blokuje typowych
 * instalacji): urzadzenia znakowe/blokowe, FIFO, sparse files.
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree::tarball {

/* Pojedynczy wpis w archiwum tar -- zwracane przez list_entries do
 * podgladu zawartosci bez rozpakowywania. */
struct Entry {
    std::string path;
    std::string link_target;
    uint64_t    size = 0;
    uint32_t    mode = 0;
    uint32_t    uid  = 0;
    uint32_t    gid  = 0;
    char        type = '0'; /* '0'=plik, '5'=katalog, '2'=symlink, '1'=hardlink */
};

/*
 * Rozpakowuje cale archiwum tar (dane juz zdekompresowane) do katalogu
 * docelowego, zachowujac uprawnienia/wlasciciela/symlinki. Tworzy katalogi
 * nadrzedne wedlug potrzeby. dest_dir musi istniec.
 *
 * Rzuca std::runtime_error przy uszkodzonym/nieprawidlowym archiwum.
 */
void extract_to_directory(const std::vector<uint8_t>& tar_data,
                          const std::string& dest_dir);

/*
 * Wyciaga zawartosc JEDNEGO pliku z archiwum tar do pamieci -- uzywane do
 * odczytania pliku "control" z control.tar.* podczas parsowania metadanych
 * pakietu .deb, bez potrzeby rozpakowywania calego archiwum na dysk.
 *
 * Zwraca pusty vector jesli plik nie zostal znaleziony w archiwum.
 */
std::vector<uint8_t> extract_single_file(const std::vector<uint8_t>& tar_data,
                                         const std::string& file_path);

/* Listuje wszystkie wpisy w archiwum tar bez wypakowywania zawartosci. */
std::vector<Entry> list_entries(const std::vector<uint8_t>& tar_data);

} // namespace debostree::tarball
