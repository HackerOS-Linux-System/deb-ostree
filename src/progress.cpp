#include "../cmd/progress.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <unistd.h>
#include <sys/ioctl.h>

namespace debostree::progress {

/* ── Helpers ── */

static bool detect_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

static uint64_t ms_now() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/* Ramki paska -- Unicode block elements */
static constexpr const char* BAR_FULL  = "█";
static constexpr const char* BAR_SEVEN = "▉";
static constexpr const char* BAR_HALF  = "▌";
static constexpr const char* BAR_EMPTY = "░";

/* Znaki spinnera */
static const char* SPINNER_FRAMES[] = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
};
static constexpr int SPINNER_COUNT = 10;

/* ── ProgressBar ── */

ProgressBar::ProgressBar(std::string title, int stages, int bar_width)
    : title_(std::move(title))
    , stages_(stages)
    , bar_width_(bar_width)
    , tty_(detect_tty())
{
    if (tty_) {
        /* Tytuł operacji — wypisujemy raz na początku */
        std::cout
            << ansi::BOLD << ansi::BCYAN
            << "╔═ " << title_ << " "
            << ansi::RESET << "\n";
    } else {
        std::cout << "[START] " << title_ << "\n";
    }
    std::cout.flush();
}

ProgressBar::~ProgressBar() {
    /* Upewniamy się że kursor jest na nowej linii przy destrukcji */
    if (tty_) {
        std::cout << "\n";
        std::cout.flush();
    }
}

void ProgressBar::begin_stage(const std::string& label) {
    ++current_stage_;
    stage_label_    = label;
    stage_start_ms_ = ms_now();

    if (tty_) {
        std::cout
            << ansi::DIM << "║ " << ansi::RESET
            << ansi::CYAN
            << "[" << current_stage_ << "/" << stages_ << "] "
            << ansi::BOLD << label
            << ansi::RESET << "\n";
    } else {
        std::cout << "[" << current_stage_ << "/" << stages_ << "] " << label << "\n";
    }
    std::cout.flush();
}

std::string ProgressBar::build_bar(int filled, int width) const {
    if (!tty_) return "";

    std::string bar;
    bar.reserve(width * 4); /* UTF-8: do 4 bajtów na znak */

    for (int i = 0; i < width; ++i) {
        if (i < filled) {
            bar += BAR_FULL;
        } else if (i == filled) {
            bar += BAR_HALF;
        } else {
            bar += BAR_EMPTY;
        }
    }
    return bar;
}

std::string ProgressBar::format_duration(uint64_t ms) const {
    if (ms < 1000) return std::to_string(ms) + "ms";
    std::ostringstream oss;
    uint64_t sec = ms / 1000;
    uint64_t msec = ms % 1000;
    if (sec < 60) {
        oss << sec << "." << std::setw(1) << (msec / 100) << "s";
    } else {
        oss << (sec / 60) << "m" << (sec % 60) << "s";
    }
    return oss.str();
}

void ProgressBar::clear_line() {
    if (tty_) std::cout << "\r\033[2K";
}

void ProgressBar::draw(int current, int total, const std::string& substatus) {
    if (!tty_) return;

    double pct = (total > 0) ? (static_cast<double>(current) / total) : 0.0;
    int    filled = static_cast<int>(pct * bar_width_);

    /* Globalny % (uwzględnia numer etapu) */
    double global_pct = ((current_stage_ - 1.0) + pct) / stages_ * 100.0;

    std::ostringstream line;
    line << "\r"
         << ansi::DIM << "║  " << ansi::RESET
         << ansi::GREEN  << build_bar(filled, bar_width_) << ansi::RESET
         << " "
         << ansi::BOLD << ansi::WHITE
         << std::setw(3) << static_cast<int>(global_pct) << "%"
         << ansi::RESET;

    if (!substatus.empty()) {
        /* Przycinamy do rozsądnej długości, żeby nie zawijać linii */
        std::string sub = substatus;
        if (sub.size() > 28) sub = sub.substr(0, 25) + "...";
        line << "  " << ansi::DIM << sub << ansi::RESET;
    }

    std::cout << line.str();
    std::cout.flush();
}

