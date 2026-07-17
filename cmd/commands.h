#pragma once
/*
 * deb-ostree -- commands.h
 * Deklaracje funkcji podkomend CLI.
 *
 * Wersja: 0.2.0
 *   - Dodano: update, autoremove
 *   - Wersja bumped: 0.1.0 -> 0.2.0
 */

#include "types.h"
#include <vector>
#include <string>

namespace debostree::cmd {

/* ── v0.1.0 ── */
int status    (const std::vector<std::string>& args, const Config& cfg);
int install   (const std::vector<std::string>& args, const Config& cfg);
int uninstall (const std::vector<std::string>& args, const Config& cfg);
int upgrade   (const std::vector<std::string>& args, const Config& cfg);
int rollback  (const std::vector<std::string>& args, const Config& cfg);
int rebase    (const std::vector<std::string>& args, const Config& cfg);
int deploy    (const std::vector<std::string>& args, const Config& cfg);
int cleanup   (const std::vector<std::string>& args, const Config& cfg);
int initramfs (const std::vector<std::string>& args, const Config& cfg);
int search    (const std::vector<std::string>& args, const Config& cfg);
int list      (const std::vector<std::string>& args, const Config& cfg);
int pin       (const std::vector<std::string>& args, const Config& cfg);

/* ── v0.2.0 ── */
int diff       (const std::vector<std::string>& args, const Config& cfg);
int update      (const std::vector<std::string>& args, const Config& cfg);
int autoremove  (const std::vector<std::string>& args, const Config& cfg);

} // namespace debostree::cmd
