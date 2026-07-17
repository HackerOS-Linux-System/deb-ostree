#include "../cmd/solv_pool.h"
#include "../cmd/logging.h"

#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solver.h>
/* solverdebug.h: optional, included only if present */
#if __has_include(<solv/solverdebug.h>)
#include <solv/solverdebug.h>
#endif
#include <solv/transaction.h>
#include <solv/policy.h>
#if __has_include(<solv/selection.h>)
#include <solv/selection.h>
#endif

#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace debostree::solv {

/* ── helpers ── */

namespace {

/* Trim whitespace */
static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/* Parsuje jeden token zależności Debiana: "pkg" lub "pkg (op ver)".
 * Zwraca Id libsolv (może być REL_AND, REL_OR itp.). */
::Id parse_dep_token(::Pool* pool, const std::string& token) {
    std::string t = trim(token);
    if (t.empty()) return 0;

    auto paren = t.find('(');
    std::string pkg_name = trim(paren != std::string::npos ? t.substr(0, paren) : t);
    if (pkg_name.empty()) return 0;

    ::Id name_id = pool_str2id(pool, pkg_name.c_str(), 1);

    if (paren == std::string::npos) return name_id;

    auto close = t.find(')', paren);
    std::string constraint = trim(t.substr(paren + 1,
        (close != std::string::npos ? close : t.size()) - paren - 1));

    int rel_flags = 0;
    std::string ver;

    if      (constraint.rfind(">=", 0) == 0) { rel_flags = REL_GT | REL_EQ; ver = constraint.substr(2); }
    else if (constraint.rfind("<=", 0) == 0) { rel_flags = REL_LT | REL_EQ; ver = constraint.substr(2); }
    else if (constraint.rfind("<<", 0) == 0) { rel_flags = REL_LT;          ver = constraint.substr(2); }
    else if (constraint.rfind(">>", 0) == 0) { rel_flags = REL_GT;          ver = constraint.substr(2); }
    else if (constraint.rfind("=",  0) == 0) { rel_flags = REL_EQ;          ver = constraint.substr(1); }

    ver = trim(ver);
    if (rel_flags == 0 || ver.empty()) return name_id;

    ::Id evr_id = pool_str2id(pool, ver.c_str(), 1);
    return pool_rel2id(pool, name_id, evr_id, rel_flags, 1);
}

/*
 * Parsuje string zależności Debiana na Id libsolv i dodaje do Solvable.
 *
 * Pełna obsługa:
 *   - przecinek (,) = AND = kolejne wywołania add_dep_id
 *   - pipe (|)      = OR  = pool_rel2id(REL_OR, A, B) -- prawidłowy OR libsolv
 *
 * Różnica od poprzedniej wersji: OR jest teraz prawdziwym REL_OR, nie
 * "pierwsza alternatywa". Libsolv SAT solver obsługuje REL_OR natywnie.
 */
void add_deps_from_string(::Pool* pool, ::Repo* repo, ::Solvable* s,
                          const std::string& dep_string, int keyname,
                          bool is_weak = false)
{
    if (dep_string.empty()) return;

    /* Rozbij po przecinku (AND) */
    std::istringstream and_stream(dep_string);
    std::string and_part;

    while (std::getline(and_stream, and_part, ',')) {
        and_part = trim(and_part);
        if (and_part.empty()) continue;

        /* Zbierz alternatywy OR */
        std::vector<std::string> or_parts;
        {
            std::istringstream or_stream(and_part);
            std::string or_part;
            while (std::getline(or_stream, or_part, '|'))
                or_parts.push_back(trim(or_part));
        }

        if (or_parts.empty()) continue;

        /* Parsuj każdą alternatywę */
        std::vector<::Id> alt_ids;
        for (auto& alt : or_parts) {
            ::Id id = parse_dep_token(pool, alt);
            if (id != 0) alt_ids.push_back(id);
        }
        if (alt_ids.empty()) continue;

        /* Zbuduj REL_OR jeśli wiele alternatyw */
        ::Id final_id = alt_ids[0];
        for (size_t i = 1; i < alt_ids.size(); ++i) {
            final_id = pool_rel2id(pool, final_id, alt_ids[i], REL_OR, 1);
            {
                log::debug("OR-dep: " + or_parts[0] + " | " + or_parts[i]);
            }
        }

        /* Dodaj do pola Solvable */
        int effective_keyname = keyname;
        if (is_weak) {
            /* Recommends: SOLVABLE_SUGGESTS -- libsolv traktuje jako "weak dep" */
            effective_keyname = SOLVABLE_SUGGESTS;
        }

        switch (effective_keyname) {
            case SOLVABLE_REQUIRES:
                s->requires    = repo_addid_dep(repo, s->requires,    final_id, 0); break;
            case SOLVABLE_CONFLICTS:
                s->conflicts   = repo_addid_dep(repo, s->conflicts,   final_id, 0); break;
            case SOLVABLE_PROVIDES:
                s->provides    = repo_addid_dep(repo, s->provides,    final_id, 0); break;
            case SOLVABLE_OBSOLETES:
                s->obsoletes   = repo_addid_dep(repo, s->obsoletes,   final_id, 0); break;
            case SOLVABLE_SUGGESTS:
                s->suggests    = repo_addid_dep(repo, s->suggests,    final_id, 0); break;
            default: break;
        }
    }
}

/* Mapuje architekturę Debiana ("amd64") na nazwę libsolv (uname -m: "x86_64").
 * libsolv używa nazw kernelowych, nie dpkg. */
static std::string deb_arch_to_libsolv(const std::string& deb_arch) {
    if (deb_arch == "amd64")   return "x86_64";
    if (deb_arch == "arm64")   return "aarch64";
    if (deb_arch == "armhf")   return "armv7hl";
    if (deb_arch == "armel")   return "armv5tel";
    if (deb_arch == "i386")    return "i686";
    if (deb_arch == "mips64el") return "mips64el";
    if (deb_arch == "ppc64el") return "ppc64le";
    if (deb_arch == "s390x")   return "s390x";
    if (deb_arch == "riscv64") return "riscv64";
    return deb_arch; /* pass-through dla nieznanych */
}

/* Buduje szczegółowy komunikat błędu z rozwiązania problemu libsolv.
 * Zawiera sugestie alternatyw gdy możliwe. */
std::string format_problem_detailed(::Solver* solver, ::Pool* /*pool*/, ::Id problem) {
    std::ostringstream oss;

    /* Główny komunikat problemu */
    const char* prob_str = solver_problem2str(solver, problem);
    oss << "  • " << (prob_str ? prob_str : "nieznany problem") << "\n";

    /* Proponowane rozwiązania (jeśli libsolv je zna) */
    ::Id solution = 0;
    int sol_count = 0;
    while ((solution = solver_next_solution(solver, problem, solution)) != 0 && sol_count < 3) {
        ++sol_count;
        oss << "    Rozwiązanie " << sol_count << ":\n";

        /* solver_solutionelement2str iteruje przez elementy rozwiązania.
         * API: solver_next_solutionelement(solver, problem, solution, element, &p, &rp)
         * Parametry p/rp to opcjonalne Id solvable -- nie potrzebujemy ich tu. */
        ::Id element = 0;
        ::Id p = 0, rp = 0;
        while ((element = solver_next_solutionelement(
                    solver, problem, solution, element, &p, &rp)) != 0) {
            /* solver_solutionelement2str(solver, p, rp) -- 3 argumenty */
            const char* sol_str = solver_solutionelement2str(solver, p, rp);
            if (sol_str) oss << "      - " << sol_str << "\n";
        }
    }

    return oss.str();
}

} // namespace

