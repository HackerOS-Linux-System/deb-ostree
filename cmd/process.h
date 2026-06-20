#pragma once
/*
 * deb-ostree -- process.h
 * Wrapper do uruchamiania zewnetrznych procesow przez fork/exec (nie /bin/sh).
 * Uzywany do: mount, umount, apt-get, dpkg, skopeo, podman, tar, grub-mkconfig.
 *
 * Brak interpretacji powloki = brak ryzyka injection przy zmiennych zawierajacych
 * spacje lub znaki specjalne (nazwy pakietow, sciezki do workdir itd.).
 *
 * Wersja: 0.0.1
 */

#include <string>
#include <vector>

namespace debostree::process {

struct Result {
    int         exit_code   = -1;
    std::string stdout_data;
    std::string stderr_data;

    bool ok() const { return exit_code == 0; }
};

/*
 * Uruchamia argv[0] z argv[1..] synchronicznie. Przechwytuje stdout i stderr
 * do osobnych buforow (lub scala gdy merge_stderr_into_stdout=true).
 * Zmienne srodowiskowe dziedziczone z procesu rodzica (deb-ostree).
 */
Result run(const std::vector<std::string>& argv,
           const std::string& cwd = "",
           bool merge_stderr_into_stdout = false);

/*
 * Jak run(), ale rzuca std::runtime_error jesli exit_code != 0.
 * Uzyc tam gdzie niepowodzenie jest nieodwracalne i chcemy natychmiastowo
 * przerwac transakcje (np. mount overlayfs, finalizacja sysroot).
 */
Result run_or_throw(const std::vector<std::string>& argv,
                    const std::string& cwd = "");

} // namespace debostree::process
