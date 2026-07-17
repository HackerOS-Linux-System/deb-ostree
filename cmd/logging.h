#pragma once
/*
 * deb-ostree -- logging.h
 * Kolorowy logger ANSI z poziomami DEBUG/INFO/WARN/ERROR.
 * Obsługuje opcjonalny log do pliku (--log-file) (#12).
 *
 * Wersja: 0.2.0
 */

#include <string_view>
#include <string>

namespace debostree::log {

enum class Level { Debug, Info, Warn, Error };

void set_verbose(bool verbose);
/* Ustawia plik logu (np. /var/log/deb-ostree.log).
 * Pusta ścieżka = wyłącz logowanie do pliku.
 * Plik jest otwierany w trybie append. (#12) */
void set_log_file(const std::string& path);
void emit(Level level, std::string_view msg);

inline void debug(std::string_view m) { emit(Level::Debug, m); }
inline void info (std::string_view m) { emit(Level::Info,  m); }
inline void warn (std::string_view m) { emit(Level::Warn,  m); }
inline void error(std::string_view m) { emit(Level::Error, m); }

} // namespace debostree::log
