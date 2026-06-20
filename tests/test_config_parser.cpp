#include "../cmd/hk_parser.h"
#include "../cmd/state_store.h"

#include <cassert>
#include <fstream>
#include <cstdio>
#include <iostream>

using debostree::hk::HkDocument;
using debostree::hk::HkParseError;

int main() {
    /* Test 1: podstawowe parsowanie sekcji i kluczy plain string. */
    {
        auto doc = HkDocument::parse(
            "[sysroot]\n"
            "-> path => /\n"
            "\n"
            "[system]\n"
            "-> osname => debian\n");

        assert(doc.getOr("sysroot", "path", "") == "/");
        assert(doc.getOr("system", "osname", "") == "debian");
        assert(doc.has("sysroot", "path"));
        assert(!doc.has("sysroot", "nieistniejacy"));
        std::cout << "[PASS] Test 1: podstawowe parsowanie\n";
    }

    /* Test 2: komentarze '!' i puste linie ignorowane. */
    {
        auto doc = HkDocument::parse(
            "! to jest komentarz\n"
            "\n"
            "[ostree]\n"
            "! kolejny komentarz\n"
            "-> repo_path => /ostree/repo\n");

        assert(doc.getOr("ostree", "repo_path", "") == "/ostree/repo");
        std::cout << "[PASS] Test 2: komentarze i puste linie\n";
    }

    /* Test 3: string cytowany z sekwencjami ucieczki. */
    {
        auto doc = HkDocument::parse(
            "[test]\n"
            "-> quoted => \"linia1\\nlinia2\"\n");

        assert(doc.getOr("test", "quoted", "") == "linia1\nlinia2");
        std::cout << "[PASS] Test 3: string cytowany z ucieczka\n";
    }

    /* Test 4: wartosc domyslna gdy klucz nie istnieje. */
    {
        auto doc = HkDocument::parse("[a]\n-> x => 1\n");
        assert(doc.getOr("a", "nieistnieje", "domyslna") == "domyslna");
        assert(doc.getOr("brak_sekcji", "x", "domyslna2") == "domyslna2");
        std::cout << "[PASS] Test 4: wartosci domyslne\n";
    }

    /* Test 5: blad -- klucz przed jakakolwiek sekcja. */
    {
        bool threw = false;
        try {
            HkDocument::parse("-> klucz => wartosc\n");
        } catch (const HkParseError&) {
            threw = true;
        }
        assert(threw);
        std::cout << "[PASS] Test 5: blad przy kluczu bez sekcji\n";
    }

    /* Test 6: blad -- niezamknieta sekcja. */
    {
        bool threw = false;
        try {
            HkDocument::parse("[niezamknieta\n-> x => 1\n");
        } catch (const HkParseError&) {
            threw = true;
        }
        assert(threw);
        std::cout << "[PASS] Test 6: blad przy niezamknietej sekcji\n";
    }

    /* Test 7: load_config (state_store) -- brak pliku -> wartosci domyslne. */
    {
        auto cfg = debostree::state::load_config("/nieistniejaca/sciezka/abc.hk");
        assert(cfg.osname == "debian");
        assert(cfg.sysroot_path == "/");
        std::cout << "[PASS] Test 7: wartosci domyslne przy braku pliku\n";
    }

    /* Test 8: load_config -- wczytanie pliku tymczasowego .hk. */
    {
        const char* tmp = "/tmp/deb-ostree-test.hk";
        {
            std::ofstream f(tmp);
            f << "[sysroot]\n";
            f << "-> path => /mnt/sysroot\n";
            f << "\n";
            f << "[system]\n";
            f << "-> osname => my-debian\n";
        }
        auto cfg = debostree::state::load_config(tmp);
        assert(cfg.sysroot_path == "/mnt/sysroot");
        assert(cfg.osname == "my-debian");
        std::remove(tmp);
        std::cout << "[PASS] Test 8: load_config z pliku .hk\n";
    }

    std::cout << "\nWszystkie testy przeszly pomyslnie.\n";
    return 0;
}
