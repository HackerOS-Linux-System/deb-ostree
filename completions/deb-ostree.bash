_deb_ostree_complete() {
    local cur prev words cword
    _init_completion || return

    local commands="status install uninstall remove update upgrade rollback
                    rebase deploy cleanup pin search list autoremove initramfs"
    local global_opts="-v --verbose -c --config --arch --dry-run -V --version -h --help"

    case "${prev}" in
        deb-ostree)
            COMPREPLY=( $(compgen -W "${commands} ${global_opts}" -- "${cur}") )
            return ;;
        install|uninstall|remove)
            # Instalacja: uzupelnij z cache indeksow jesli dostepny
            if [[ "${prev}" == "install" ]]; then
                local cache_dir="${DEB_OSTREE_LISTS:-/var/lib/deb-ostree/apt-cache}"
                if [[ -d "${cache_dir}" ]]; then
                    local pkgs
                    pkgs=$(grep -h "^Package:" "${cache_dir}"/*_Packages 2>/dev/null | \
                           awk '{print $2}' | sort -u)
                    COMPREPLY=( $(compgen -W "${pkgs}" -- "${cur}") )
                    return
                fi
            else
                # Uninstall: uzupelnij z zainstalowanych pakietow
                local status_file="/var/lib/deb-ostree/status.jsonl"
                if [[ -f "${status_file}" ]]; then
                    local pkgs
                    pkgs=$(grep -o '"name":"[^"]*"' "${status_file}" | \
                           cut -d'"' -f4 | sort -u)
                    COMPREPLY=( $(compgen -W "${pkgs}" -- "${cur}") )
                    return
                fi
            fi
            ;;
        cleanup)
            COMPREPLY=( $(compgen -W "--keep --cache" -- "${cur}") )
            return ;;
        --keep)
            COMPREPLY=( $(compgen -W "1 2 3 4 5" -- "${cur}") )
            return ;;
        pin)
            COMPREPLY=( $(compgen -W "--unpin --list" -- "${cur}") )
            return ;;
        list)
            COMPREPLY=( $(compgen -W "--deployments --files --upgradeable" -- "${cur}") )
            return ;;
        search)
            COMPREPLY=( $(compgen -W "--exact" -- "${cur}") )
            return ;;
        --arch)
            COMPREPLY=( $(compgen -W "amd64 arm64 armhf i386 riscv64" -- "${cur}") )
            return ;;
        --config|-c)
            _filedir '*.hk'
            return ;;
    esac

    # Uzupelnianie podkomend po globalnych flagach
    if [[ "${cur}" == -* ]]; then
        COMPREPLY=( $(compgen -W "${global_opts}" -- "${cur}") )
    else
        COMPREPLY=( $(compgen -W "${commands}" -- "${cur}") )
    fi
}

complete -F _deb_ostree_complete deb-ostree
