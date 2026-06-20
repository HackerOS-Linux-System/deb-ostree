#include "../cmd/logging.h"
#include <iostream>
#include <unistd.h>

namespace debostree::log {

namespace {
bool g_verbose = false;

const char* level_color(Level lvl) {
    switch (lvl) {
        case Level::Debug: return "\033[2m";      /* ciemny szary   */
        case Level::Info:  return "\033[36m";     /* cyan           */
        case Level::Warn:  return "\033[33m";     /* zolty          */
        case Level::Error: return "\033[1;31m";   /* jasny czerwony */
    }
    return "";
}

const char* level_tag(Level lvl) {
    switch (lvl) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}
} // namespace

void set_verbose(bool v) { g_verbose = v; }

void emit(Level level, std::string_view msg) {
    if (level == Level::Debug && !g_verbose) return;

    auto& stream = (level == Level::Error || level == Level::Warn)
                   ? std::cerr : std::cout;

    bool tty = (&stream == &std::cerr) ? (isatty(STDERR_FILENO) != 0)
                                        : (isatty(STDOUT_FILENO) != 0);

    if (tty) {
        stream << level_color(level)
               << "[" << level_tag(level) << "] "
               << "\033[0m"
               << msg << "\n";
    } else {
        stream << "[" << level_tag(level) << "] " << msg << "\n";
    }
}

} // namespace debostree::log
