#pragma once
/*
 * deb-ostree -- hk_parser.h
 * Minimalny parser formatu .hk (uzywanego w calym ekosystemie HackerOS)
 * wystarczajacy do wczytania pliku konfiguracyjnego deb-ostree.hk.
 *
 * Pelna specyfikacja formatu:
 * https://hackeros-linux-system.github.io/HackerOS-Website/tools-docs/hk.html
 *
 * Ten parser wspiera podzbior formatu potrzebny dla configu deb-ostree:
 *   - sekcje [nazwa]
 *   - klucze "-> klucz => wartosc" (poziom 1 -- deb-ostree.hk nie potrzebuje
 *     glebszego zagniezdzenia niz plaska sekcja -> klucz)
 *   - komentarze zaczynajace sie od '!'
 *   - typy: string (cytowany i plain) -- inne typy (number/bool/array) nie sa
 *     potrzebne w configu deb-ostree, ale parser je rozpoznaje i zwraca jako
 *     string znormalizowany (np. "true"/"false", liczba jako tekst)
 *
 * NIE wspiera (bo deb-ostree.hk tego nie potrzebuje): interpolacji ${...},
 * kluczy kropkowych, map zagniezdzonych glebiej niz 1 poziom, tablic.
 * Jesli te funkcje sa potrzebne w przyszlosci, patrz pelna implementacja
 * referencyjna w hackeros-builder (internal/hk -- Go).
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <map>
#include <stdexcept>

namespace debostree::hk {

/* Wyjatek niosacy blad parsowania z numerem linii (analog hk::ParseError). */
class HkParseError : public std::runtime_error {
public:
    HkParseError(int line, const std::string& msg)
        : std::runtime_error("hk: parse error at line " + std::to_string(line) +
                             ": " + msg),
          line_(line) {}
    int line() const { return line_; }

private:
    int line_;
};

/*
 * HkDocument to uproszczona reprezentacja sparsowanego pliku .hk: mapa
 * "sekcja.klucz" -> wartosc (string). Plaska reprezentacja jest wystarczajaca
 * dla configu deb-ostree, ktory uzywa tylko jednego poziomu zagniezdzenia
 * (sekcja -> klucz, bez map zagniezdzonych).
 */
class HkDocument {
public:
    /* Parsuje zawartosc pliku .hk z podanego stringa. Rzuca HkParseError
     * przy bledzie skladni. */
    static HkDocument parse(const std::string& content);

    /* Wczytuje i parsuje plik z dysku. Rzuca std::runtime_error jesli plik
     * nie istnieje (NIE jest to traktowane jako blad parsowania -- caller
     * decyduje czy brak pliku to blad czy "uzyj wartosci domyslnych"). */
    static HkDocument loadFile(const std::string& path);

    /* Zwraca wartosc pod "sekcja.klucz", lub defaultValue jesli nie istnieje. */
    std::string getOr(const std::string& section, const std::string& key,
                      const std::string& defaultValue) const;

    /* Zwraca true jesli dana kombinacja sekcja.klucz istnieje w dokumencie. */
    bool has(const std::string& section, const std::string& key) const;

private:
    /* Klucz mapy to "sekcja.klucz" (plaska reprezentacja -- prosta i
     * wystarczajaca dla zakresu uzycia w deb-ostree). */
    std::map<std::string, std::string> values_;

    void set(const std::string& section, const std::string& key, const std::string& value);
    static std::string makeFlatKey(const std::string& section, const std::string& key);
};

} // namespace debostree::hk