/* ── SolvPool ── */

SolvPool SolvPool::create(const std::string& deb_arch) {
    SolvPool sp;
    sp.pool_ = pool_create();

    /* Ustaw architekturę -- libsolv filtruje pakiety niezgodne z arch */
    std::string libsolv_arch = deb_arch_to_libsolv(deb_arch);
    pool_setarch(sp.pool_, libsolv_arch.c_str());
    sp.arch_ = deb_arch;

    /* Ustaw disttype Debian -- włącza poprawne parsowanie wersji epoch:ver-rev */
    pool_setdisttype(sp.pool_, DISTTYPE_DEB);

    log::debug("SolvPool: utworzono pul, arch=" + deb_arch + " (libsolv=" + libsolv_arch + ")");
    return sp;
}

SolvPool::~SolvPool() {
    if (pool_) pool_free(pool_);
}

SolvPool::SolvPool(SolvPool&& other) noexcept
    : pool_(other.pool_), arch_(std::move(other.arch_))
{
    other.pool_ = nullptr;
}

SolvPool& SolvPool::operator=(SolvPool&& other) noexcept {
    if (this != &other) {
        if (pool_) pool_free(pool_);
        pool_ = other.pool_;
        arch_ = std::move(other.arch_);
        other.pool_ = nullptr;
    }
    return *this;
}

