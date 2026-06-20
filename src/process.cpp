#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <array>

namespace debostree::process {

namespace {

/* Wyczerpuje fd do konca (EOF) i zwraca zawartosc jako string. */
std::string drain_fd(int fd) {
    std::string out;
    std::array<char, 4096> buf{};
    ssize_t n;
    while ((n = ::read(fd, buf.data(), buf.size())) > 0)
        out.append(buf.data(), static_cast<size_t>(n));
    return out;
}

} // namespace

Result run(const std::vector<std::string>& argv,
           const std::string& cwd,
           bool merge_stderr_into_stdout)
{
    if (argv.empty()) return {-1, "", "pusty argv"};

    int out_pipe[2], err_pipe[2];
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0)
        return {-1, "", "pipe() nie powiodlo sie"};

    {
        std::ostringstream dbg;
        for (auto& a : argv) dbg << a << ' ';
        log::debug("exec: " + dbg.str());
    }

    pid_t pid = ::fork();
    if (pid < 0)
        return {-1, "", "fork() nie powiodlo sie"};

    if (pid == 0) {
        /* ---- potomek ---- */
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);

        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(merge_stderr_into_stdout ? out_pipe[1] : err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);

        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(127);

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);

        ::execvp(c_argv[0], c_argv.data());
        ::_exit(127); /* execvp wrocil = blad */
    }

    /* ---- rodzic ---- */
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    std::string out = drain_fd(out_pipe[0]);
    std::string err = merge_stderr_into_stdout ? "" : drain_fd(err_pipe[0]);

    ::close(out_pipe[0]);
    if (!merge_stderr_into_stdout) ::close(err_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return {.exit_code = code, .stdout_data = out, .stderr_data = err};
}

Result run_or_throw(const std::vector<std::string>& argv, const std::string& cwd) {
    Result r = run(argv, cwd);
    if (!r.ok()) {
        std::ostringstream msg;
        msg << "Komenda nie powiodla sie (exit " << r.exit_code << "): ";
        for (auto& a : argv) msg << a << ' ';
        msg << "\nstderr: " << r.stderr_data;
        throw std::runtime_error(msg.str());
    }
    return r;
}

} // namespace debostree::process
