#include "../cmd/oci_ref.h"

#include <stdexcept>
#include <sstream>
#include <vector>

namespace debostree::oci {

/* ── parser ── */

ImageRef parse(const std::string& ref) {
    ImageRef r;
    if (ref.empty()) {
        r.error = "Pusta referencja obrazu OCI.";
        return r;
    }

    std::string s = ref;

    /* Usun prefix "deb-ostree-oci:" jesli obecny (wewnetrzny format) */
    if (s.rfind("deb-ostree-oci:", 0) == 0) s = s.substr(15);

    /* Wyodrebnij digest @sha256:... */
    auto at_pos = s.rfind('@');
    if (at_pos != std::string::npos) {
        r.digest = s.substr(at_pos + 1);
        s        = s.substr(0, at_pos);
    }

    /* Wyodrebnij tag :tag */
    /* Znajdz ostatni ':' ktory nie jest czescia portu rejestru */
    auto tag_pos = s.rfind(':');
    if (tag_pos != std::string::npos) {
        /* Sprawdz czy ':' jest czescia portu (np. localhost:5000/org/img) */
        std::string before_colon = s.substr(0, tag_pos);
        std::string after_colon  = s.substr(tag_pos + 1);
        /* Jesli after_colon nie zawiera '/' -- to tag, nie port */
        if (after_colon.find('/') == std::string::npos && !after_colon.empty()) {
            r.tag = after_colon;
            s     = before_colon;
        }
    }
    if (r.tag.empty()) r.tag = "latest";

    /* Rozdziel na komponenty przez '/' */
    std::vector<std::string> parts;
    {
        std::istringstream iss(s);
        std::string part;
        while (std::getline(iss, part, '/')) parts.push_back(part);
    }

    /* Wykryj czy pierwszy komponent to registry (zawiera '.' lub ':') */
    if (parts.size() >= 3 ||
        (parts.size() >= 2 &&
         (parts[0].find('.') != std::string::npos ||
          parts[0].find(':') != std::string::npos ||
          parts[0] == "localhost"))) {
        /* registry/org/image lub registry/org */
        r.registry     = parts[0];
        r.organization = parts.size() >= 3 ? parts[1] : "";
        r.image        = parts.back();
    } else if (parts.size() == 2) {
        /* org/image -- brak registry, zakladamy docker.io */
        r.registry     = "docker.io";
        r.organization = parts[0];
        r.image        = parts[1];
    } else if (parts.size() == 1) {
        /* samo image -- niejasne, brak organizacji */
        r.image = parts[0];
    }

    /* Walidacja */
    if (r.organization.empty() && r.registry.empty()) {
        r.error = "Brak organizacji w referencji '" + ref + "'.\n"
                  "Format wymagany: registry/organizacja/obraz:tag\n"
                  "Przyklad: ghcr.io/mojorg/debian-bootc:bookworm";
        return r;
    }

    if (r.image.empty()) {
        r.error = "Brak nazwy obrazu w referencji '" + ref + "'.";
        return r;
    }

    r.valid = true;
    return r;
}

std::string ImageRef::full_ref() const {
    std::string s;
    if (!registry.empty()) s += registry + "/";
    if (!organization.empty()) s += organization + "/";
    s += image;
    if (!digest.empty()) s += "@" + digest;
    else if (!tag.empty() && tag != "latest") s += ":" + tag;
    else s += ":latest";
    return s;
}

std::string ImageRef::short_ref() const {
    std::string s;
    if (!organization.empty()) s += organization + "/";
    s += image + ":" + (tag.empty() ? "latest" : tag);
    return s;
}

void validate_or_throw(const std::string& ref) {
    auto r = parse(ref);
    if (!r.valid) {
        throw std::runtime_error(
            "Nieprawidlowa referencja obrazu OCI: " + r.error + "\n\n"
            "deb-ostree jest narzedziem dla systemow immutable opartych na obrazach OCI.\n"
            "Kazda instalacja musi byc oparta na konkretnym obrazie bazowym.\n\n"
            "Przyklady poprawnych referencji:\n"
            "  ghcr.io/mojorg/debian-bootc:bookworm\n"
            "  registry.example.com/team/myos:1.0.0\n"
            "  quay.io/fedora/fedora-bootc:40\n\n"
            "Uzyj 'deb-ostree deploy <obraz:tag>' aby zainicjalizowac system.");
    }
}

void require_origin_refspec(const std::string& origin_refspec,
                            const std::string& command_name)
{
    if (!origin_refspec.empty()) {
        /* Zweryfikuj ze refspec jest prawidlowy */
        std::string ref = origin_refspec;
        const std::string prefix = "deb-ostree-oci:";
        if (ref.rfind(prefix, 0) == 0) ref = ref.substr(prefix.size());
        auto r = parse(ref);
        if (r.valid) return; /* OK */
    }

    throw std::runtime_error(
        "Komenda '" + command_name + "' wymaga skonfigurowanego obrazu bazowego OCI.\n\n"
        "Ten system nie ma ustawionego obrazu bazowego (origin_refspec).\n\n"
        "Rozwiazania:\n"
        "  1. Zainicjalizuj system od nowa:\n"
        "     sudo deb-ostree deploy ghcr.io/twoja-org/debian-bootc:bookworm\n\n"
        "  2. Ustaw obraz bazowy dla istniejacego deploymentu:\n"
        "     sudo deb-ostree rebase ghcr.io/twoja-org/debian-bootc:bookworm\n\n"
        "deb-ostree wymaga obrazu OCI z nazwa organizacji (np. ghcr.io/org/image:tag)\n"
        "aby zapewnic powtarzalnosc i mozliwosc aktualizacji systemu.\n"
        "Obrazy bez organizacji (np. 'debian:bookworm') nie sa akceptowane.");
}

} // namespace debostree::oci
