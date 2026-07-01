#include "../cmd/solv_pool.h"
#include "../cmd/logging.h"

#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solver.h>
#include <solv/transaction.h>
#include <solv/policy.h>

#include <sstream>
#include <unordered_set>

namespace debostree::solv {

namespace {

/* Dodaje jedna zaleznosc (Id relacji) do odpowiedniego pola Solvable
 * (requires/conflicts/provides/obsoletes) przez repo_addid_dep -- prawdziwa
 * funkcja publiczna libsolv operujaca na Offset polach struct Solvable. */
void add_dep_id(::Repo* repo, ::Solvable* s, ::Id dep_id, int keyname) {
    switch (keyname) {
        case SOLVABLE_REQUIRES:
            s->requires = repo_addid_dep(repo, s->requires, dep_id, 0);
            break;
        case SOLVABLE_CONFLICTS:
            s->conflicts = repo_addid_dep(repo, s->conflicts, dep_id, 0);
            break;
        case SOLVABLE_PROVIDES:
            s->provides = repo_addid_dep(repo, s->provides, dep_id, 0);
            break;
        case SOLVABLE_OBSOLETES:
            s->obsoletes = repo_addid_dep(repo, s->obsoletes, dep_id, 0);
            break;
        default:
            break;
    }
}

/* Parsuje string zaleznosci w stylu Debian ("pkg1 (>= 1.0), pkg2 | pkg3")
 * na liste Id relacji libsolv i dodaje je do danego pola Solvable.
 *
 * Skladnia Debian:
 *   - przecinek ',' rozdziela alternatywy "AND" (wszystkie musza byc spelnione)
 *   - pipe '|' rozdziela alternatywy "OR" (przynajmniej jedna spelniona)
 *   - "(>= wersja)", "(<= wersja)", "(= wersja)", "(<< wersja)", "(>> wersja)"
 *     okreslaja ograniczenie wersji
 *
 * UPROSZCZENIE: dla klauzul z '|' (OR) bierzemy tylko PIERWSZA alternatywe
 * -- pelne wsparcie OR (SOLVER_SOLVABLE_PROVIDES z wieloma Id naraz przez
 * pool_rel2id z REL_OR) jest w ROADMAP. To upraszcza rozwiazywanie kosztem
 * mozliwego niedopasowania gdy pierwsza alternatywa jest niedostepna a
 * druga jest -- rzadki przypadek w typowych zaleznosciach Debiana, ale
 * realny (np. "Depends: default-mta | mail-transport-agent"). */
void add_deps_from_string(::Pool* pool, ::Repo* repo, ::Solvable* s,
                          const std::string& dep_string, int keyname) {
    if (dep_string.empty()) return;

    std::istringstream and_stream(dep_string);
    std::string and_part;

    while (std::getline(and_stream, and_part, ',')) {
        size_t pipe_pos = and_part.find('|');
        std::string clause = (pipe_pos != std::string::npos)
                             ? and_part.substr(0, pipe_pos) : and_part;

        auto b = clause.find_first_not_of(" \t");
        auto e = clause.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        clause = clause.substr(b, e - b + 1);

        std::string pkg_name = clause;
        std::string version_constraint;
        int rel_flags = 0;

        auto paren = clause.find('(');
        if (paren != std::string::npos) {
            pkg_name = clause.substr(0, paren);
            auto pe = pkg_name.find_last_not_of(" \t");
            if (pe != std::string::npos) pkg_name = pkg_name.substr(0, pe + 1);

            auto close_paren = clause.find(')', paren);
            std::string constraint = clause.substr(paren + 1,
                (close_paren == std::string::npos ? clause.size() : close_paren) - paren - 1);

            if      (constraint.rfind(">=", 0) == 0) { rel_flags = REL_GT | REL_EQ; version_constraint = constraint.substr(2); }
            else if (constraint.rfind("<=", 0) == 0) { rel_flags = REL_LT | REL_EQ; version_constraint = constraint.substr(2); }
            else if (constraint.rfind("<<", 0) == 0) { rel_flags = REL_LT; version_constraint = constraint.substr(2); }
            else if (constraint.rfind(">>", 0) == 0) { rel_flags = REL_GT; version_constraint = constraint.substr(2); }
            else if (constraint.rfind("=", 0)  == 0) { rel_flags = REL_EQ; version_constraint = constraint.substr(1); }

            auto vb = version_constraint.find_first_not_of(" \t");
            if (vb != std::string::npos) version_constraint = version_constraint.substr(vb);
        }

        if (pkg_name.empty()) continue;

        ::Id name_id = pool_str2id(pool, pkg_name.c_str(), 1);
        ::Id dep_id;

        if (rel_flags != 0 && !version_constraint.empty()) {
            ::Id evr_id = pool_str2id(pool, version_constraint.c_str(), 1);
            dep_id = pool_rel2id(pool, name_id, evr_id, rel_flags, 1);
        } else {
            dep_id = name_id;
        }

        add_dep_id(repo, s, dep_id, keyname);
    }
}

} // namespace

SolvPool SolvPool::create() {
    SolvPool sp;
    sp.pool_ = pool_create();
    pool_setarch(sp.pool_, "x86_64"); /* libsolv uzywa nazewnictwa uname -m, nie "amd64" Debiana */
    return sp;
}

SolvPool::~SolvPool() {
    if (pool_) pool_free(pool_);
}

SolvPool::SolvPool(SolvPool&& other) noexcept : pool_(other.pool_) {
    other.pool_ = nullptr;
}

SolvPool& SolvPool::operator=(SolvPool&& other) noexcept {
    if (this != &other) {
        if (pool_) pool_free(pool_);
        pool_ = other.pool_;
        other.pool_ = nullptr;
    }
    return *this;
}

void SolvPool::add_repo_from_index(const apt::RepoIndex& index, const std::string& repo_name) {
    ::Repo* repo = repo_create(pool_, repo_name.c_str());
    ::Repodata* data = repo_add_repodata(repo, 0);

    for (auto& entry : index.entries()) {
        ::Id sid = repo_add_solvable(repo);
        ::Solvable* s = pool_id2solvable(pool_, sid);

        s->name = pool_str2id(pool_, entry.package.c_str(), 1);
        s->evr  = pool_str2id(pool_, entry.version.c_str(), 1);
        s->arch = pool_str2id(pool_, entry.architecture.c_str(), 1);

        /* "Provides: <self> = <version>" jest wymagane przez libsolv, by
         * inne pakiety mogly zaleznosciowac sie na "pkg (= wersja)" wobec
         * tego pakietu -- standardowa konwencja przy recznym wypelnianiu Pool. */
        ::Id self_provide = pool_rel2id(pool_, s->name, s->evr, REL_EQ, 1);
        s->provides = repo_addid_dep(repo, s->provides, self_provide, 0);

        add_deps_from_string(pool_, repo, s, entry.pre_depends, SOLVABLE_REQUIRES);
        add_deps_from_string(pool_, repo, s, entry.depends,     SOLVABLE_REQUIRES);
        add_deps_from_string(pool_, repo, s, entry.conflicts,   SOLVABLE_CONFLICTS);
        add_deps_from_string(pool_, repo, s, entry.provides,    SOLVABLE_PROVIDES);
        add_deps_from_string(pool_, repo, s, entry.replaces,    SOLVABLE_OBSOLETES);

        /* Przechowujemy filename/sha256/size jako atrybuty Repodata --
         * potrzebne pozniej do pobrania pliku .deb z mirror po
         * rozwiazaniu zaleznosci. */
        repodata_set_str(data, sid, SOLVABLE_MEDIAFILE, entry.filename.c_str());
        repodata_set_str(data, sid, SOLVABLE_CHECKSUM, entry.sha256.c_str());
        repodata_set_num(data, sid, SOLVABLE_DOWNLOADSIZE, entry.size);
    }

    repodata_internalize(data);
    repo_internalize(repo);
    log::debug("SolvPool: dodano repo '" + repo_name + "' z " +
              std::to_string(index.entries().size()) + " pakietami");
}

std::string SolvPool::format_solver_problems(::Solver* solver) {
    std::ostringstream oss;
    int problem_count = solver_problem_count(solver);
    oss << "libsolv: nie udalo sie rozwiazac zaleznosci (" << problem_count << " problem(ow)):\n";

    ::Id problem = 0;
    while ((problem = solver_next_problem(solver, problem)) != 0) {
        const char* problem_str = solver_problem2str(solver, problem);
        oss << "  - " << (problem_str ? problem_str : "nieznany problem") << "\n";
    }
    return oss.str();
}

std::vector<ResolvedPackage> SolvPool::resolve_install(const std::vector<std::string>& package_names) {
    pool_addfileprovides(pool_);
    pool_createwhatprovides(pool_);

    ::Queue jobs;
    queue_init(&jobs);

    for (auto& name : package_names) {
        ::Id name_id = pool_str2id(pool_, name.c_str(), 0);
        if (name_id == 0) {
            queue_free(&jobs);
            throw SolvError("libsolv: pakiet '" + name + "' nie istnieje w zadnym dodanym repo");
        }
        queue_push2(&jobs, SOLVER_SOLVABLE_PROVIDES | SOLVER_INSTALL, name_id);
    }

    ::Solver* solver = solver_create(pool_);
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
        ::Solvable* s = pool_id2solvable(pool_, p);

        std::string pkg_name = pool_id2str(pool_, s->name);
        if (seen.count(pkg_name)) continue;
        seen.insert(pkg_name);

        ResolvedPackage rp;
        rp.name    = pkg_name;
        rp.version = pool_id2str(pool_, s->evr);

        const char* filename = solvable_lookup_str(s, SOLVABLE_MEDIAFILE);
        const char* sha256   = solvable_lookup_str(s, SOLVABLE_CHECKSUM);
        rp.filename = filename ? filename : "";
        rp.sha256   = sha256 ? sha256 : "";
        rp.size     = solvable_lookup_num(s, SOLVABLE_DOWNLOADSIZE, 0);

        result.push_back(std::move(rp));
    }

