#pragma once
/*
 * deb-ostree -- sources_parser.h  [NOWY v0.2.0]
 * Parser /etc/apt/sources.list i /etc/apt/sources.list.d/ --
 * te same pliki co apt, bez wywolywania apt.
 *
 * Parsuje format RFC 822 (sources.list) i DEB822 (sources.list.d/[*].sources).
 * Wynik: wektor stringow "deb <url> <suite> <comp...>" identyczny z
 * formatem który deb-ostree uzywalo w [apt] -> source_N.
 *
 * Priorytet: system apt_sources_list > apt_sources_dir > .hk source_N
 */

#include <string>
#include <vector>

namespace debostree::sources {

/*
 * Wczytuje zrodla apt z:
 *   1. <sources_list>        (/etc/apt/sources.list)
 *   2. <sources_dir>/[*].list  (/etc/apt/sources.list.d/[*].list)
 *   3. <sources_dir>/[*].sources  (format DEB822)
 *
 * Pomija linie zaczynajace sie od '#', puste linie, i linie "deb-src".
 * Zwraca wektor stringow "deb <url> <suite> <comp...>" gotowych do
 * uzycia przez deb::parse_apt_source_line().
 *
 * Jesli zaden plik nie istnieje -- zwraca pusty wektor
 * (caller uzyje source_N z .hk jako fallback).
 */
std::vector<std::string> load_sources(const std::string& sources_list,
                                       const std::string& sources_dir);

/*
 * Parsuje pojedynczy plik sources.list (format "deb <url> <suite> <comp>").
 * Ignoruje komentarze, deb-src, puste linie.
 */
std::vector<std::string> parse_sources_list(const std::string& path);

/*
 * Parsuje plik .sources (format DEB822 -- wieloliniowy RFC 822).
 * Przykladowy blok:
 *   Types: deb
 *   URIs: http://deb.debian.org/debian
 *   Suites: bookworm
 *   Components: main contrib
 */
std::vector<std::string> parse_sources_deb822(const std::string& path);

} // namespace debostree::sources