void ProgressBar::tick(int current, int total, const std::string& substatus) {
    if (tty_) {
        draw(current, total, substatus);
    } else {
        /* Nie-TTY: pisz co 10% lub na każdym pakiecie (gdy total < 10) */
        if (total < 10 || (total > 0 && current % (total / 10 + 1) == 0)) {
            int pct = (total > 0) ? (current * 100 / total) : 0;
            std::cout << "  [" << std::setw(3) << pct << "%] ";
            if (!substatus.empty()) std::cout << substatus;
            std::cout << "\n";
        }
    }
}

void ProgressBar::end_stage(const std::string& summary) {
    uint64_t elapsed = ms_now() - stage_start_ms_;

    if (tty_) {
        /* Usuń pasek, zastąp znacznikiem ✓ */
        clear_line();
        std::cout
            << "\r" << ansi::DIM << "║  " << ansi::RESET
            << ansi::BGREEN << "✓ " << ansi::RESET
            << ansi::BOLD << stage_label_ << ansi::RESET
            << ansi::DIM << "  " << format_duration(elapsed) << ansi::RESET;
        if (!summary.empty()) std::cout << "  " << summary;
        std::cout << "\n";
    } else {
        std::cout << "  ✓ " << stage_label_
                  << " (" << format_duration(elapsed) << ")";
        if (!summary.empty()) std::cout << " -- " << summary;
        std::cout << "\n";
    }
    std::cout.flush();
}

void ProgressBar::finish(const std::string& message) {
    if (tty_) {
        std::cout
            << ansi::BOLD << ansi::BGREEN
            << "╚═ ✓ " << message
            << ansi::RESET << "\n";
    } else {
        std::cout << "[OK] " << message << "\n";
    }
    std::cout.flush();
}

void ProgressBar::fail(const std::string& message) {
    if (tty_) {
        clear_line();
        std::cout
            << ansi::BOLD << "\033[1;31m"
            << "╚═ ✗ " << message
            << ansi::RESET << "\n";
    } else {
        std::cout << "[FAIL] " << message << "\n";
    }
    std::cout.flush();
}

void ProgressBar::spin(const std::string& substatus) {
    if (!tty_) return;

    const char* frame = SPINNER_FRAMES[spin_idx_ % SPINNER_COUNT];
    ++spin_idx_;

    std::ostringstream line;
    line << "\r"
         << ansi::DIM << "║  " << ansi::RESET
         << ansi::CYAN << frame << ansi::RESET
         << "  ";

    if (!substatus.empty()) {
        std::string sub = substatus;
        if (sub.size() > 50) sub = sub.substr(0, 47) + "...";
        line << ansi::DIM << sub << ansi::RESET;
    }
    line << "         "; /* padding by nadpisać poprzednią linię */

    std::cout << line.str();
    std::cout.flush();
}

/* ── ScopedSpinner ── */

ScopedSpinner::ScopedSpinner(std::string label)
    : bar_(label, 1)
{
    bar_.begin_stage(label);
}

ScopedSpinner::~ScopedSpinner() {
    if (!finished_) {
        bar_.end_stage();
        bar_.finish();
    }
}

void ScopedSpinner::update(const std::string& substatus) {
    bar_.spin(substatus);
}

void ScopedSpinner::done(const std::string& msg) {
    finished_ = true;
    bar_.end_stage(msg);
    bar_.finish(msg.empty() ? "Gotowe" : msg);
}

void ScopedSpinner::fail(const std::string& msg) {
    finished_ = true;
    bar_.end_stage("BŁĄD");
    bar_.fail(msg);
}

} // namespace debostree::progress
