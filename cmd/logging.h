#pragma once
/*
 * deb-ostree -- logging.h
 * Prosty, kolorowy logger ANSI z poziomami DEBUG/INFO/WARN/ERROR.
 * Wyjscie kolorowe tylko gdy stdout/stderr jest TTY (np. nie do pliku/pipe).
 *
 * Wersja: 0.0.1
 */

#include <string_view>

namespace debostree::log {

enum class Level { Debug, Info, Warn, Error };

void set_verbose(bool verbose);
void emit(Level level, std::string_view msg);

inline void debug(std::string_view m) { emit(Level::Debug, m); }
inline void info (std::string_view m) { emit(Level::Info,  m); }
inline void warn (std::string_view m) { emit(Level::Warn,  m); }
inline void error(std::string_view m) { emit(Level::Error, m); }

} // namespace debostree::log
