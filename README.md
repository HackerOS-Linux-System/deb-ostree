# deb-ostree

**Wersja: 0.0.1** (pierwsza wersja do wydania na GitHubie po skompilowaniu na docelowym systemie)

`rpm-ostree`, ale dla systemów opartych na Debianie. Narzędzie do zarządzania
systemami w modelu **image-based / immutable**, kompatybilne koncepcyjnie z
`bootc` (obraz bazowy jako OCI image) i z ekosystemem `.deb`/`apt` jako
warstwą pakietów nakładaną na ten obraz.

## Dlaczego to istnieje

Fedora/RHEL ma `bootc` + `rpm-ostree`: obraz systemu trzymany jako commity
OSTree, deploymenty z atomowym rollbackiem, możliwość nakładania pakietów RPM
jako warstwy na obraz bazowy bez przebudowywania całego obrazu.

W świecie Debiana nie istnieje odpowiednik `rpm-ostree`. `deb-ostree` wypełnia
tę dziurę: ta sama architektura (libostree + OCI + warstwy pakietów), ale
pakiety to `.deb` instalowane przez prawdziwy `apt`/`dpkg`, nie reimplementacja
resolvera zależności.

## Struktura projektu

```
deb-ostree/
├── CMakeLists.txt          ← konfiguracja budowania (głowna)
├── README.md               ← ten plik
├── cmd/                    ← WSZYSTKIE pliki nagłówkowe (.h)
│   ├── types.h              -- wspólne typy domenowe (Deployment, Config...)
│   ├── logging.h            -- logger poziomowy
│   ├── process.h            -- subprocess wrapper (fork/exec)
│   ├── ostree_repo.h        -- wrapper C++ na libostree (OstreeRepo*)
│   ├── sysroot.h            -- wrapper na OstreeSysroot* (deploymenty)
│   ├── overlay_manager.h    -- montowanie overlayfs
│   ├── deb_layer.h          -- instalacja .deb przez apt/dpkg
│   ├── oci_puller.h         -- ściąganie obrazów OCI (skopeo+podman)
│   ├── state_store.h        -- parser konfiguracji
│   └── commands.h           -- deklaracje podkomend CLI
├── src/                    ← WSZYSTKIE pliki źródłowe (.cpp)
│   ├── main.cpp              -- punkt wejścia, dispatcher CLI
│   ├── logging.cpp
│   ├── process.cpp
│   ├── state_store.cpp
│   ├── ostree_repo.cpp
│   ├── sysroot.cpp
│   ├── overlay_manager.cpp
│   ├── deb_layer.cpp
│   ├── oci_puller.cpp
│   ├── cmd_status.cpp        -- "deb-ostree status"
│   ├── cmd_install.cpp       -- "deb-ostree install"
│   ├── cmd_uninstall.cpp     -- "deb-ostree uninstall"
│   ├── cmd_upgrade.cpp       -- "deb-ostree upgrade"
│   ├── cmd_rollback.cpp      -- "deb-ostree rollback"
│   ├── cmd_rebase.cpp        -- "deb-ostree rebase"
│   ├── cmd_deploy.cpp        -- "deb-ostree deploy"
│   ├── cmd_cleanup.cpp       -- "deb-ostree cleanup"
│   ├── cmd_initramfs.cpp     -- "deb-ostree initramfs"
│   └── hk_parser.cpp         -- parser formatu konfiguracji .hk
├── tests/
│   ├── CMakeLists.txt
│   └── test_config_parser.cpp
└── config/
    └── deb-ostree.hk.example
```

Każdy plik `src/cmd_*.cpp` odpowiada jednej podkomendzie CLI i włącza headery
z `cmd/` przez ścieżkę relatywną `../cmd/*.h`.

## Architektura

