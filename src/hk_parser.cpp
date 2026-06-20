#include "../cmd/hk_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace debostree::hk {

namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/* Usuwa cudzyslowy otaczajace string cytowany i rozwiazuje podstawowe
 * sekwencje ucieczki (\n \t \r \" \\), zgodnie ze specyfikacja .hk. */
std::string unquoteAndUnescape(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"')
        return raw; /* plain string -- bez zmian */

    std::string inner = raw.substr(1, raw.size() - 2);
    std::string out;
    out.reserve(inner.size());

    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            switch (inner[i + 1]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                default:
                    out += '\\';
                    out += inner[i + 1];
            }
            ++i;
        } else {
            out += inner[i];
        }
    }
    return out;
}

/* Liczy myslniki na poczatku linii (zwraca glebokosc i pozycje '>'). */
int countDashDepth(const std::string& line, size_t& arrowPos) {
    size_t i = 0;
    while (i < line.size() && line[i] == '-') ++i;
    if (i == 0 || i >= line.size() || line[i] != '>') return -1;
    arrowPos = i;
    return static_cast<int>(i);
}

} // namespace

void HkDocument::set(const std::string& section, const std::string& key,
                     const std::string& value) {
    values_[makeFlatKey(section, key)] = value;
}

std::string HkDocument::makeFlatKey(const std::string& section, const std::string& key) {
    return section + "." + key;
}

std::string HkDocument::getOr(const std::string& section, const std::string& key,
                              const std::string& defaultValue) const {
    auto it = values_.find(makeFlatKey(section, key));
    if (it == values_.end()) return defaultValue;
    return it->second;
}

bool HkDocument::has(const std::string& section, const std::string& key) const {
    return values_.find(makeFlatKey(section, key)) != values_.end();
}

HkDocument HkDocument::parse(const std::string& content) {
    HkDocument doc;
    std::istringstream iss(content);
    std::string rawLine;
    std::string currentSection;
    int lineNo = 0;

    while (std::getline(iss, rawLine)) {
        ++lineNo;
        std::string line = trim(rawLine);

        if (line.empty() || line[0] == '!') continue; /* komentarz/pusta linia */

        if (line.front() == '[') {
            if (line.back() != ']')
                throw HkParseError(lineNo, "naglowek sekcji nie jest zamkniety ']'");
            currentSection = trim(line.substr(1, line.size() - 2));
            if (currentSection.empty())
                throw HkParseError(lineNo, "nazwa sekcji nie moze byc pusta");
            continue;
        }

        if (line.front() == '-') {
            if (currentSection.empty())
                throw HkParseError(lineNo, "klucz zdefiniowany przed jakakolwiek sekcja");

            size_t arrowPos = 0;
            int depth = countDashDepth(line, arrowPos);
            if (depth < 0)
                throw HkParseError(lineNo, "oczekiwano '>' po myslnikach");

            /* deb-ostree.hk uzywa tylko glebokosci 1 -- glebsze zagniezdzenie
             * jest parsowane (nie rzucamy bledu), ale traktowane jako klucz
             * "plaski" pod ostatnia sekcja (uproszczenie wystarczajace dla
             * naszego zakresu uzycia; pelne zagniezdzenie wymaga implementacji
             * jak w hackeros-builder/internal/hk). */
            std::string rest = trim(line.substr(arrowPos + 1));
            if (rest.empty())
                throw HkParseError(lineNo, "brak klucza po myslnikach");

            size_t fatArrow = rest.find("=>");
            if (fatArrow == std::string::npos) {
                /* Linia bez "=>" otwiera mape inline -- deb-ostree.hk nie
                 * uzywa tej funkcji, wiec po prostu ja ignorujemy (parser
                 * nie traktuje to jako blad, dla wytrzymalosci na przyszle
                 * rozszerzenia configu). */
                continue;
            }

            std::string key = trim(rest.substr(0, fatArrow));
            std::string rawValue = trim(rest.substr(fatArrow + 2));

            if (key.empty())
                throw HkParseError(lineNo, "brak nazwy klucza przed '=>'");

            /* Usuwamy ewentualne otaczajace cudzyslowy z klucza (klucz
             * literalny), tak jak specyfikacja .hk opisuje dla nazw kluczy. */
            if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
                key = key.substr(1, key.size() - 2);

            std::string value = unquoteAndUnescape(rawValue);
            doc.set(currentSection, key, value);
            continue;
        }

        throw HkParseError(lineNo, "nieoczekiwana linia (oczekiwano '!', '[' lub '-')");
    }

    return doc;
}

HkDocument HkDocument::loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("hk: nie mozna otworzyc " + path);

    std::ostringstream buf;
    buf << f.rdbuf();
    return parse(buf.str());
}

} // namespace debostree::hk