/* ── add_repo_from_index ── */

void SolvPool::add_repo_from_index(const apt::RepoIndex& index,
                                    const std::string& repo_name)
{
    ::Repo* repo = repo_create(pool_, repo_name.c_str());
    ::Repodata* data = repo_add_repodata(repo, 0);

    int added = 0;
    for (auto& entry : index.entries()) {
        /* Filtruj pakiety o architekturze niezgodnej z pula.
         * Zawsze akceptuj:
         *   - "all"   -- architekturalnie niezalezne (python3, fonts, doc)
         *   - ""      -- brak pola Architecture (stare indeksy)
         *   - arch_   -- architektura docelowa (amd64, arm64 itd.)
         * Odrzucaj np. i386 gdy arch_=amd64, chyba ze mamy multi-arch. */
        if (!entry.architecture.empty() &&
            entry.architecture != "all" &&
            entry.architecture != arch_) {
            continue;
        }

        ::Id sid = repo_add_solvable(repo);
        ::Solvable* s = pool_id2solvable(pool_, sid);

        s->name = pool_str2id(pool_, entry.package.c_str(), 1);
        s->evr  = pool_str2id(pool_, entry.version.c_str(), 1);

        /* arch: "all" -> "noarch" w libsolv.
         * WAZNE: pool_setarch() musi byc wywolane PRZED add_repo_from_index.
         * Libsolv traktuje "noarch" jako zawsze zgodne z architektura puli
         * gdy pool->disttype = DISTTYPE_DEB -- wiec noarch jest zawsze
         * instalowalne bez wzgledu na pool_setarch(). */
        std::string arch = (entry.architecture == "all") ? "noarch" : entry.architecture;
        if (arch.empty()) arch = arch_.empty() ? "amd64" : arch_;
        s->arch = pool_str2id(pool_, arch.c_str(), 1);

        /* Self-provides: wymagane przez libsolv do poprawnego działania
         * zależności wersjonowanych ("pkg (= ver)") */
        ::Id self_provide = pool_rel2id(pool_, s->name, s->evr, REL_EQ, 1);
        s->provides = repo_addid_dep(repo, s->provides, self_provide, 0);

        /* Zależności obowiązkowe */
        add_deps_from_string(pool_, repo, s, entry.pre_depends, SOLVABLE_REQUIRES);
        add_deps_from_string(pool_, repo, s, entry.depends,     SOLVABLE_REQUIRES);

        /* Conflicts i Breaks -- oba blokują instalację pakietu gdy kolizja */
        add_deps_from_string(pool_, repo, s, entry.conflicts, SOLVABLE_CONFLICTS);
        add_deps_from_string(pool_, repo, s, entry.breaks,    SOLVABLE_CONFLICTS);

        /* Provides -- wirtualne pakiety (np. "editor", "mail-transport-agent") */
        add_deps_from_string(pool_, repo, s, entry.provides, SOLVABLE_PROVIDES);

        /* Replaces -> Obsoletes (libsolv: pakiet zastępuje inny przy upgrade) */
        add_deps_from_string(pool_, repo, s, entry.replaces, SOLVABLE_OBSOLETES);

        /* Recommends -- słabe zależności: instalowane gdy dostępne, nie blokują
         * Używamy SOLVABLE_SUGGESTS + SOLVER_WEAK przy jobs */
        add_deps_from_string(pool_, repo, s, entry.recommends, SOLVABLE_REQUIRES,
                             true /* is_weak */);

        /* Metadane do pobrania pliku .deb */
        if (!entry.filename.empty())
            repodata_set_str(data, sid, SOLVABLE_MEDIAFILE,    entry.filename.c_str());
        if (!entry.sha256.empty())
            repodata_set_str(data, sid, SOLVABLE_CHECKSUM,     entry.sha256.c_str());
        if (entry.size > 0)
            repodata_set_num(data, sid, SOLVABLE_DOWNLOADSIZE, entry.size);

        ++added;
    }

    repodata_internalize(data);
    repo_internalize(repo);

    log::debug("SolvPool: repo '" + repo_name + "': " + std::to_string(added)
               + "/" + std::to_string(index.entries().size()) + " pakietów (arch=" + arch_ + ")");
}

