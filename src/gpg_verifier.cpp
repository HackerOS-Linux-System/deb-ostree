#include "../cmd/gpg_verifier.h"
#include "../cmd/process.h"
#include "../cmd/logging.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace debostree::gpg {

/* ── Helpers ── */

static void write_temp_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::trunc | std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("gpg: nie można zapisać pliku tymczasowego: " + path);
    f << content;
}

/* Parsuje wyjście gpgv i wyodrębnia fingerprint podpisującego. */
static std::string extract_fingerprint(const std::string& gpgv_output) {
    /* gpgv wypisuje linie takie jak:
     *   gpgv: Good signature from "Debian Archive Automatic Signing Key ..."
     *   gpgv: aka ...
     * Szukamy fingerprinta w formacie KEY_ID z linii "[GNUPG:] VALIDSIG ..." */
    size_t pos = gpgv_output.find("VALIDSIG ");
    if (pos == std::string::npos) return "";
    pos += 9;
    size_t end = gpgv_output.find(' ', pos);
    if (end == std::string::npos) end = gpgv_output.find('\n', pos);
    return (end == std::string::npos) ? gpgv_output.substr(pos)
                                      : gpgv_output.substr(pos, end - pos);
}

/* ── GpgVerifier ── */

GpgVerifier::GpgVerifier(std::string keyring_dir)
    : keyring_dir_(std::move(keyring_dir))
{
    load_keyring_files();
}

void GpgVerifier::load_keyring_files() {
    keyring_files_.clear();
    std::error_code ec;
    if (!fs::exists(keyring_dir_, ec)) {
        log::warn("gpg: katalog kluczy '" + keyring_dir_ + "' nie istnieje -- "
                  "weryfikacja GPG wyłączona. Zainstaluj klucze Debian:\n"
                  "  sudo apt-key add /usr/share/keyrings/debian-archive-keyring.gpg");
        return;
    }

    for (auto& entry : fs::directory_iterator(keyring_dir_, ec)) {
        auto ext = entry.path().extension().string();
        if (ext == ".gpg" || ext == ".asc" || ext == ".kbx") {
            keyring_files_.push_back(entry.path().string());
            log::debug("gpg: klucz: " + entry.path().filename().string());
        }
    }

    if (keyring_files_.empty()) {
        log::warn("gpg: brak plików kluczy w '" + keyring_dir_ + "' -- "
                  "weryfikacja GPG wyłączona.");
    } else {
        log::debug("gpg: załadowano " + std::to_string(keyring_files_.size()) +
                   " keyrings z " + keyring_dir_);
    }
}

bool GpgVerifier::gpgv_available() {
    auto r = process::run({"gpgv", "--version"});
    return r.ok();
}

VerifyResult GpgVerifier::verify_inrelease(const std::string& inrelease_content,
                                            const std::string& temp_dir) const
{
    VerifyResult result;

    if (keyring_files_.empty()) {
        /* Brak kluczy -- nie możemy zweryfikować. Logujemy ostrzeżenie
         * (nie błąd krytyczny) żeby nie blokować środowisk bez apt-keyring. */
        log::warn("gpg: pominięto weryfikację InRelease -- brak zaufanych kluczy w '"
                  + keyring_dir_ + "'. W środowisku produkcyjnym jest to błąd bezpieczeństwa.");
        result.ok = true; /* soft-fail: nie blokujemy, logujemy */
        result.error_message = "brak kluczy -- weryfikacja pominięta";
        return result;
    }

    if (!gpgv_available()) {
        log::warn("gpg: gpgv niedostępny -- weryfikacja podpisu pominięta. "
                  "Zainstaluj pakiet 'gpgv'.");
        result.ok = true; /* soft-fail */
        result.error_message = "gpgv niedostępny";
        return result;
    }

    /* Zapisz InRelease do pliku tymczasowego */
    std::string inrelease_path = temp_dir + "/InRelease.tmp";
    write_temp_file(inrelease_path, inrelease_content);

    /* Zbuduj wywołanie gpgv z wszystkimi keyrings */
    std::vector<std::string> cmd = {"gpgv", "--status-fd", "1"};
    for (auto& kr : keyring_files_) {
        cmd.push_back("--keyring");
        cmd.push_back(kr);
    }
    cmd.push_back(inrelease_path);

    auto r = process::run(cmd, "", true /* merge stderr */);
    fs::remove(inrelease_path);

    if (!r.ok()) {
        result.ok = false;
        result.error_message = "gpgv: weryfikacja InRelease nie powiodła się:\n"
                               + r.stdout_data;
        log::error(result.error_message);
        return result;
    }

    result.ok = true;
    result.signer_fingerprint = extract_fingerprint(r.stdout_data);
    log::debug("gpg: InRelease zweryfikowany, podpisujący: " + result.signer_fingerprint);
    return result;
}

VerifyResult GpgVerifier::verify_release_gpg(const std::string& release_content,
                                              const std::string& release_gpg_content,
                                              const std::string& temp_dir) const
{
    VerifyResult result;

    if (keyring_files_.empty() || !gpgv_available()) {
        result.ok = true;
        result.error_message = "weryfikacja pominięta";
        return result;
    }

    std::string release_path     = temp_dir + "/Release.tmp";
    std::string release_gpg_path = temp_dir + "/Release.gpg.tmp";
    write_temp_file(release_path,     release_content);
    write_temp_file(release_gpg_path, release_gpg_content);

    std::vector<std::string> cmd = {"gpgv", "--status-fd", "1"};
    for (auto& kr : keyring_files_) {
        cmd.push_back("--keyring");
        cmd.push_back(kr);
    }
    cmd.push_back(release_gpg_path);
    cmd.push_back(release_path);

    auto r = process::run(cmd, "", true);
    fs::remove(release_path);
    fs::remove(release_gpg_path);

    if (!r.ok()) {
        result.ok = false;
        result.error_message = "gpgv: weryfikacja Release.gpg nie powiodła się:\n"
                               + r.stdout_data;
        return result;
    }

    result.ok = true;
    result.signer_fingerprint = extract_fingerprint(r.stdout_data);
    return result;
}

std::unordered_map<std::string, std::string>
GpgVerifier::parse_release_checksums(const std::string& release_content) {
    /* InRelease/Release zawiera sekcję SHA256: z liniami:
     *   <sha256>  <size>  <relative_path>
     * np.:
     *   a1b2c3...  12345  main/binary-amd64/Packages.xz
     * Szukamy tej sekcji i parsujemy ją. */
    std::unordered_map<std::string, std::string> result;

    std::istringstream iss(release_content);
    std::string line;
    bool in_sha256_section = false;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "SHA256:") {
            in_sha256_section = true;
            continue;
        }

        /* Nowe pole (linia bez spacji na początku) kończy sekcję SHA256 */
        if (!line.empty() && line[0] != ' ' && line[0] != '\t') {
            in_sha256_section = false;
        }

        if (!in_sha256_section) continue;

        /* Format linii: " <sha256>  <size>  <path>" */
        std::istringstream ls(line);
        std::string sha256, size, path;
        ls >> sha256 >> size >> path;
        if (!sha256.empty() && !path.empty()) {
            result[path] = sha256;
        }
    }

    return result;
}

} // namespace debostree::gpg
