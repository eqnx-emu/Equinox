#include "emu/RomCrc.hpp"
#include "emu/PathUtf8.hpp"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace emu {

namespace {

constexpr size_t kChunk = 64 * 1024;
constexpr uint64_t kCopierHeader = 512;

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool hasCopierHeader(uint64_t size) { return size % 1024 == kCopierHeader; }

// Feeds bytes to crc32, dropping the first `skip` of them.
struct CrcSink {
    uLong value = crc32(0L, Z_NULL, 0);
    uint64_t skip = 0;

    void feed(const unsigned char* data, size_t len) {
        size_t drop = static_cast<size_t>(std::min<uint64_t>(skip, len));
        skip -= drop;
        if (len > drop) value = crc32(value, data + drop, static_cast<uInt>(len - drop));
    }
};

uint16_t rd16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool readAt(std::ifstream& f, uint64_t offset, void* buf, size_t len) {
    f.clear();
    f.seekg(static_cast<std::streamoff>(offset));
    f.read(static_cast<char*>(buf), static_cast<std::streamsize>(len));
    return f.gcount() == static_cast<std::streamsize>(len);
}

bool acceptedExtension(const std::string& name, const Console& console) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = lower(name.substr(dot));
    if (ext == ".zip") return false;
    return std::find(console.extensions.begin(), console.extensions.end(), ext) !=
           console.extensions.end();
}

struct ZipEntry {
    uint16_t method = 0;
    uint32_t crc = 0, compressed = 0, size = 0, localOffset = 0;
};

bool findZipRomEntry(std::ifstream& f, uint64_t fileSize, const Console& console, ZipEntry& out,
                     std::string& error) {
    constexpr size_t kEocd = 22;
    if (fileSize < kEocd) {
        error = "not a zip file";
        return false;
    }
    size_t tailLen = static_cast<size_t>(std::min<uint64_t>(fileSize, kEocd + 0xFFFF));
    std::vector<unsigned char> tail(tailLen);
    if (!readAt(f, fileSize - tailLen, tail.data(), tailLen)) {
        error = "cannot read zip";
        return false;
    }
    size_t eocd = std::string::npos;
    for (size_t i = tailLen - kEocd + 1; i-- > 0;) {
        if (rd32(&tail[i]) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "zip directory not found";
        return false;
    }
    uint16_t count = rd16(&tail[eocd + 10]);
    uint32_t cdSize = rd32(&tail[eocd + 12]);
    uint32_t cdOffset = rd32(&tail[eocd + 16]);
    if (cdOffset == 0xFFFFFFFFu || count == 0xFFFF) {
        error = "zip64 is not supported";
        return false;
    }
    if (static_cast<uint64_t>(cdOffset) + cdSize > fileSize) {
        error = "zip is truncated";
        return false;
    }
    std::vector<unsigned char> cd(cdSize);
    if (cdSize > 0 && !readAt(f, cdOffset, cd.data(), cdSize)) {
        error = "cannot read zip directory";
        return false;
    }
    size_t pos = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > cd.size() || rd32(&cd[pos]) != 0x02014b50) {
            error = "zip directory is damaged";
            return false;
        }
        uint16_t nameLen = rd16(&cd[pos + 28]);
        uint16_t extraLen = rd16(&cd[pos + 30]);
        uint16_t commentLen = rd16(&cd[pos + 32]);
        size_t next = pos + 46 + nameLen + extraLen + commentLen;
        if (next > cd.size()) {
            error = "zip directory is damaged";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(&cd[pos + 46]), nameLen);
        if (acceptedExtension(name, console)) {
            out.method = rd16(&cd[pos + 10]);
            out.crc = rd32(&cd[pos + 16]);
            out.compressed = rd32(&cd[pos + 20]);
            out.size = rd32(&cd[pos + 24]);
            out.localOffset = rd32(&cd[pos + 42]);
            return true;
        }
        pos = next;
    }
    error = "no rom inside the zip";
    return false;
}

bool crcOfStream(std::ifstream& f, uint64_t offset, uint64_t length, CrcSink& sink,
                 std::string& error) {
    std::vector<unsigned char> buf(kChunk);
    f.clear();
    f.seekg(static_cast<std::streamoff>(offset));
    while (length > 0) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(length, kChunk));
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(want));
        size_t got = static_cast<size_t>(f.gcount());
        if (got != want) {
            error = "file is truncated";
            return false;
        }
        sink.feed(buf.data(), got);
        length -= got;
    }
    return true;
}

bool crcOfZipEntry(std::ifstream& f, const ZipEntry& e, uint64_t fileSize, CrcSink& sink,
                   std::string& error) {
    unsigned char lh[30];
    if (!readAt(f, e.localOffset, lh, sizeof lh) || rd32(lh) != 0x04034b50) {
        error = "zip entry is damaged";
        return false;
    }
    uint64_t dataOffset = static_cast<uint64_t>(e.localOffset) + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (dataOffset + e.compressed > fileSize) {
        error = "zip is truncated";
        return false;
    }
    if (e.method == 0) return crcOfStream(f, dataOffset, e.compressed, sink, error);
    if (e.method != 8) {
        error = "unsupported zip compression";
        return false;
    }

    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        error = "cannot start decompression";
        return false;
    }
    std::vector<unsigned char> in(kChunk), out(kChunk);
    uint64_t left = e.compressed;
    f.clear();
    f.seekg(static_cast<std::streamoff>(dataOffset));
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        if (zs.avail_in == 0) {
            size_t want = static_cast<size_t>(std::min<uint64_t>(left, kChunk));
            if (want == 0) break;
            f.read(reinterpret_cast<char*>(in.data()), static_cast<std::streamsize>(want));
            if (static_cast<size_t>(f.gcount()) != want) break;
            left -= want;
            zs.next_in = in.data();
            zs.avail_in = static_cast<uInt>(want);
        }
        zs.next_out = out.data();
        zs.avail_out = static_cast<uInt>(out.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) break;
        sink.feed(out.data(), out.size() - zs.avail_out);
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END) {
        error = "cannot decompress the zip entry";
        return false;
    }
    return true;
}

bool compute(const std::string& path, const Console& console, uint32_t& crc, std::string& error) {
    std::ifstream f(fromUtf8(path), std::ios::binary);
    if (!f) {
        error = "cannot open file";
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff end = f.tellg();
    if (end < 0) {
        error = "cannot read file";
        return false;
    }
    uint64_t fileSize = static_cast<uint64_t>(end);

    size_t dot = path.rfind('.');
    bool isZip = dot != std::string::npos && lower(path.substr(dot)) == ".zip";

    CrcSink sink;
    if (!isZip) {
        if (hasCopierHeader(fileSize)) sink.skip = kCopierHeader;
        if (!crcOfStream(f, 0, fileSize, sink, error)) return false;
        crc = static_cast<uint32_t>(sink.value);
        return true;
    }

    ZipEntry entry;
    if (!findZipRomEntry(f, fileSize, console, entry, error)) return false;
    if (!hasCopierHeader(entry.size)) {
        crc = entry.crc;
        return true;
    }
    sink.skip = kCopierHeader;
    if (!crcOfZipEntry(f, entry, fileSize, sink, error)) return false;
    crc = static_cast<uint32_t>(sink.value);
    return true;
}

}  // namespace

bool romCrc32(const std::string& path, const Console& console, uint32_t& crc, std::string& error) {
    error.clear();
    try {
        return compute(path, console, crc, error);
    } catch (...) {
        error = "unexpected error reading the rom";
        return false;
    }
}

}  // namespace emu
