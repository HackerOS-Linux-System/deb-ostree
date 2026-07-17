_deb_ostree() {
    local state

    _arguments -C \
        '(-v --verbose)'{-v,--verbose}'[Wlacz logi DEBUG]' \
        '(-c --config)'{-c,--config}'[Plik konfiguracyjny]:plik:_files -g "*.hk"' \
        '--arch[Architektura]:arch:(amd64 arm64 armhf i386 riscv64)' \
        '--dry-run[Symuluj bez zmian]' \
        '(-V --version)'{-V,--version}'[Wyswietl wersje]' \
        '(-h --help)'{-h,--help}'[Wyswietl pomoc]' \
        '1:komenda:->commands' \
        '*:argumenty:->args'

    case ${state} in
        commands)
            local cmds=(
                'status:Wyswietl deploymenty'
                'install:Zainstaluj pakiet warstwowy'
                'uninstall:Usun pakiet warstwowy'
                'remove:Alias dla uninstall'
                'update:Odswiez indeksy apt'
                'upgrade:Aktualizuj obraz bazowy i warstwy'
                'rollback:Wroc do poprzedniego deploymentu'
                'rebase:Przelacz na inny obraz OCI'
                'deploy:Inicjalny deployment z obrazu OCI'
                'cleanup:Usun stare deploymenty'
                'pin:Przypnij deployment'
                'search:Szukaj pakietow'
                'list:Wyswietl zainstalowane pakiety'
                'autoremove:Usun osierocon e zaleznosci'
                'initramfs:Zarzadzaj initramfs'
            )
            _describe 'komenda' cmds ;;
        args)
            case ${words[2]} in
                install)
                    # Uzupelnianie z cache indeksow
                    local cache_dir="${DEB_OSTREE_LISTS:-/var/lib/deb-ostree/apt-cache}"
                    if [[ -d "${cache_dir}" ]]; then
                        local pkgs=( $(grep -h "^Package:" ${cache_dir}/*_Packages 2>/dev/null | awk '{print $2}' | sort -u) )
                        _values 'pakiet' ${pkgs[@]}
                    fi ;;
                uninstall|remove)
                    # Uzupelnianie z zainstalowanych
                    local db="/var/lib/deb-ostree/status.jsonl"
                    if [[ -f "${db}" ]]; then
                        local pkgs=( $(grep -o '"name":"[^"]*"' "${db}" | cut -d'"' -f4 | sort -u) )
                        _values 'pakiet' ${pkgs[@]}
                    fi ;;
                list)
                    _arguments \
                        '--deployments[Wyswietl deploymenty]' \
                        '--files[Wyswietl pliki pakietu]' \
                        '--upgradeable[Pokaz dostepne aktualizacje]' ;;
                cleanup)
                    _arguments '--keep[Liczba deploymentow do zachowania]:N:(1 2 3 4 5)' \
                               '--cache[Wyczysc cache indeksow]' ;;
                pin)
                    _arguments \
                        '--unpin[Odpin deployment]' \
                        '--list[Wyswietl przypniete]' ;;
                search)
                    _arguments '--exact[Szukaj dokladnej nazwy]' ;;
            esac ;;
    esac
}

_deb_ostree