/* ── add_installed_packages ── */

void SolvPool::add_installed_packages(
    const std::vector<statusdb::InstalledPackage>& installed)
{
    if (installed.empty()) return;

    /* @System = konwencja libsolv dla zainstalowanego stanu systemu.
     * pool_set_installed() instruuje solver żeby:
     *   - traktował te pakiety jako obecne w systemie
     *   - sprawdzał Conflicts/Breaks między nowymi a @System
     *   - nie instalował ponownie tego co jest w @System */
    ::Repo* system_repo = repo_create(pool_, "@System");

    std::string libsolv_arch = deb_arch_to_libsolv(arch_);

    for (auto& pkg : installed) {
        ::Id sid = repo_add_solvable(system_repo);
        ::Solvable* s = pool_id2solvable(pool_, sid);

        s->name   = pool_str2id(pool_, pkg.name.c_str(), 1);
        s->evr    = pool_str2id(pool_, pkg.version.c_str(), 1);
        s->arch   = pool_str2id(pool_, libsolv_arch.c_str(), 1);
        s->vendor = pool_str2id(pool_, "deb-ostree", 1);

        /* Self-provides -- krytyczne dla detekcji konfliktów */
        ::Id self_dep = pool_rel2id(pool_, s->name, s->evr, REL_EQ, 1);
        s->provides = repo_addid_dep(system_repo, s->provides, self_dep, 0);
    }

    pool_set_installed(pool_, system_repo);
    log::debug("SolvPool: @System: " + std::to_string(installed.size())
               + " zainstalowanych pakietów");
}

/* ── format_solver_problems ── */

std::string SolvPool::format_solver_problems(::Solver* solver) {
    int problem_count = solver_problem_count(solver);
    std::ostringstream oss;
    oss << "Resolver zależności (libsolv SAT) nie znalazł rozwiązania.\n"
        << problem_count << " problem(ów):\n\n";

    ::Id problem = 0;
    while ((problem = solver_next_problem(solver, problem)) != 0) {
        oss << format_problem_detailed(solver, pool_, problem);
    }

    oss << "\nWskazówki:\n"
        << "  • Sprawdź 'deb-ostree search <pakiet>' czy pakiet jest dostępny\n"
        << "  • Sprawdź skonfigurowane apt_sources w deb-ostree.hk\n"
        << "  • Uruchom 'deb-ostree list' aby zobaczyć konflikty z zainstalowanymi\n";

    return oss.str();
}

/* ── resolve_install ── */

