#include "../cmd/apt_repo_index.h"

#include <iostream>
#include <cassert>
#include <stdexcept>

using namespace debostree;

static const char* PACKAGES_CONTENT = R"(
Package: vim
Version: 2:9.0.1378-2
Architecture: amd64
Maintainer: Debian Vim Maintainers <team+vim@tracker.debian.org>
Depends: vim-common (= 2:9.0.1378-2), libacl1 (>= 2.2.23)
Provides: editor
Filename: pool/main/v/vim/vim_9.0.1378-2_amd64.deb
Size: 1876504
SHA256: a1b2c3d4e5f6000000000000000000000000000000000000000000000000000001
Description: Vi IMproved - enhanced vi editor
 Vi IMproved is an almost compatible version of the UNIX editor Vi.
 .
 Many new features have been added: multi-level undo, syntax highlighting,
 command line history and much more.

Package: htop
Version: 3.2.2-1
Architecture: amd64
Depends: libc6 (>= 2.36), libncursesw6 (>= 6)
Filename: pool/main/h/htop/htop_3.2.2-1_amd64.deb
Size: 170064
SHA256: b2c3d4e5f6000000000000000000000000000000000000000000000000000002
Description: interactive processes viewer

Package: curl
Version: 7.88.1-10+deb12u5
Architecture: amd64
Pre-Depends: libc6 (>= 2.17)
Depends: libcurl4 (= 7.88.1-10+deb12u5), zlib1g (>= 1:1.1.4)
Filename: pool/main/c/curl/curl_7.88.1-10+deb12u5_amd64.deb
Size: 326072
SHA256: c3d4e5f600000000000000000000000000000000000000000000000000000003
Description: command line tool for transferring data with URL syntax

)";

static void test_basic_parse() {
    std::cout << "[TEST] test_basic_parse ... ";
    apt::RepoIndex idx = apt::RepoIndex::parse(PACKAGES_CONTENT);
    auto& entries = idx.entries();

    assert(entries.size() == 3 && "Powinny być 3 wpisy");

    auto& vim = entries[0];
    assert(vim.package == "vim");
    assert(vim.version == "2:9.0.1378-2");
    assert(vim.architecture == "amd64");
    assert(vim.filename == "pool/main/v/vim/vim_9.0.1378-2_amd64.deb");
    assert(vim.size == 1876504);
    assert(vim.sha256 == "a1b2c3d4e5f6000000000000000000000000000000000000000000000000000001");

    std::cout << "OK (" << entries.size() << " wpisów)\n";
}

static void test_depends_parsed() {
    std::cout << "[TEST] test_depends_parsed ... ";
    apt::RepoIndex idx = apt::RepoIndex::parse(PACKAGES_CONTENT);
    auto& entries = idx.entries();

    /* vim ma Depends: vim-common (= 2:9.0.1378-2), libacl1 (>= 2.2.23) */
    auto& vim = entries[0];
    assert(!vim.depends.empty() && "Depends vim powinny być niepuste");
    assert(vim.depends.find("vim-common") != std::string::npos);

    /* curl ma Pre-Depends */
    auto& curl = entries[2];
    assert(!curl.pre_depends.empty() && "Pre-Depends curl powinny być niepuste");
    assert(curl.pre_depends.find("libc6") != std::string::npos);

    std::cout << "OK\n";
}

static void test_multiline_description() {
    std::cout << "[TEST] test_multiline_description ... ";
    apt::RepoIndex idx = apt::RepoIndex::parse(PACKAGES_CONTENT);
    auto& vim = idx.entries()[0];

    /* Opis wieloliniowy powinien być wczytany (przynajmniej pierwsza linia) */
    assert(!vim.description.empty() && "Description powinna być niepusta");
    assert(vim.description.find("Vi IMproved") != std::string::npos);

    std::cout << "OK\n";
}

static void test_provides_field() {
    std::cout << "[TEST] test_provides_field ... ";
    apt::RepoIndex idx = apt::RepoIndex::parse(PACKAGES_CONTENT);
    auto& vim = idx.entries()[0];

    assert(vim.provides == "editor" && "vim powinien mieć Provides: editor");
    std::cout << "OK\n";
}

static void test_empty_input() {
    std::cout << "[TEST] test_empty_input ... ";
    apt::RepoIndex idx = apt::RepoIndex::parse("");
    assert(idx.entries().empty() && "Pusty input => brak wpisów");
    std::cout << "OK\n";
}

static void test_epoch_version() {
    std::cout << "[TEST] test_epoch_version ... ";
    apt::RepoIndex idx = apt::RepoIndex::parse(PACKAGES_CONTENT);
    /* vim ma wersję z epochą: "2:9.0.1378-2" */
    assert(idx.entries()[0].version == "2:9.0.1378-2");
    std::cout << "OK\n";
}

int main() {
    std::cout << "=== AptRepoIndex unit tests ===\n";
    try {
        test_basic_parse();
        test_depends_parsed();
        test_multiline_description();
        test_provides_field();
        test_empty_input();
        test_epoch_version();
        std::cout << "\nWszystkie testy APT_REPO_INDEX zaliczone.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nBLAD: " << e.what() << "\n";
        return 1;
    }
}
