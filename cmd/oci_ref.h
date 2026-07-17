#pragma once
/*
 * deb-ostree -- oci_ref.h  [NOWY v0.2.0]
 * Parsowanie i walidacja referencji obrazu OCI.
 *
 * deb-ostree jest narzedziem dla systemow immutable opartych na obrazach OCI.
 * Kazdy system MUSI miec okreslony obraz bazowy (jak w bootc/rpm-ostree).
 *
 * Format referencji:
 *   [registry/]organizacja/obraz[:tag|@sha256:...]
 *
 * Przyklady poprawnych referencji:
 *   ghcr.io/mojorg/debian-bootc:bookworm          <- registry/org/image:tag
 *   registry.example.com/team/myos:1.0.0          <- custom registry
 *   docker.io/library/debian:bookworm-slim         <- Docker Hub
 *   quay.io/fedora/fedora-bootc:40                 <- Quay
 *   localhost/local-build:latest                   <- lokalny build
 *
 * Bledne referencje (bez organizacji):
 *   debian:bookworm    <- brak organizacji (niejasne kto utrzymuje)
 *   bookworm           <- brak wszystkiego
 *
 * Wersja: 0.2.0
 */

#include <string>

namespace debostree::oci {

struct ImageRef {
    std::string registry;     /* np. "ghcr.io" (domyslnie: "docker.io") */
    std::string organization; /* np. "mojorg" */
    std::string image;        /* np. "debian-bootc" */
    std::string tag;          /* np. "bookworm" (domyslnie: "latest") */
    std::string digest;       /* np. "sha256:abc..." (opcjonalnie) */
    bool        valid = false;
    std::string error;

    /* Pelna referencja do uzycia przez skopeo/podman */
    std::string full_ref() const;

    /* Krotki opis dla uzytkownika */
    std::string short_ref() const;
};

/*
 * Parsuje referencje obrazu OCI.
 * Zwraca ImageRef z valid=false i opisem bledu jesli format nieprawidlowy.
 */
ImageRef parse(const std::string& ref);

/*
 * Waliduje referencje i rzuca std::runtime_error z czytelnym komunikatem
 * jesli format jest nieprawidlowy lub brak organizacji.
 */
void validate_or_throw(const std::string& ref);

/*
 * Sprawdza czy origin_refspec deploymentu jest ustawiony.
 * Uzywane przez install/remove/upgrade zeby weryfikowac ze system ma
 * znany obraz bazowy przed modyfikacja warstw.
 *
 * Rzuca runtime_error z instrukcja jak ustawic obraz bazowy.
 */
void require_origin_refspec(const std::string& origin_refspec,
                            const std::string& command_name);

} // namespace debostree::oci
