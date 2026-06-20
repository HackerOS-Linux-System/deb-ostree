#pragma once
/*
 * deb-ostree -- commands.h
 * Deklaracje funkcji podkomend CLI. Kazda odpowiada jednemu plikowi src/cmd_*.cpp.
 *
 * Sygnatura jest jednolita: (args, cfg) -> exit code (0 = sukces, 1 = blad).
 * Bledy wewnetrzne sa logowane przez debostree::log::error() PRZED zwroceniem 1,
 * zeby uzytkownik widzial czytelny komunikat, a nie tylko "exit 1".
 *
 * Wersja: 0.0.1
 */

#include "types.h"
#include <vector>
#include <string>

namespace debostree::cmd {

int status    (const std::vector<std::string>& args, const Config& cfg);
int install   (const std::vector<std::string>& args, const Config& cfg);
int uninstall (const std::vector<std::string>& args, const Config& cfg);
int upgrade   (const std::vector<std::string>& args, const Config& cfg);
int rollback  (const std::vector<std::string>& args, const Config& cfg);
int rebase    (const std::vector<std::string>& args, const Config& cfg);
int deploy    (const std::vector<std::string>& args, const Config& cfg);
int cleanup   (const std::vector<std::string>& args, const Config& cfg);
int initramfs (const std::vector<std::string>& args, const Config& cfg);

} // namespace debostree::cmd
