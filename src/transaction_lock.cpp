#include "../cmd/transaction_lock.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace fs = std::filesystem;

namespace debostree {

TransactionLock::TransactionLock(const std::string& lock_dir)
    : lock_path_(lock_dir + "/lock")
    , incomplete_path_(lock_dir + "/transaction.incomplete")
{
    fs::create_directories(lock_dir);

    /* Sprawdź plik .incomplete -- sygnał przerwanej transakcji */
    if (fs::exists(incomplete_path_)) {
        found_incomplete_ = true;
        log::warn("Wykryto przerwana transakcje (plik: " + incomplete_path_ + ").\n"
                  "Uruchom 'deb-ostree cleanup' aby przywrocic spójnosć systemu.");
    }

    lock_fd_ = ::open(lock_path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_fd_ < 0)
        throw std::runtime_error("TransactionLock: nie mozna otworzyc " + lock_path_ +
                                 ": " + std::strerror(errno));

    /* LOCK_EX | LOCK_NB -- wyłączna, nieblokująca */
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) < 0) {
        ::close(lock_fd_);
        lock_fd_ = -1;
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            throw std::runtime_error(
                "deb-ostree jest juz uruchomiony (lockfile: " + lock_path_ + ").\n"
                "Jezeli poprzednia operacja zostala zakonczona nieprawidlowo, usun: "
                + lock_path_);
        }
        throw std::runtime_error("flock(" + lock_path_ + "): " + std::strerror(errno));
    }

    /* Zapisz PID do lockfile (pomocne przy debugowaniu) */
    {
        std::ofstream f(lock_path_, std::ios::trunc);
        f << ::getpid() << "\n";
    }

    /* Oznacz transakcję jako niezakończoną */
    { std::ofstream f(incomplete_path_, std::ios::trunc); f << "pending\n"; }

    log::debug("TransactionLock: blokada uzyskana (pid=" + std::to_string(::getpid()) + ")");
}

TransactionLock::~TransactionLock() {
    if (lock_fd_ >= 0) {
        ::flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
        lock_fd_ = -1;
    }
    log::debug("TransactionLock: blokada zwolniona");
}

void TransactionLock::mark_complete() {
    std::error_code ec;
    fs::remove(incomplete_path_, ec);
    log::debug("TransactionLock: transakcja zakonczona pomyslnie");
}

} // namespace debostree
