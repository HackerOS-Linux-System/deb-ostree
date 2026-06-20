#pragma once
/*
 * deb-ostree -- state_store.h
 * Wczytywanie konfiguracji z /etc/deb-ostree/deb-ostree.hk (format .hk --
 * patrz cmd/hk_parser.h dla parsera i pelnej specyfikacji formatu).
 *
 * To JEDYNE miejsce w kodzie odpowiedzialne za zaladowanie konfiguracji
 * przez wysokopoziomowe API (Config). Reszta kodu dostaje gotowy struct
 * Config i nigdy nie czyta pliku .hk bezposrednio.
 *
 * Wersja: 0.0.1
 */

#include "types.h"
#include <string>

namespace debostree::state {

/*
 * Wczytuje konfiguracje z path. Jesli plik nie istnieje, zwraca Config
 * z wartosciami domyslnymi (uzyteczne przy testach i pierwszym uruchomieniu).
 * Nieznane klucze sa logowane jako WARN i ignorowane (forward compatibility).
 */
Config load_config(const std::string& path = "/etc/deb-ostree/deb-ostree.hk");

} // namespace debostree::state
