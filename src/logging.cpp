#include "../cmd/logging.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <unistd.h>

namespace debostree::log {

namespace {
bool         g_verbose  = false;
std::string  g_log_file;
std::ofstream g_log_stream;

/* Timestamp RFC3339 dla logów do pliku */
std::string timestamp() {
    auto now   = std::chrono::system_clock::now();
    auto t     = std::chrono::system_clock::to_time_t(now);
    auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    ::gmtime_r(&t, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
    return oss.str();
}

const char* level_color(Level lvl) {
    switch (lvl) {
        case Level::Debug: return "\033[2m";
        case Level::Info:  return "\033[36m";
        case Level::Warn:  return "\033[33m";
        case Level::Error: return "\033[1;31m";
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

void set_log_file(const std::string& path) {
    if (g_log_stream.is_open()) g_log_stream.close();
    if (path.empty()) { g_log_file.clear(); return; }
    g_log_file = path;
    g_log_stream.open(path, std::ios::app);
    if (!g_log_stream.is_open()) {
        std::cerr << "[WARN ] Nie mozna otworzyc pliku logu: " << path << "\n";
        g_log_file.clear();
    }
}

void emit(Level level, std::string_view msg) {
    if (level == Level::Debug && !g_verbose) return;

    auto& stream = (level == Level::Error || level == Level::Warn)
                   ? std::cerr : std::cout;

    bool tty = (&stream == &std::cerr) ? (isatty(STDERR_FILENO) != 0)
                                        : (isatty(STDOUT_FILENO) != 0);

    /* Wyjście na terminal */
    if (tty) {
        stream << level_color(level)
               << "[" << level_tag(level) << "] \033[0m"
               << msg << "\n";
    } else {
        stream << "[" << level_tag(level) << "] " << msg << "\n";
    }

    /* Wyjście do pliku logu (#12) -- bez kolorów, ze znacznikiem czasu */
    if (g_log_stream.is_open()) {
        g_log_stream << timestamp()
                     << " [" << level_tag(level) << "] "
                     << msg << "\n";
        g_log_stream.flush();
    }
}

} // namespace debostree::log