```
                    ┌─────────────────────┐
                    │   OCI Registry        │  (obraz bazowy, jak bootc)
                    └──────────┬────────────┘
                               │ skopeo + podman (OciPuller)
                               ▼
                    ┌─────────────────────┐
                    │  rootfs (rozpakowany) │
                    └──────────┬────────────┘
                               │ ostree_repo_write_directory_to_mtree
                               ▼
                    ┌─────────────────────┐
                    │  Repo OSTree          │  (ostree::Repo, cmd/ostree_repo.h)
                    │  /ostree/repo         │  -- niemutowalne commity
                    └──────────┬────────────┘
                               │ checkout (read-only)
                               ▼
              ┌────────────────────────────────┐
              │  OverlayManager                   │
              │  lower = checkout OSTree (RO)     │
              │  upper = scratch (tu apt pisze)    │
              │  merged = to co widzi dpkg/chroot  │
              └────────────────┬───────────────────┘
                               │ apt-get install --root=merged (DebLayer)
                               ▼
                    ┌─────────────────────┐
                    │  merged (lower+upper)  │ → kopiowane na płasko
                    └──────────┬────────────┘
                               │ commit_directory(...)
                               ▼
                    ┌─────────────────────┐
                    │  NOWY commit OSTree    │
                    └──────────┬────────────┘
                               │ Sysroot::deploy_commit(...)
                               ▼
                    ┌─────────────────────┐
                    │  Nowy deployment       │  (wpis w bootloaderze,
                    │  (staged, czeka na     │   stary zostaje jako
                    │   reboot)               │   rollback)
                    └─────────────────────┘
```

### Kluczowa zasada: nigdy nie modyfikujemy aktywnego systemu w miejscu

Każda operacja (`install`, `upgrade`, `rebase`...) tworzy **nowy commit OSTree
i nowy deployment**. Aktualnie działający system nie jest dotykany — zmiany
wchodzą w życie po `reboot`, dokładnie tak jak w `rpm-ostree`/`bootc`. Dzięki
temu `rollback` jest natychmiastowy i zawsze bezpieczny (oba stany istnieją
nienaruszone na dysku).

## Format konfiguracji: .hk

