complete -c deb-ostree -e

# Opcje globalne
complete -c deb-ostree -s v -l verbose     -d "Wlacz logi DEBUG"
complete -c deb-ostree -s c -l config      -d "Plik konfiguracyjny" -r -F
complete -c deb-ostree -l arch             -d "Architektura" -r -a "amd64 arm64 armhf i386"
complete -c deb-ostree -l dry-run          -d "Symuluj bez zmian"
complete -c deb-ostree -s V -l version     -d "Wyswietl wersje"
complete -c deb-ostree -s h -l help        -d "Wyswietl pomoc"

# Komendy (tylko gdy nie podano jeszcze komendy)
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "status"     -d "Wyswietl deploymenty"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "install"    -d "Zainstaluj pakiet warstwowy"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "uninstall"  -d "Usun pakiet warstwowy"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "remove"     -d "Alias dla uninstall"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "update"     -d "Odswiez indeksy apt"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "upgrade"    -d "Aktualizuj obraz bazowy + warstwy"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "rollback"   -d "Wroc do poprzedniego deploymentu"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "rebase"     -d "Przelacz na inny obraz OCI"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "deploy"     -d "Inicjalny deployment z obrazu OCI"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "cleanup"    -d "Usun stare deploymenty"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "pin"        -d "Przypnij deployment"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "search"     -d "Szukaj pakietow"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "list"       -d "Wyswietl zainstalowane"
complete -c deb-ostree -f -n "__fish_use_subcommand" \
    -a "autoremove" -d "Usun osierocon e zaleznosci"

# Argumenty podkomend
complete -c deb-ostree -f -n "__fish_seen_subcommand_from list" \
    -a "--deployments" -d "Wyswietl deploymenty OSTree"
complete -c deb-ostree -f -n "__fish_seen_subcommand_from list" \
    -a "--upgradeable" -d "Pokaz dostepne aktualizacje"
complete -c deb-ostree -f -n "__fish_seen_subcommand_from list" \
    -a "--files"       -d "Wyswietl pliki pakietu"

complete -c deb-ostree -f -n "__fish_seen_subcommand_from cleanup" \
    -l keep -d "Liczba deploymentow do zachowania" -r
complete -c deb-ostree -f -n "__fish_seen_subcommand_from cleanup" \
    -l cache -d "Wyczysc cache indeksow"

complete -c deb-ostree -f -n "__fish_seen_subcommand_from pin" \
    -l unpin -d "Odpin deployment"
complete -c deb-ostree -f -n "__fish_seen_subcommand_from pin" \
    -l list  -d "Wyswietl przypniete deploymenty"

complete -c deb-ostree -f -n "__fish_seen_subcommand_from search" \
    -l exact -d "Szukaj dokladnej nazwy pakietu"

# Uzupelnianie pakietow dla install z cache indeksow
function __deb_ostree_packages
    set cache_dir /var/lib/deb-ostree/apt-cache
    if test -d $cache_dir
        grep -h "^Package:" $cache_dir/*_Packages 2>/dev/null | awk '{print $2}' | sort -u
    end
end

complete -c deb-ostree -n "__fish_seen_subcommand_from install" \
    -a "(__deb_ostree_packages)"

# Uzupelnianie zainstalowanych pakietow dla uninstall/remove
function __deb_ostree_installed
    set db /var/lib/deb-ostree/status.jsonl
    if test -f $db
        grep -o '"name":"[^"]*"' $db | cut -d'"' -f4 | sort -u
    end
end

complete -c deb-ostree -n "__fish_seen_subcommand_from uninstall remove" \
    -a "(__deb_ostree_installed)"
