#pragma once
/*
 * deb-ostree -- tree_export.h  [NOWY v0.1.0]
 * Eksport scalonego drzewa overlay (lower + upper) do katalogu docelowego
 * z poprawną obsługą hardlinków, urządzeń i whiteout OCI.
 *
 * Problem z std::filesystem::copy (punkt #10 z listy błędów):
 *   - Traci hardlinki (kopiuje każdy plik osobno)
 *   - Nie obsługuje plików specjalnych (urządzenia blokowe/znakowe)
 *   - Nie obsługuje whiteout (pliki .wh.<nazwa> w upperdir overlayfs)
 *
 * Ta implementacja używa cp -a przez process::run() jako preferowaną
 * ścieżkę (zachowuje hardlinki, urządzenia, uprawnienia, xattry)
 * z fallbackiem na własną iterację przez linkat(2)/mknod(2).
 *
 * Wersja: 0.1.0
 */

#include <string>

namespace debostree::tree {

/*
 * Kopiuje src_dir do dst_dir z zachowaniem:
 *   - hardlinków (linkat) gdy na tym samym filesystemie
 *   - plików specjalnych (mknod) dla urządzeń z /dev
 *   - xattrów (POSIX xattr) dla capabilities i SELinux labels
 *   - dowiązań symbolicznych (lstat + symlink, nie podążamy)
 *   - whiteoutów OCI (.wh.* i .wh..wh..opq)
 *
 * Rzuca std::runtime_error przy pierwszym błędzie krytycznym.
 * Błędy dla plików specjalnych wymagających root są logowane jako warn.
 */
void copy_tree(const std::string& src_dir, const std::string& dst_dir);

/*
 * Eksportuje scalone drzewo overlayfs (merged_dir = lower+upper)
 * do dst_dir, prawidłowo obsługując whiteouty overlayfs które muszą
 * być przetłumaczone na usunięcia pliku w dst.
 *
 * Uwaga: overlay_merged_dir musi być aktywnie zamontowany podczas wywołania.
 */
void export_overlay_merged(const std::string& overlay_merged_dir,
                            const std::string& dst_dir);

} // namespace debostree::tree
