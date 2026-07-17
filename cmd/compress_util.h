#pragma once
/*
 * deb-ostree -- compress_util.h
 * Dekompresja gzip i xz/lzma do pamieci -- uzywana do:
 *   - Packages.gz / Packages.xz (indeksy apt sciagniete z mirror)
 *   - control.tar.xz / control.tar.gz wewnatrz archiwum .deb (DebArchive)
 *   - data.tar.xz / data.tar.gz / data.tar.zst wewnatrz archiwum .deb
 *
 * Implementacja oparta na zlib (gzip), liblzma (xz) i libzstd (zstd) --
 * standardowe, dobrze przetestowane biblioteki kompresji. NIE
 * reimplementujemy algorytmow kompresji od zera.
 *
 * Wersja: 0.2.0
 */

#include <string>
#include <vector>
#include <cstdint>

namespace debostree::compress {

/* Format kompresji wykryty z magic bytes danych. */
enum class Format {
    Gzip,
    Xz,
    Zstd,
    None, /* dane nieskompresowane */
};

/* Wykrywa format na podstawie pierwszych bajtow danych (magic number) --
 * niezalezne od rozszerzenia pliku/nazwy. */
Format detect_format(const std::vector<uint8_t>& data);

/* Dekompresuje bufor gzip do bufora wynikowego (zlib). Rzuca
 * std::runtime_error przy bledzie strumienia. */
std::vector<uint8_t> decompress_gzip(const std::vector<uint8_t>& input);

/* Dekompresuje bufor xz/lzma2 do bufora wynikowego (liblzma). */
std::vector<uint8_t> decompress_xz(const std::vector<uint8_t>& input);

/* Dekompresuje bufor zstd (libzstd) -- uzywane przez nowsze .deb z data.tar.zst. */
std::vector<uint8_t> decompress_zstd(const std::vector<uint8_t>& input);

/* Automatycznie wykrywa format (detect_format) i wywoluje odpowiedni
 * decompress_*. Dla Format::None zwraca input bez zmian (kopia). */
std::vector<uint8_t> decompress_auto(const std::vector<uint8_t>& input);

/* Wygodne nadkladki operujace na plikach z dysku -- uzywane dla duzych
 * indeksow Packages.gz (kilkadziesiat MB), gdzie strumieniowanie do/z
 * pliku jest bezpieczniejsze pamieciowo niz trzymanie calosci w RAM
 * dwukrotnie (skompresowane + zdekompresowane). */
void decompress_file_auto(const std::string& input_path, const std::string& output_path);

} // namespace debostree::compress