    transaction_free(trans);
    solver_free(solver);
    queue_free(&jobs);

    return result;
}

std::vector<std::string> SolvPool::resolve_remove(const std::vector<std::string>& package_names) {
    ::Queue jobs;
    queue_init(&jobs);

    for (auto& name : package_names) {
        ::Id name_id = pool_str2id(pool_, name.c_str(), 0);
        if (name_id == 0) continue; /* pakiet nieznany pulom -- nie ma czego usuwac wzgledem niego */
        queue_push2(&jobs, SOLVER_SOLVABLE_PROVIDES | SOLVER_ERASE, name_id);
    }

    ::Solver* solver = solver_create(pool_);
    int problem_count = solver_solve(solver, &jobs);

    if (problem_count > 0) {
        std::string err = format_solver_problems(solver);
        solver_free(solver);
        queue_free(&jobs);
        throw SolvError(err);
    }

    ::Transaction* trans = solver_create_transaction(solver);

    std::vector<std::string> result;
    for (int i = 0; i < trans->steps.count; ++i) {
        ::Id p = trans->steps.elements[i];
        ::Solvable* s = pool_id2solvable(pool_, p);
        result.push_back(pool_id2str(pool_, s->name));
    }

    transaction_free(trans);
    solver_free(solver);
    queue_free(&jobs);

    return result;
}

} // namespace debostree::solv
