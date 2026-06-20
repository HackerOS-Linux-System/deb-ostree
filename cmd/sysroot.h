#pragma once
/*
 * deb-ostree -- sysroot.h
 * Wrapper na OstreeSysroot*: lista deploymentow, tworzenie nowego deploymentu,
 * rollback (przestawienie priorytetu bootloadera), cleanup (usuniecie starych).
 *
 * To jest serce mechanizmu immutable deploymentow -- kazdy "deb-ostree install"
 * konczy sie wywolaniem deploy_commit(), ktore:
 *   1. Rejestruje nowy deployment w OstreeSysroot (merge /etc, wpis w grub).
 *   2. Zachowuje poprzedni deployment jako dostepny do rollback.
 *   3. Nie dotyka aktywnie dzialajacego systemu -- zmiana wchodzi po reboot.
 *
 * Wersja: 0.0.1
 */

#include "types.h"
#include "ostree_repo.h"

#include <ostree.h>
#include <string>
#include <vector>
#include <optional>

namespace debostree {

class Sysroot {
public:
    /* Otwiera sysroot w path (zwykle "/"). Laduje tez wbudowane repo OSTree. */
    static Sysroot open(const std::string& path);

    /*
     * Lista wszystkich zarejestrowanych deploymentow, od najnowszego (index 0)
     * do najstarszego -- ta sama kolejnosc co "rpm-ostree status" i GRUB menu.
     */
    std::vector<Deployment> list_deployments() const;

    /* Aktualnie zabootowany deployment (nullopt jesli sysroot nie jest "live"). */
    std::optional<Deployment> booted_deployment() const;

    /*
     * Tworzy nowy deployment z commita checksum i rejestruje go jako domyslny
     * (staged) na nastepny reboot. Poprzedni deployment zachowywany jako
     * dostepny do rollback -- standardowe zachowanie rpm-ostree.
     */
    TransactionResult deploy_commit(const std::string&              checksum,
                                    const std::string&              osname,
                                    const std::string&              origin_refspec,
                                    const std::vector<PackageLayer>& layered_packages);

    /*
     * Zamienia kolejnoscia deploymenty #0 i #1 -- po reboot wejdzie w zycie
     * poprzedni deployment. Analog "rpm-ostree rollback".
     */
    TransactionResult rollback();

    /*
     * Usuwa deploymenty starsze niz keep_last_n i wywoluje ostree prune
     * na repo. Analog "rpm-ostree cleanup -p".
     */
    TransactionResult cleanup(int keep_last_n = 2);

    /* Fizyczna sciezka katalogu deployment na dysku:
     * /ostree/deploy/<osname>/deploy/<checksum>.<serial>  */
    std::string deployment_path(const Deployment& dep) const;

    ::OstreeSysroot*  raw()  const { return sysroot_.get(); }
    ostree::Repo&     repo()       { return repo_; }

private:
    Sysroot(::OstreeSysroot* s, ostree::Repo r)
        : sysroot_(s), repo_(std::move(r)) {}

    GObjPtr<::OstreeSysroot> sysroot_;
    ostree::Repo             repo_;

    Deployment to_deployment_struct(::OstreeDeployment* dep, bool is_booted) const;
};

} // namespace debostree
