#include "../cmd/compress_util.h"

#include <zlib.h>
#include <lzma.h>
#include <zstd.h>

#include <stdexcept>
#include <fstream>
#include <cstring>

namespace debostree::compress {

Format detect_format(const std::vector<uint8_t>& data) {
    if (data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b)
        return Format::Gzip; /* magic gzip: 1f 8b */

    if (data.size() >= 6 &&
        data[0] == 0xFD && data[1] == 0x37 && data[2] == 0x7A &&
        data[3] == 0x58 && data[4] == 0x5A && data[5] == 0x00)
        return Format::Xz; /* magic xz: FD 37 7A 58 5A 00 */

    if (data.size() >= 4 &&
        data[0] == 0x28 && data[1] == 0xB5 && data[2] == 0x2F && data[3] == 0xFD)
        return Format::Zstd; /* magic zstd: 28 B5 2F FD */

    return Format::None;
}

std::vector<uint8_t> decompress_gzip(const std::vector<uint8_t>& input) {
    z_stream strm{};
    /* windowBits = 15 + 16 wlacza automatyczne wykrywanie naglowka gzip
     * (a nie surowego deflate) -- standardowy trik zlib dla plikow .gz. */
    if (inflateInit2(&strm, 15 + 16) != Z_OK)
        throw std::runtime_error("compress::decompress_gzip: inflateInit2 nie powiodlo sie");

    std::vector<uint8_t> output;
    output.resize(input.size() * 4 + 4096);

    strm.next_in = const_cast<uint8_t*>(input.data());
    strm.avail_in = static_cast<uInt>(input.size());

    size_t total_out = 0;
    int ret;
    do {
        if (total_out == output.size()) output.resize(output.size() * 2);

        strm.next_out = output.data() + total_out;
        strm.avail_out = static_cast<uInt>(output.size() - total_out);

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            throw std::runtime_error(
                "compress::decompress_gzip: inflate() blad (" + std::to_string(ret) + ")");
        }
        total_out = output.size() - strm.avail_out;
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    output.resize(total_out);
    return output;
}

std::vector<uint8_t> decompress_xz(const std::vector<uint8_t>& input) {
    lzma_stream strm = LZMA_STREAM_INIT;

    /* UINT64_MAX jako memlimit -- nie ograniczamy pamieci, indeksy apt i
     * archiwa .deb sa malych/srednich rozmiarow (typowo < 100MB). */
    lzma_ret init_ret = lzma_stream_decoder(&strm, UINT64_MAX, 0);
    if (init_ret != LZMA_OK)
        throw std::runtime_error(
            "compress::decompress_xz: lzma_stream_decoder() blad (" + std::to_string(init_ret) + ")");

    std::vector<uint8_t> output;
    output.resize(input.size() * 4 + 4096);

    strm.next_in = input.data();
    strm.avail_in = input.size();

    size_t total_out = 0;
    lzma_ret ret;
    do {
        if (total_out == output.size()) output.resize(output.size() * 2);

        strm.next_out = output.data() + total_out;
        strm.avail_out = output.size() - total_out;

        ret = lzma_code(&strm, LZMA_FINISH);
        if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
            lzma_end(&strm);
            throw std::runtime_error(
                "compress::decompress_xz: lzma_code() blad (" + std::to_string(ret) + ")");
        }
        total_out = output.size() - strm.avail_out;
    } while (ret != LZMA_STREAM_END);

    lzma_end(&strm);
    output.resize(total_out);
    return output;
}

std::vector<uint8_t> decompress_zstd(const std::vector<uint8_t>& input) {
    unsigned long long const expected_size =
        ZSTD_getFrameContentSize(input.data(), input.size());

    if (expected_size == ZSTD_CONTENTSIZE_ERROR)
        throw std::runtime_error("compress::decompress_zstd: nieprawidlowa ramka zstd");

    std::vector<uint8_t> output;
    if (expected_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        output.resize(input.size() * 8 + 65536);
    } else {
        output.resize(static_cast<size_t>(expected_size));
    }

    size_t decompressed_size = ZSTD_decompress(
        output.data(), output.size(), input.data(), input.size());

    if (ZSTD_isError(decompressed_size))
        throw std::runtime_error(
            std::string("compress::decompress_zstd: ") + ZSTD_getErrorName(decompressed_size));

    output.resize(decompressed_size);
    return output;
}

std::vector<uint8_t> decompress_auto(const std::vector<uint8_t>& input) {
    switch (detect_format(input)) {
        case Format::Gzip: return decompress_gzip(input);
        case Format::Xz:   return decompress_xz(input);
        case Format::Zstd: return decompress_zstd(input);
        case Format::None: return input;
    }
    return input;
}

void decompress_file_auto(const std::string& input_path, const std::string& output_path) {
    std::ifstream in(input_path, std::ios::binary);
    if (!in.is_open())
        throw std::runtime_error("compress::decompress_file_auto: nie mozna otworzyc " + input_path);

    std::vector<uint8_t> input(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::vector<uint8_t> output = decompress_auto(input);

    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("compress::decompress_file_auto: nie mozna zapisac " + output_path);
    out.write(reinterpret_cast<const char*>(output.data()),
              static_cast<std::streamsize>(output.size()));
}

} // namespace debostree::compress
