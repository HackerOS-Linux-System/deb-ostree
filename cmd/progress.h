#pragma once
/*
 * deb-ostree -- progress.h
 * Piękny, kolorowy pasek postępu ANSI z obsługą spinnerów, etapów
 * i procentowego wypełnienia -- zaprojektowany dla operacji pakietowych.
 *
 * Cechy:
 *   - Automatyczne wyłączanie kolorów/animacji gdy stdout nie jest TTY
 *   - Tryb "spinner" (nieokreślony czas) i "bar" (znany postęp %)
 *   - Etykiety etapów i podstatus (np. "Pobieranie vim 2/8")
 *   - Zero zależności poza <iostream> i <unistd.h>
 *   - Bezpieczny do użycia współbieżnie z log::info/warn (czeka na nową linię)
 *
 * Wersja: 0.1.0
 */

#include <string>
#include <cstdint>
#include <vector>

namespace debostree::progress {

/* Kolory ANSI */
namespace ansi {
    static constexpr const char* RESET   = "\033[0m";
    static constexpr const char* BOLD    = "\033[1m";
    static constexpr const char* DIM     = "\033[2m";
    static constexpr const char* GREEN   = "\033[32m";
    static constexpr const char* CYAN    = "\033[36m";
    static constexpr const char* YELLOW  = "\033[33m";
    static constexpr const char* BLUE    = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";
    static constexpr const char* WHITE   = "\033[97m";
    static constexpr const char* BGREEN  = "\033[1;32m";
    static constexpr const char* BCYAN   = "\033[1;36m";
}

/*
 * ProgressBar -- wieloetapowy pasek postępu dla operacji pakietowych.
 *
 * Przykładowe użycie:
 *
 *   ProgressBar bar("Instalacja vim", 3);   // 3 etapy
 *   bar.begin_stage("Pobieranie indeksów");
 *   bar.tick(0, 2, "bookworm-main");
 *   bar.tick(1, 2, "bookworm-contrib");
 *   bar.tick(2, 2);
 *   bar.end_stage();
 *
 *   bar.begin_stage("Pobieranie pakietów");
 *   for (int i = 0; i < total; ++i) {
 *       bar.tick(i, total, pkgs[i].name);
 *   }
 *   bar.end_stage();
 *
 *   bar.finish("Zakończono pomyślnie");
 */
class ProgressBar {
public:
    /* title     -- główny tytuł operacji (np. "Instalacja pakietów")
     * stages    -- liczba etapów (używane do obliczania % globalnego)
     * bar_width -- szerokość paska w znakach (domyślnie 40) */
    explicit ProgressBar(std::string title, int stages = 1, int bar_width = 38);
    ~ProgressBar();

    ProgressBar(const ProgressBar&) = delete;
    ProgressBar& operator=(const ProgressBar&) = delete;

    /* Rozpoczyna nowy etap z etykietą (np. "Rozwiązywanie zależności"). */
    void begin_stage(const std::string& label);

    /* Aktualizuje pasek postępu w bieżącym etapie.
     * current  -- bieżący krok (0-based)
     * total    -- łączna liczba kroków w etapie
     * substatus -- opcjonalny opis bieżącej pozycji (np. nazwa pakietu) */
    void tick(int current, int total, const std::string& substatus = "");

    /* Kończy etap i wypisuje podsumowanie (czas trwania). */
    void end_stage(const std::string& summary = "");

    /* Kończy całą operację -- rysuje zieloną linię sukcesu. */
    void finish(const std::string& message = "Gotowe");

    /* Kończy z błędem -- rysuje czerwoną linię błędu. */
    void fail(const std::string& message);

    /* Spinner -- do użycia gdy całkowity postęp nie jest znany.
     * Wywołuj w pętli; spinner animuje się przy każdym wywołaniu. */
    void spin(const std::string& substatus = "");

    /* Zwraca true jeśli stdout jest TTY (animacje aktywne). */
    bool is_tty() const { return tty_; }

private:
    std::string title_;
    int         stages_;
    int         bar_width_;
    bool        tty_;

    int         current_stage_  = 0;
    std::string stage_label_;
    uint64_t    stage_start_ms_ = 0;
    int         spin_idx_       = 0;

    /* Wewnętrzne rysowanie -- nadpisuje bieżącą linię terminala (\r). */
    void draw(int current, int total, const std::string& substatus);
    void clear_line();
    uint64_t now_ms() const;
    std::string format_duration(uint64_t ms) const;
    std::string build_bar(int filled, int width) const;
};

/*
 * ScopedSpinner -- RAII wrapper do prostych operacji z nieokreślonym czasem.
 *
 *   {
 *       ScopedSpinner sp("Pobieranie indeksu mirror...");
 *       fetcher.fetch(...);
 *   }  // automatycznie kończy spinner po wyjściu ze scope
 */
class ScopedSpinner {
public:
    explicit ScopedSpinner(std::string label);
    ~ScopedSpinner();

    ScopedSpinner(const ScopedSpinner&) = delete;
    ScopedSpinner& operator=(const ScopedSpinner&) = delete;

    void update(const std::string& substatus);
    void done(const std::string& msg = "");
    void fail(const std::string& msg);

private:
    ProgressBar bar_;
    bool        finished_ = false;
};

} // namespace debostree::progress