std::vector<ResolvedPackage> SolvPool::resolve_install(
    const std::vector<std::string>& package_names)
{
    /* Przygotuj indeksy provides (konieczne przed każdą sesją solvera) */
    pool_addfileprovides(pool_);
    pool_createwhatprovides(pool_);

    ::Queue jobs;
    queue_init(&jobs);

    for (auto& name : package_names) {
        /* Szukaj po nazwie LUB jako wirtualny pakiet (Provides:) */
        ::Id name_id = pool_str2id(pool_, name.c_str(), 0);
        if (name_id == 0) {
            queue_free(&jobs);
            throw SolvError(
                "Pakiet '" + name + "' nie istnieje w żadnym skonfigurowanym "
                "repozytorium.\nUruchom 'deb-ostree search " + name + "' aby sprawdzić.");
        }

        /* SOLVER_SOLVABLE_PROVIDES: szuka po nazwie ORAZ wirtualnych provides.
         * Dzięki temu "install editor" znajdzie vim który ma Provides: editor. */
        queue_push2(&jobs, SOLVER_SOLVABLE_PROVIDES | SOLVER_INSTALL, name_id);
    }

    ::Solver* solver = solver_create(pool_);

    /* Polityki solvera: */

    /* Nie pozwalaj na downgrade przy rozwiązywaniu */
    solver_set_flag(solver, SOLVER_FLAG_ALLOW_DOWNGRADE, 0);

    /* Preferuj najnowszą wersję */
    solver_set_flag(solver, SOLVER_FLAG_BEST_OBEY_POLICY, 1);

    /* Nie instaluj dodatkowych Recommends automatycznie (explicit install only) */
    solver_set_flag(solver, SOLVER_FLAG_IGNORE_RECOMMENDED, 1);

    /* Nie usuwaj istniejących pakietów żeby rozwiązać konflikt -- zgłoś błąd */
    solver_set_flag(solver, SOLVER_FLAG_ALLOW_UNINSTALL, 0);

    int problem_count = solver_solve(solver, &jobs);

    if (problem_count > 0) {
        std::string err = format_solver_problems(solver);
        solver_free(solver);
        queue_free(&jobs);
        throw SolvError(err);
    }

    ::Transaction* trans = solver_create_transaction(solver);
    transaction_order(trans, 0); /* topologiczny porządek instalacji */

    std::vector<ResolvedPackage> result;
    std::unordered_set<std::string> seen; /* deduplikacja po name+arch */

    for (int i = 0; i < trans->steps.count; ++i) {
        ::Id p = trans->steps.elements[i];

        /* Pomiń pakiety do usunięcia (w teorii nie powinno być przy install) */
        if (transaction_type(trans, p, SOLVER_TRANSACTION_SHOW_ALL) ==
            SOLVER_TRANSACTION_ERASE) continue;

        ::Solvable* s = pool_id2solvable(pool_, p);
        std::string pkg_name = pool_id2str(pool_, s->name);
        std::string pkg_arch = pool_id2str(pool_, s->arch);
        std::string dedup_key = pkg_name + ":" + pkg_arch;

        if (seen.count(dedup_key)) continue;
        seen.insert(dedup_key);

        ResolvedPackage rp;
        rp.name    = pkg_name;
        rp.version = pool_id2str(pool_, s->evr);

        const char* filename = solvable_lookup_str(s, SOLVABLE_MEDIAFILE);
        const char* sha256   = solvable_lookup_str(s, SOLVABLE_CHECKSUM);
        rp.filename = filename ? filename : "";
        rp.sha256   = sha256   ? sha256   : "";
        rp.size     = static_cast<uint64_t>(solvable_lookup_num(s, SOLVABLE_DOWNLOADSIZE, 0));

        result.push_back(std::move(rp));
    }

    transaction_free(trans);
    solver_free(solver);
    queue_free(&jobs);

    log::debug("SolvPool: resolve_install: " + std::to_string(result.size())
               + " pakietów do zainstalowania");
    return result;
}

/* ── resolve_remove ── */

