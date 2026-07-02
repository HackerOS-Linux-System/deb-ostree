#pragma once
/*
 * deb-ostree -- transaction_lock.h  [NOWY v0.1.0]
 * Plik lockfile chroniący przed równoczesnym uruchomieniem dwóch operacji
 * modyfikujących sysroot (install/uninstall/upgrade/rebase/deploy).
 *
 * Mechanizm: flock(2) na /var/lib/deb-ostree/lock (LOCK_EX | LOCK_NB).
 * Przy starcie sprawdzamy też plik .incomplete -- jeśli istnieje, poprzednia
 * transakcja została przerwana i wymagany jest cleanup.
 *
 * Wersja: 0.1.0
 */

#include <string>

namespace debostree {

/* RAII lockfile -- blokuje przy konstruowaniu, zwalnia przy destrukcji. */
class TransactionLock {
public:
    /*
     * Próbuje uzyskać wyłączną blokadę na lock_path.
     * Rzuca std::runtime_error jeśli blokada zajęta (inna instancja działa)
     * lub jeśli wykryto plik .incomplete (przerwana poprzednia transakcja).
     *
     * lock_path: np. /var/lib/deb-ostree/lock
     */
    explicit TransactionLock(const std::string& lock_dir);
    ~TransactionLock();

    TransactionLock(const TransactionLock&) = delete;
    TransactionLock& operator=(const TransactionLock&) = delete;

    /*
     * Oznacza transakcję jako zakończoną (usuwa plik .incomplete).
     * Wywoływać PO pomyślnym zakończeniu wszystkich operacji.
     */
    void mark_complete();

    /*
     * Zwraca true jeśli wykryto plik .incomplete przy poprzednim starcie.
     * Caller powinien zaproponować użytkownikowi "deb-ostree cleanup".
     */
    bool found_incomplete() const { return found_incomplete_; }

private:
    std::string lock_path_;
    std::string incomplete_path_;
    int         lock_fd_ = -1;
    bool        found_incomplete_ = false;
};

} // namespace debostree
