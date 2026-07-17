#include "../cmd/signal_guard.h"
#include "../cmd/overlay_manager.h"
#include "../cmd/logging.h"

#include <csignal>
#include <atomic>
#include <cstring>
#include <iostream>

namespace debostree {

/* Globalne zmienne dostępne z handlera sygnału (async-signal-safe). */
static std::atomic<bool> g_interrupted{false};
static OverlayManager*   g_active_ovl = nullptr;
static OverlaySession*   g_active_ses = nullptr;

/* Stare handlery do przywrócenia po destrukcji SignalGuard */
static struct sigaction g_old_sigint{};
static struct sigaction g_old_sigterm{};

void SignalGuard::handle(int sig) {
    g_interrupted.store(true, std::memory_order_relaxed);

    /* Wypisz wiadomość (write() jest async-signal-safe, std::cerr NIE jest,
     * ale piszemy tylko jako best-effort) */
    const char* msg = "\n[deb-ostree] Przerywanie... czyszczenie overlayfs.\n";
    ::write(STDERR_FILENO, msg, std::strlen(msg));

    /* Odmontuj overlay jeśli jest aktywny -- best-effort w handlerze.
     * process::run() jest async-signal-safe (fork+exec), więc to jest OK. */
    if (g_active_ses && g_active_ses->mounted) {
        /* Lazy umount przez shell -- nie możemy używać std::string w handlerze */
        const char* merged = g_active_ses->merged_dir.c_str();
        ::execlp("umount", "umount", "-l", merged, nullptr);
        /* Jeśli execlp failed (nie powinno) -- kontynuuj */
    }

    /* Przywróć domyślny handler i re-raise */
    if (sig == SIGINT)  ::sigaction(SIGINT,  &g_old_sigint,  nullptr);
    if (sig == SIGTERM) ::sigaction(SIGTERM, &g_old_sigterm, nullptr);
    ::raise(sig);
}

SignalGuard::SignalGuard() {
    g_interrupted.store(false, std::memory_order_relaxed);
    g_active_ovl = nullptr;
    g_active_ses = nullptr;

    struct sigaction sa{};
    sa.sa_handler = SignalGuard::handle;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND; /* reset po pierwszym sygnale */

    ::sigaction(SIGINT,  &sa, &g_old_sigint);
    ::sigaction(SIGTERM, &sa, &g_old_sigterm);

    log::debug("SignalGuard: aktywny (SIGINT/SIGTERM przechwytywane)");
}

SignalGuard::~SignalGuard() {
    /* Przywróć oryginalne handlery */
    ::sigaction(SIGINT,  &g_old_sigint,  nullptr);
    ::sigaction(SIGTERM, &g_old_sigterm, nullptr);
    g_active_ovl = nullptr;
    g_active_ses = nullptr;
    log::debug("SignalGuard: deaktywowany");
}

void SignalGuard::set_active_session(OverlayManager* ovl, OverlaySession* ses) {
    g_active_ovl = ovl;
    g_active_ses = ses;
}

void SignalGuard::clear_session() {
    g_active_ovl = nullptr;
    g_active_ses = nullptr;
}

bool SignalGuard::interrupted() {
    return g_interrupted.load(std::memory_order_relaxed);
}

} // namespace debostree