std::vector<std::string> SolvPool::resolve_remove(
    const std::vector<std::string>& package_names)
{
    pool_addfileprovides(pool_);
    pool_createwhatprovides(pool_);

    ::Queue jobs;
    queue_init(&jobs);

    for (auto& name : package_names) {
        ::Id name_id = pool_str2id(pool_, name.c_str(), 0);
        if (name_id == 0) {
            log::warn("resolve_remove: '" + name + "' nieznany w puli -- pomijam");
            continue;
        }
        queue_push2(&jobs, SOLVER_SOLVABLE_PROVIDES | SOLVER_ERASE, name_id);
    }

    if (jobs.count == 0) {
        queue_free(&jobs);
        return {};
    }

    ::Solver* solver = solver_create(pool_);

    /* Przy usuwaniu: pozwól na usunięcie pakietów zależnych */
    solver_set_flag(solver, SOLVER_FLAG_ALLOW_UNINSTALL, 1);

    /* Nie czyść nieużywanych zależności automatycznie (jak apt autoremove)
     * -- to jest osobna komenda */
    solver_set_flag(solver, SOLVER_CLEANDEPS, 0);

    int problem_count = solver_solve(solver, &jobs);

    if (problem_count > 0) {
        std::string err = format_solver_problems(solver);
        solver_free(solver);
        queue_free(&jobs);
        throw SolvError(err);
    }

    ::Transaction* trans = solver_create_transaction(solver);

    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    for (int i = 0; i < trans->steps.count; ++i) {
        ::Id p = trans->steps.elements[i];

        /* Zbierz tylko pakiety do usunięcia */
        int ttype = transaction_type(trans, p, SOLVER_TRANSACTION_SHOW_ALL);
        if (ttype != SOLVER_TRANSACTION_ERASE &&
            ttype != SOLVER_TRANSACTION_OBSOLETED) continue;

        ::Solvable* s = pool_id2solvable(pool_, p);
        std::string pkg_name = pool_id2str(pool_, s->name);
        if (seen.count(pkg_name)) continue;
        seen.insert(pkg_name);
        result.push_back(pkg_name);
    }

    transaction_free(trans);
    solver_free(solver);
    queue_free(&jobs);

    log::debug("SolvPool: resolve_remove: " + std::to_string(result.size())
               + " pakietów do usunięcia");
    return result;
}

/* ── resolve_upgrade ── */

std::vector<ResolvedPackage> SolvPool::resolve_upgrade(
    const std::vector<std::string>& package_names)
{
    /* WAZNE (#8): Wykrywanie Breaks/Conflicts z nową wersją wymaga że
     * zainstalowane pakiety są w puli jako @System (pool_set_installed).
     * Caller MUSI wywołać add_installed_packages() PRZED resolve_upgrade()
     * jeśli chce wykrywać konflikty nowej wersji z istniejącymi pakietami.
     * pool_builder.cpp::build_solv_pool() robi to automatycznie. */

    pool_addfileprovides(pool_);
    pool_createwhatprovides(pool_);

    ::Queue jobs;
    queue_init(&jobs);

    if (package_names.empty()) {
        /* Upgrade wszystkich zainstalowanych */
        queue_push2(&jobs, SOLVER_UPDATE | SOLVER_SOLVABLE_ALL, 0);
    } else {
        for (auto& name : package_names) {
            ::Id name_id = pool_str2id(pool_, name.c_str(), 0);
            if (name_id == 0) {
                log::warn("resolve_upgrade: '" + name + "' nieznany -- pomijam");
                continue;
            }
            queue_push2(&jobs, SOLVER_UPDATE | SOLVER_SOLVABLE_PROVIDES, name_id);
        }
    }

    if (jobs.count == 0) {
        queue_free(&jobs);
        return {};
    }

    ::Solver* solver = solver_create(pool_);
    solver_set_flag(solver, SOLVER_FLAG_BEST_OBEY_POLICY, 1);
    solver_set_flag(solver, SOLVER_FLAG_ALLOW_DOWNGRADE, 0);
    solver_set_flag(solver, SOLVER_FLAG_IGNORE_RECOMMENDED, 1);

    int problem_count = solver_solve(solver, &jobs);

    if (problem_count > 0) {
        std::string err = format_solver_problems(solver);
        solver_free(solver);
        queue_free(&jobs);
        throw SolvError(err);
    }

    ::Transaction* trans = solver_create_transaction(solver);
    transaction_order(trans, 0);

    std::vector<ResolvedPackage> result;
    std::unordered_set<std::string> seen;

    for (int i = 0; i < trans->steps.count; ++i) {
        ::Id p = trans->steps.elements[i];
        int ttype = transaction_type(trans, p, SOLVER_TRANSACTION_SHOW_ALL);

        /* Zbierz instalacje i aktualizacje */
        if (ttype != SOLVER_TRANSACTION_INSTALL &&
            ttype != SOLVER_TRANSACTION_UPGRADE  &&
            ttype != SOLVER_TRANSACTION_REINSTALL) continue;

        ::Solvable* s = pool_id2solvable(pool_, p);
        std::string pkg_name = pool_id2str(pool_, s->name);
        if (seen.count(pkg_name)) continue;
        seen.insert(pkg_name);

        ResolvedPackage rp;
        rp.name    = pkg_name;
        rp.version = pool_id2str(pool_, s->evr);
        const char* fn  = solvable_lookup_str(s, SOLVABLE_MEDIAFILE);
        const char* sha = solvable_lookup_str(s, SOLVABLE_CHECKSUM);
        rp.filename = fn  ? fn  : "";
        rp.sha256   = sha ? sha : "";
        rp.size     = static_cast<uint64_t>(solvable_lookup_num(s, SOLVABLE_DOWNLOADSIZE, 0));
        result.push_back(std::move(rp));
    }

    transaction_free(trans);
    solver_free(solver);
    queue_free(&jobs);

    log::debug("SolvPool: resolve_upgrade: " + std::to_string(result.size())
               + " pakietów do aktualizacji");
    return result;
}

} // namespace debostree::solv

