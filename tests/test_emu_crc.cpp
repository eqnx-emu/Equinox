#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <zlib.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "emu/Console.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/RomCrc.hpp"

namespace fs = std::filesystem;

namespace {

fs::path tempDir() {
    fs::path d = fs::temp_directory_path() /
                 ("equinox_crc_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(d);
    return d;
}

void put(const fs::path& p, const std::string& body) {
    std::ofstream f(p, std::ios::binary);
    f << body;
}

void le16(std::string& s, uint16_t v) {
    s += static_cast<char>(v & 0xFF);
    s += static_cast<char>(v >> 8);
}
void le32(std::string& s, uint32_t v) {
    le16(s, static_cast<uint16_t>(v & 0xFFFF));
    le16(s, static_cast<uint16_t>(v >> 16));
}

uint32_t crcOf(const std::string& s) {
    return static_cast<uint32_t>(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(s.data()),
                                       static_cast<uInt>(s.size())));
}

// Entries are stored (method 0), or raw-deflated when `deflate` is set.
std::string makeZip(const std::vector<std::pair<std::string, std::string>>& entries,
                    bool deflate = false) {
    std::string out, cd;
    for (const auto& [name, data] : entries) {
        std::string body = data;
        uint16_t method = 0;
        if (deflate) {
            z_stream zs{};
            deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            body.assign(deflateBound(&zs, static_cast<uLong>(data.size())), '\0');
            zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
            zs.avail_in = static_cast<uInt>(data.size());
            zs.next_out = reinterpret_cast<Bytef*>(body.data());
            zs.avail_out = static_cast<uInt>(body.size());
            ::deflate(&zs, Z_FINISH);
            body.resize(zs.total_out);
            deflateEnd(&zs);
            method = 8;
        }
        uint32_t offset = static_cast<uint32_t>(out.size());
        uint32_t crc = crcOf(data);
        le32(out, 0x04034b50);
        le16(out, 20); le16(out, 0); le16(out, method); le16(out, 0); le16(out, 0);
        le32(out, crc);
        le32(out, static_cast<uint32_t>(body.size()));
        le32(out, static_cast<uint32_t>(data.size()));
        le16(out, static_cast<uint16_t>(name.size())); le16(out, 0);
        out += name;
        out += body;

        le32(cd, 0x02014b50);
        le16(cd, 20); le16(cd, 20); le16(cd, 0); le16(cd, method); le16(cd, 0); le16(cd, 0);
        le32(cd, crc);
        le32(cd, static_cast<uint32_t>(body.size()));
        le32(cd, static_cast<uint32_t>(data.size()));
        le16(cd, static_cast<uint16_t>(name.size())); le16(cd, 0); le16(cd, 0);
        le16(cd, 0); le16(cd, 0); le32(cd, 0);
        le32(cd, offset);
        cd += name;
    }
    uint32_t cdOffset = static_cast<uint32_t>(out.size());
    out += cd;
    le32(out, 0x06054b50);
    le16(out, 0); le16(out, 0);
    le16(out, static_cast<uint16_t>(entries.size()));
    le16(out, static_cast<uint16_t>(entries.size()));
    le32(out, static_cast<uint32_t>(cd.size()));
    le32(out, cdOffset);
    le16(out, 0);
    return out;
}

const emu::Console& snes() { return *emu::findConsole("snes"); }

bool crcFor(const fs::path& p, uint32_t& crc, std::string& err) {
    return emu::romCrc32(emu::toUtf8(p), snes(), crc, err);
}

const std::string kBody(1024, 'x');
const std::string kHeadered = std::string(512, '\xAA') + kBody;

}  // namespace

TEST_CASE("plain rom") {
    fs::path d = tempDir();
    put(d / "a.sfc", "123456789");
    uint32_t crc = 0;
    std::string err;
    CHECK(crcFor(d / "a.sfc", crc, err));
    CHECK(crc == 0xCBF43926u);
    fs::remove_all(d);
}

TEST_CASE("copier header is skipped") {
    fs::path d = tempDir();
    put(d / "h.smc", kHeadered);
    put(d / "p.sfc", kBody);
    uint32_t withHeader = 0, plain = 0;
    std::string err;
    CHECK(crcFor(d / "h.smc", withHeader, err));
    CHECK(crcFor(d / "p.sfc", plain, err));
    CHECK(withHeader == plain);
    CHECK(withHeader != crcOf(kHeadered));
    fs::remove_all(d);
}

TEST_CASE("zip uses the rom entry") {
    fs::path d = tempDir();
    put(d / "z.zip", makeZip({{"readme.txt", "hello"}, {"game.sfc", "123456789"}}));
    uint32_t crc = 0;
    std::string err;
    CHECK(crcFor(d / "z.zip", crc, err));
    CHECK(crc == 0xCBF43926u);
    fs::remove_all(d);
}

TEST_CASE("zip with copier header") {
    fs::path d = tempDir();
    uint32_t expected = crcOf(kBody);
    for (bool deflated : {false, true}) {
        CAPTURE(deflated);
        put(d / "z.zip", makeZip({{"game.smc", kHeadered}}, deflated));
        uint32_t crc = 0;
        std::string err;
        CHECK(crcFor(d / "z.zip", crc, err));
        CHECK(crc == expected);
    }
    fs::remove_all(d);
}

TEST_CASE("zip without a rom") {
    fs::path d = tempDir();
    put(d / "z.zip", makeZip({{"readme.txt", "hello"}}));
    uint32_t crc = 0;
    std::string err;
    CHECK_FALSE(crcFor(d / "z.zip", crc, err));
    CHECK_FALSE(err.empty());
    fs::remove_all(d);
}

TEST_CASE("missing or broken file") {
    fs::path d = tempDir();
    uint32_t crc = 0;
    std::string err;
    CHECK_FALSE(crcFor(d / "nope.sfc", crc, err));
    CHECK_FALSE(err.empty());

    std::string zip = makeZip({{"game.sfc", "123456789"}});
    put(d / "cut.zip", zip.substr(0, zip.size() - 10));
    err.clear();
    CHECK_FALSE(crcFor(d / "cut.zip", crc, err));
    CHECK_FALSE(err.empty());

    put(d / "tiny.zip", "PK");
    CHECK_FALSE(crcFor(d / "tiny.zip", crc, err));

    // Truncated deflate data behind an intact directory.
    zip = makeZip({{"game.smc", kHeadered}}, true);
    size_t cdPos = zip.find("PK\x01\x02");
    put(d / "bad.zip", zip.substr(0, 40) + zip.substr(cdPos));
    CHECK_FALSE(crcFor(d / "bad.zip", crc, err));
    fs::remove_all(d);
}
