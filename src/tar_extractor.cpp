#include "../cmd/tar_extractor.h"

#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace debostree::tarball {

namespace {

constexpr size_t BLOCK_SIZE = 512;

uint64_t parse_octal(const char* field, size_t len) {
    /* GNU base-256 dla duzych wartosci: pierwszy bajt ma bit 0x80 ustawiony. */
    if (len > 0 && (static_cast<unsigned char>(field[0]) & 0x80)) {
        uint64_t value = 0;
        for (size_t i = 1; i < len; ++i) {
            value = (value << 8) | static_cast<unsigned char>(field[i]);
        }
        return value;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < len && field[i] != '\0' && field[i] != ' '; ++i) {
        if (field[i] < '0' || field[i] > '7') break;
        value = value * 8 + static_cast<uint64_t>(field[i] - '0');
    }
    return value;
}

std::string parse_string_field(const char* field, size_t len) {
    size_t actual_len = 0;
    while (actual_len < len && field[actual_len] != '\0') ++actual_len;
    return std::string(field, actual_len);
}

/* Parsuje extended header PAX (sekwencja "<len> <key>=<value>\n" wpisow)
 * i zwraca mape klucz->wartosc. */
std::map<std::string, std::string> parse_pax_header(const uint8_t* data, size_t size) {
    std::map<std::string, std::string> result;
    size_t pos = 0;

    while (pos < size) {
        size_t line_start = pos;
        while (pos < size && data[pos] != ' ') ++pos;
        if (pos >= size) break;

        std::string len_str(reinterpret_cast<const char*>(data + line_start), pos - line_start);
        size_t record_len = std::strtoul(len_str.c_str(), nullptr, 10);
        if (record_len == 0 || line_start + record_len > size) break;

        size_t kv_start = pos + 1;
        size_t kv_end = line_start + record_len - 1; /* -1 bo rekord konczy sie '\n' */

        std::string kv(reinterpret_cast<const char*>(data + kv_start), kv_end - kv_start);
        auto eq = kv.find('=');
        if (eq != std::string::npos) {
            result[kv.substr(0, eq)] = kv.substr(eq + 1);
        }

        pos = line_start + record_len;
    }
    return result;
}

/* Iteruje po wpisach tar wywolujac callback(entry, file_data) dla kazdego
 * wpisu -- wspolny szkielet uzywany przez extract_to_directory,
 * extract_single_file i list_entries. */
template <typename Callback>
void iterate_entries(const std::vector<uint8_t>& tar_data, Callback callback) {
    size_t pos = 0;
    std::string pending_long_name;
    std::string pending_long_link;
    std::map<std::string, std::string> pending_pax;

    while (pos + BLOCK_SIZE <= tar_data.size()) {
        const char* header = reinterpret_cast<const char*>(tar_data.data() + pos);

        bool all_zero = true;
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            if (header[i] != '\0') { all_zero = false; break; }
        }
        if (all_zero) break;

        char typeflag = header[156];
        uint64_t header_size = parse_octal(header + 124, 12);
        size_t data_start = pos + BLOCK_SIZE;
        size_t padded_size = ((header_size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;

        if (typeflag == 'L') {
            size_t len = strnlen(reinterpret_cast<const char*>(tar_data.data() + data_start), header_size);
            pending_long_name = std::string(
                reinterpret_cast<const char*>(tar_data.data() + data_start), len);
            pos = data_start + padded_size;
            continue;
        }
        if (typeflag == 'K') {
            size_t len = strnlen(reinterpret_cast<const char*>(tar_data.data() + data_start), header_size);
            pending_long_link = std::string(
                reinterpret_cast<const char*>(tar_data.data() + data_start), len);
            pos = data_start + padded_size;
            continue;
        }
        if (typeflag == 'x' || typeflag == 'g') {
            pending_pax = parse_pax_header(tar_data.data() + data_start, header_size);
            pos = data_start + padded_size;
            continue;
        }

        Entry entry;
        entry.size = header_size;
        entry.mode = static_cast<uint32_t>(parse_octal(header + 100, 8));
        entry.uid  = static_cast<uint32_t>(parse_octal(header + 108, 8));
        entry.gid  = static_cast<uint32_t>(parse_octal(header + 116, 8));
        entry.type = typeflag;

        std::string name = parse_string_field(header, 100);
        std::string prefix = parse_string_field(header + 345, 155);
        if (!prefix.empty()) name = prefix + "/" + name;

        if (!pending_long_name.empty()) { name = pending_long_name; pending_long_name.clear(); }
        if (auto it = pending_pax.find("path"); it != pending_pax.end()) name = it->second;
        if (auto it = pending_pax.find("size"); it != pending_pax.end())
            entry.size = std::strtoull(it->second.c_str(), nullptr, 10);

        entry.path = name;
        entry.link_target = parse_string_field(header + 157, 100);
        if (!pending_long_link.empty()) { entry.link_target = pending_long_link; pending_long_link.clear(); }

        const uint8_t* file_data = (entry.size > 0) ? (tar_data.data() + data_start) : nullptr;

        callback(entry, file_data);

        pending_pax.clear();
        /* Przeskakujemy blok danych wedlug ORYGINALNEGO rozmiaru z naglowka
         * (header_size), bo dane w archiwum faktycznie zajmuja tyle miejsca
         * niezaleznie od tego czy PAX nadpisal "size" w entry. */
        pos = data_start + padded_size;
    }
}

} // namespace

void extract_to_directory(const std::vector<uint8_t>& tar_data, const std::string& dest_dir) {
    iterate_entries(tar_data, [&](const Entry& entry, const uint8_t* file_data) {
        if (entry.path.empty() || entry.path == "./") return;

        fs::path target = fs::path(dest_dir) / entry.path;
        std::error_code ec;

        switch (entry.type) {
            case '5': /* katalog */
                fs::create_directories(target);
                break;

            case '2': /* symlink */
                fs::create_directories(target.parent_path());
                fs::remove(target, ec); /* moze juz istniec z poprzedniej warstwy */
                fs::create_symlink(entry.link_target, target, ec);
                break;

            case '1': { /* hardlink */
                fs::create_directories(target.parent_path());
                fs::path link_target_path = fs::path(dest_dir) / entry.link_target;
                fs::remove(target, ec);
                fs::create_hard_link(link_target_path, target, ec);
                break;
            }

            case '0':
            case '\0':
            default: { /* plik regularny (typeflag '0' lub brak = stary format) */
                fs::create_directories(target.parent_path());
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                    throw std::runtime_error("tarball::extract_to_directory: nie mozna zapisac " + target.string());
                if (file_data && entry.size > 0) {
                    out.write(reinterpret_cast<const char*>(file_data),
                             static_cast<std::streamsize>(entry.size));
                }
                out.close();
                ::chmod(target.c_str(), entry.mode);
                break;
            }
        }
    });
}

std::vector<uint8_t> extract_single_file(const std::vector<uint8_t>& tar_data,
                                         const std::string& file_path) {
    std::vector<uint8_t> result;
    iterate_entries(tar_data, [&](const Entry& entry, const uint8_t* file_data) {
        if (result.empty() && entry.type == '0' && entry.path == file_path && file_data) {
            result.assign(file_data, file_data + entry.size);
        }
    });
    return result;
}

std::vector<Entry> list_entries(const std::vector<uint8_t>& tar_data) {
    std::vector<Entry> entries;
    iterate_entries(tar_data, [&](const Entry& entry, const uint8_t* /*file_data*/) {
        entries.push_back(entry);
    });
    return entries;
}

} // namespace debostree::tarball
