#pragma once
/*
 * deb-ostree -- oci_puller.h
 * Sciaganie obrazu OCI z registry i rozpakowywanie go do plaskiego katalogu
 * rootfs gotowego do commitowania do repo OSTree.
 *
 * Deleguje do skopeo (sciaganie + konwersja na OCI layout) i podman
 * (eksport unified rootfs z poprawna obsuga whiteoutow OCI). To jest ten
 * sam wzorzec co containers/image (Go), ktora jest bazą bootc/rpm-ostree.
 *
 * W przyszlosci mozna zastapic podman przez umoci lub wlasna implementacje
 * rozpakowywania warstw tar OCI -- patrz ROADMAP w README.md.
 *
 * Wersja: 0.0.1
 */

#include <string>

namespace debostree {

class OciPuller {
public:
    /* work_dir: katalog na tymczasowe warstwy OCI i rootfs (powinien miec
     * minimum kilka GB wolnego miejsca, ten sam filesystem co /ostree). */
    explicit OciPuller(std::string work_dir);

    /*
     * Sciaga obraz image_ref (np. "registry.example.com/debian-bootc:bookworm"),
     * scala warstwy OCI z poprawna obsuga whiteoutow i zwraca sciezke
     * do katalogu z rozpakowanym rootfs gotowym do ostree commit_directory().
     *
     * Katalog zwrocony nalezy do callera -- odpowiada za jego usuniecie po
     * zakonczeniu ostree commit.
     *
     * Rzuca std::runtime_error przy bledach skopeo lub podman.
     */
    std::string pull_and_unpack(const std::string& image_ref);

private:
    std::string work_dir_;
};

} // namespace debostree