/* ── resolve_autoremove jest zdefiniowane w namespace debostree::solv ponizej ── */

namespace debostree::solv {

/* ── resolve_autoremove [NOWE 0.2.0] ── */

std::vector<std::string> SolvPool::resolve_autoremove(
    const std::vector<statusdb::InstalledPackage>& installed,
    const std::vector<PackageLayer>& explicit_pkgs)
{
    pool_addfileprovides(pool_);
    pool_createwhatprovides(pool_);

    /* Zbierz nazwy pakietow jawnie zainstalowanych przez uzytkownika */
    std::unordered_set<std::string> explicit_names;
    for (auto& p : explicit_pkgs) explicit_names.insert(p.name);

    /* Candidate do usuniecia: zainstalowane ale NIEOBECNE w explicit_pkgs.
     * Czyli: zainstalowane jako zaleznosci, teraz potencjalnie osierocon e. */
    std::vector<std::string> candidates;
    for (auto& p : installed)
        if (!explicit_names.count(p.name)) candidates.push_back(p.name);

    if (candidates.empty()) return {};

    ::Queue jobs;
    queue_init(&jobs);

    for (auto& name : candidates) {
        ::Id name_id = pool_str2id(pool_, name.c_str(), 0);
        if (name_id == 0) continue;
        queue_push2(&jobs, SOLVER_ERASE | SOLVER_SOLVABLE_PROVIDES, name_id);
    }

    if (jobs.count == 0) {
        queue_free(&jobs);
        return {};
    }

    ::Solver* solver = solver_create(pool_);
    solver_set_flag(solver, SOLVER_CLEANDEPS,        1); /* usun osierocon e */
    solver_set_flag(solver, SOLVER_FLAG_ALLOW_UNINSTALL, 1);

    int problem_count = solver_solve(solver, &jobs);
    if (problem_count > 0) {
        std::string err = format_solver_problems(solver);
        solver_free(solver);
        queue_free(&jobs);
        throw SolvError(err);
    }

    ::Transaction* trans = solver_create_transaction(solver);
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    for (int i = 0; i < trans->steps.count; ++i) {
        ::Id p = trans->steps.elements[i];
        int ttype = transaction_type(trans, p, SOLVER_TRANSACTION_SHOW_ALL);
        if (ttype != SOLVER_TRANSACTION_ERASE &&
            ttype != SOLVER_TRANSACTION_OBSOLETED) continue;

        ::Solvable* s = pool_id2solvable(pool_, p);
        std::string pkg_name = pool_id2str(pool_, s->name);

        /* Nie usuwaj pakietow jawnie zainstalowanych przez uzytkownika */
        if (explicit_names.count(pkg_name)) continue;
        if (seen.count(pkg_name)) continue;
        seen.insert(pkg_name);
        result.push_back(pkg_name);
    }

    transaction_free(trans);
    solver_free(solver);
    queue_free(&jobs);

    log::debug("resolve_autoremove: " + std::to_string(result.size()) +
               " osierocon ych pakietow");
    return result;
}

} // namespace debostree::solv