`deb-ostree` używa formatu `.hk` (wspólny dla całego ekosystemu HackerOS —
patrz [dokumentacja formatu](https://hackeros-linux-system.github.io/HackerOS-Website/tools-docs/hk.html))
zamiast klasycznego `.conf`. Plik konfiguracyjny to `/etc/deb-ostree/deb-ostree.hk`:

```
[sysroot]
-> path => /

[ostree]
-> repo_path => /ostree/repo

[system]
-> osname => debian

[overlay]
-> work_dir => /var/lib/deb-ostree/overlay-work

[apt]
-> lists_path => /var/lib/deb-ostree/apt-cache

[origin]
-> refspec => deb-ostree-oci:ghcr.io/przyklad/obraz:trixie
```

Parser (`cmd/hk_parser.h`, `src/hk_parser.cpp`) implementuje **podzbiór**
pełnej specyfikacji `.hk` wystarczający dla configu deb-ostree: sekcje,
klucze poziomu 1 (`->`), komentarze (`!`), stringi cytowane i plain. Nie
wspiera interpolacji `${...}`, kluczy kropkowych ani głębszego zagnieżdżenia
— jeśli configi deb-ostree w przyszłości potrzebują tych funkcji, pełna
implementacja referencyjna parsera `.hk` znajduje się w
[`hackeros-builder/internal/hk`](https://github.com/HackerOS-Linux-System/hackeros-builder)
(Go) i może być portem-wzorcem.

**Ten plik jest generowany automatycznie** przez `hackeros-builder` podczas
budowy obrazu (`hackeros-builder build cloud` / `build iso`) — patrz pakiet
`internal/hkgen` w tamtym repozytorium. Możesz go też edytować ręcznie na
żywym systemie.

## Wymagania budowania

```bash
sudo apt install build-essential cmake pkg-config \
    libostree-dev libglib2.0-dev \
    skopeo podman
```

## Budowanie

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

Binarka trafia do `build/deb-ostree`, a po `make install` do `/usr/local/bin/deb-ostree`.

### Testy

```bash
cd build
ctest --output-on-failure
```

Obecny zestaw testów (`tests/test_config_parser.cpp`) sprawdza tylko parser
konfiguracji — nie wymaga roota. Testy integracyjne dla `install`/`rollback`
wymagają środowiska z `CAP_SYS_ADMIN` (patrz sekcja "Co trzeba dopracować").

## Użycie

```bash
# Pierwszy, inicjalny deployment z obrazu OCI (bootstrap, na maszynie docelowej
# lub w installerze)
sudo deb-ostree deploy registry.example.com/debian-bootc:bookworm

# Status deploymentów (jak `rpm-ostree status`)
deb-ostree status

# Instalacja pakietu jako warstwa
sudo deb-ostree install neovim htop

# Usunięcie pakietu warstwowego
sudo deb-ostree uninstall htop

# Aktualizacja obrazu bazowego + ponowne nałożenie pakietów warstwowych
sudo deb-ostree upgrade

# Powrót do poprzedniego deploymentu
sudo deb-ostree rollback

# Przełączenie na inny obraz bazowy
sudo deb-ostree rebase registry.example.com/debian-bootc:trixie

# Czyszczenie starych deploymentów (domyślnie zachowuje 2 ostatnie)
sudo deb-ostree cleanup --keep 2
```

---

## Co trzeba dopracować, żeby to było narzędzie produkcyjne

To jest **działający szkielet architektury** zweryfikowany pod kątem
poprawności składniowej C++ (`-fsyntax-only -Wall -Wextra -Wpedantic` na
każdym pliku, plus realna kompilacja+uruchomienie testu konfiguracji). Nie
jest jeszcze skompilowany i przetestowany end-to-end na realnym systemie z
`libostree-dev` — zrób to jako pierwszy krok po pobraniu repo.

### Krytyczne przed pierwszym użyciem produkcyjnym

1. **Bootloader (GRUB / systemd-boot)**
   `Sysroot::deploy_commit` rejestruje deployment w OSTree (BLS entries,
   `/boot/loader/entries`), ale **nie generuje** `grub.cfg`. Trzeba dowiązać
   wywołanie `grub-mkconfig` (lub odpowiednika dla systemd-boot) po każdym
   `deploy_commit`, inaczej nowy wpis nie pojawi się w menu bootowania na
   systemach z GRUB jako pierwszym stage'em.

2. **Testy integracyjne na realnym systemie**
   Obecny zestaw testów nie obejmuje `install`/`upgrade`/`rollback` (wymagają
   roota + `mount`/`overlayfs`). Potrzebne: środowisko CI z kontenerem
   privileged lub VM, scenariusze: instalacja pakietu z zależnościami,
   upgrade z re-layeringiem, rollback po nieudanej instalacji, cleanup.

3. **Obsługa błędów w trakcie transakcji (atomicity)**
   Jeśli proces `deb-ostree` zostanie przerwany (kill -9, awaria zasilania)
   w trakcie `commit_directory` lub `deploy_commit`, repo OSTree ma wbudowaną
   ochronę transakcyjną, ale stan `overlay_work_dir` (upper/work/merged) może
   zostać w niespójnym stanie. Trzeba dodać wykrywanie i czyszczenie "osierocone
   sesje" przy starcie (`deb-ostree status` lub dedykowana komenda `--repair`).

4. **Walidacja podpisów / bezpieczeństwo obrazów OCI**
   `OciPuller` obecnie nie weryfikuje podpisów obrazu (np. `cosign`,
   `sigstore`, GPG). Dla produkcyjnego użycia w dystrybucji Linuxa to jest
   wymagane — `skopeo copy` wspiera `--policy` z `policy.json` (containers/image
   signature verification policy).

### Ważne, ale nie blokujące pierwszego wydania

5. **3-way merge `/etc` — testy edge case'ów**
   libostree robi to natywnie, ale warto przetestować scenariusze konfliktów
   (np. użytkownik edytował `/etc/hosts` lokalnie, nowy obraz też go zmienia)
   żeby wiedzieć czego się spodziewać.

6. **Automatyczna regeneracja initramfs**
   `cmd_initramfs` jest punktem rozszerzenia. Trzeba dodać wykrywanie "ten
   pakiet dotyczy jądra/modułów" (np. `linux-image-*`, cokolwiek instaluje
   pliki w `/lib/modules/`) i automatyczne wywołanie `update-initramfs -u`
   wewnątrz `merged_dir` PRZED commitem, żeby initramfs odpowiadał
   zainstalowanym modułom.

7. **Lepsze parsowanie wyjścia `apt-get --simulate`**
   `DebLayer::install_packages` parsuje linie `Inst ...` w sposób uproszczony.
   Warto przejść na `apt-get install --print-uris` z `python3-apt` lub bindingi
   `libapt-pkg` dla solidniejszego, formalnego parsowania (różne wersje apt
   mogą zmienić format `--simulate`).

8. **Pełna zgodność z whiteouts OCI**
   `checkout_commit` ustawia `process_whiteouts = TRUE`, ale warto przetestować
   `upgrade`/`rebase` między obrazami z różną strukturą warstw (opaque
   whiteouts, `.wh..wh..opq`) na realnych obrazach bootc-style.

9. **Blokada równoległych transakcji**
   Obecnie nic nie chroni przed dwoma równoczesnymi `deb-ostree install`
   (dwa procesy współdzielące `overlay_work_dir`). Potrzebny lockfile
   (np. `flock` na `/var/lib/deb-ostree/.lock`) na początku każdej komendy
   modyfikującej.

10. **Konfigurowalne źródła apt per-operacja**
    `apt_sources` w konfiguracji jest zdefiniowane w `types.h`, ale `DebLayer`
    nie zapisuje ich jeszcze do `merged_dir/etc/apt/sources.list` przed
    `refresh_package_index`. To trzeba domknąć, żeby `install`/`upgrade`
    używały repozytoriów z konfiguracji, nie tych "dziedziczonych" z obrazu
    bazowego.

### Estetyka / UX (niski priorytet, ale ważne dla odbioru)

11. **Ładny progress indicator (spinner/progress bar)**
    Obecnie operacje długotrwałe (`skopeo copy`, `apt-get install`, `tar -xpf`)
    pokazują tylko statyczne logi `[INFO] ...`. `rpm-ostree` (i `bootc`) mają
    czytelny, animowany spinner z procentami podczas pull obrazu i
    aktualizacji. **→ Przeniesione do ROADMAP poniżej.**

---

## ROADMAP

Pomysły na przyszłe wersje, w przybliżonym porządku priorytetu:

- [ ] **v0.1.0** — Generowanie konfiguracji bootloadera (GRUB/systemd-boot)
      automatycznie po `deploy_commit`.
- [ ] **v0.1.0** — Testy integracyjne w kontenerze privileged (GitHub Actions
      self-hosted runner lub VM z KVM) dla `install`/`upgrade`/`rollback`.
- [ ] **v0.2.0** — **Ładny, animowany progress indicator** (spinner + progress
      bar) dla operacji długotrwałych: `skopeo copy` (pull warstw OCI z
      procentami), `apt-get install` (postęp pobierania/rozpakowywania
      pakietów), `tar -xpf` (rozpakowywanie rootfs). Wzorować się na stylu
      `rpm-ostree`/`bootc` (biblioteki typu `indicatif` w Rust mają dobry UX —
      w C++ można rozważyć własną implementację na bazie ANSI escape codes,
      albo lekką bibliotekę typu `indicators`).
- [ ] **v0.2.0** — Lockfile chroniący przed równoległymi transakcjami.
- [ ] **v0.2.0** — Weryfikacja podpisów obrazów OCI (`cosign`/`sigstore`,
      `containers-policy.json`).
- [ ] **v0.3.0** — Automatyczna regeneracja initramfs przy wykryciu zmian
      w modułach jądra.
- [ ] **v0.3.0** — Zapisywanie `apt_sources` z konfiguracji do
      `sources.list` w overlay przed `refresh_package_index`.
- [ ] **v0.3.0** — Pełne wsparcie formatu `.hk` w parserze (interpolacja
      `${...}`, klucze kropkowe, głębsze zagnieżdżenie) — port logiki z
      `hackeros-builder/internal/hk` (Go) do `cmd/hk_parser.h` (C++),
      przydatne gdy config deb-ostree zacznie potrzebować np.
      `${env:NAZWA}` do wstrzykiwania sekretów bez trzymania ich w pliku.
- [ ] **v0.4.0** — Komenda `--repair` do czyszczenia osieroconych sesji
      overlay po przerwanej transakcji.
- [ ] **v0.4.0** — Przejście na `libapt-pkg` bindings (lub `python3-apt`
      przez subprocess z `--print-uris`) zamiast parsowania `--simulate`.
- [ ] **v0.5.0** — Wsparcie dla `override` (analog `rpm-ostree override
      replace/remove`) — podmiana pakietu z obrazu bazowego bez czekania na
      upstream upgrade.
- [ ] **v1.0.0** — Stabilne API CLI, pełna dokumentacja man page,
      paczka `.deb` dla samego `deb-ostree` (meta!).

## Licencja

GPL-3.0
