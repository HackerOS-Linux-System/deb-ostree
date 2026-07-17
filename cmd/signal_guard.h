#pragma once
/*
 * deb-ostree -- signal_guard.h  [NOWY v0.2.0]
 * RAII handler sygnałów SIGINT/SIGTERM podczas transakcji.
 *
 * Problem (#4): Ctrl+C w połowie install/upgrade zwalnia TransactionLock
 * (destruktor), ale overlay może pozostać zamontowany, co blokuje umount
 * i zostawia system w niespójnym stanie.
 *
 * Rozwiązanie: SignalGuard rejestruje handler który:
 *   1. Ustawia flagę g_interrupted
 *   2. Odmontowuje aktywną sesję overlay (jeśli istnieje)
 *   3. Kontynuuje normalny stack unwinding przez re-raise sygnału
 *
 * Użycie:
 *   SignalGuard guard;
 *   guard.set_active_session(&ovl, &ses);  // po begin_session
 *   guard.clear_session();                  // po end/discard_session
 *
 * Wersja: 0.2.0
 */

#include <atomic>
#include <functional>

namespace debostree {

class OverlayManager;
struct OverlaySession;

class SignalGuard {
public:
    SignalGuard();
    ~SignalGuard();

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;

    /* Rejestruje aktywną sesję overlay do czyszczenia przy sygnale. */
    void set_active_session(OverlayManager* ovl, OverlaySession* ses);

    /* Czyści rejestrację sesji (po pomyślnym end_session / discard_session). */
    void clear_session();

    /* Zwraca true jeśli operacja powinna być przerwana (sygnał odebrany). */
    static bool interrupted();

private:
    static void handle(int sig);
};

} // namespace debostree
