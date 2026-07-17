#pragma once
/*
 * deb-ostree -- gpg_verifier.h  [NOWY v0.1.0]
 * Weryfikacja podpisów GPG indeksów repozytorium apt.
 *
 * Przepływ weryfikacji (zgodny ze standardem apt):
 *   1. Pobierz InRelease (podpisany plik z sumami kontrolnymi indeksów)
 *      LUB Release + Release.gpg (oddzielny podpis).
 *   2. Zweryfikuj podpis InRelease przez gpgv/gpg względem zaufanych kluczy
 *      w /etc/apt/trusted.gpg.d/ (lub skonfigurowanym keyring_path).
 *   3. Wyciągnij sumy kontrolne Packages.xz/gz z InRelease i sprawdź
 *      pobrany indeks Packages.
 *
 * Implementacja przez wywołanie gpgv (lekki weryfikator GPG bez agenta,
 * zawarty w pakiecie gpg lub gnupg) -- nie reimplementujemy GPG.
 *
 * Wersja: 0.2.0
 */

#include <string>
#include <vector>
#include <unordered_map>

namespace debostree::gpg {

/* Wynik weryfikacji podpisu. */
struct VerifyResult {
    bool        ok = false;
    std::string signer_fingerprint;
    std::string error_message;
};

/*
 * GpgVerifier weryfikuje pliki InRelease/Release.gpg przez gpgv.
 * Klucze zaufanych kluczy są ładowane z keyring_dir przy konstruowaniu.
 */
class GpgVerifier {
public:
    /* keyring_dir: katalog z zaufanymi kluczami publicznymi (.gpg/.asc),
     * domyślnie /etc/apt/trusted.gpg.d/ -- zgodnie z konwencją Debian. */
    explicit GpgVerifier(std::string keyring_dir = "/etc/apt/trusted.gpg.d");

    /*
     * Weryfikuje plik InRelease (zawiera podpis inline ClearsignMessage).
     * Zwraca VerifyResult.ok=true jeśli podpis poprawny i klucz zaufany.
     */
    VerifyResult verify_inrelease(const std::string& inrelease_content,
                                  const std::string& temp_dir) const;

    /*
     * Weryfikuje oddzielny plik Release + Release.gpg (dwa oddzielne pliki
     * -- starszy format, używany gdy InRelease niedostępny).
     */
    VerifyResult verify_release_gpg(const std::string& release_content,
                                    const std::string& release_gpg_content,
                                    const std::string& temp_dir) const;

    /*
     * Parsuje sekcję SHA256 z zawartości pliku InRelease/Release i zwraca
     * mapę: ścieżka_relative → oczekiwana_suma_sha256.
     * Używane przez DebFetcher do weryfikacji pobranych Packages.xz.
     */
    static std::unordered_map<std::string, std::string>
    parse_release_checksums(const std::string& release_content);

    /* Sprawdza czy gpgv jest dostępny w PATH. */
    static bool gpgv_available();

private:
    std::string keyring_dir_;
    std::vector<std::string> keyring_files_;

    void load_keyring_files();
};

} // namespace debostree::gpg
