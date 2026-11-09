#include "emu/SevenZip.hpp"

#include "7z.h"
#include "7zCrc.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

namespace sevenz {

namespace {

constexpr size_t kLookSize = 64 * 1024;

void* szAlloc(ISzAllocPtr, size_t size) { return size ? std::malloc(size) : nullptr; }
void szFree(ISzAllocPtr, void* address) { std::free(address); }
const ISzAlloc kAlloc = {szAlloc, szFree};

// vt comes first: the decoder hands the vtable pointer back as the stream.
struct MemoryStream {
    ISeekInStream vt;
    const Byte* data;
    size_t size;
    size_t pos;
};

MemoryStream* self(ISeekInStreamPtr p) {
    return const_cast<MemoryStream*>(reinterpret_cast<const MemoryStream*>(p));
}

SRes memoryRead(ISeekInStreamPtr p, void* buf, size_t* size) {
    MemoryStream* s = self(p);
    const size_t left = s->size - s->pos;
    if (*size > left) *size = left;
    if (*size) std::memcpy(buf, s->data + s->pos, *size);
    s->pos += *size;
    return SZ_OK;
}

SRes memorySeek(ISeekInStreamPtr p, Int64* pos, ESzSeek origin) {
    MemoryStream* s = self(p);
    Int64 base = 0;
    if (origin == SZ_SEEK_CUR) base = static_cast<Int64>(s->pos);
    if (origin == SZ_SEEK_END) base = static_cast<Int64>(s->size);
    const Int64 target = base + *pos;
    if (target < 0 || target > static_cast<Int64>(s->size)) return SZ_ERROR_PARAM;
    s->pos = static_cast<size_t>(target);
    *pos = target;
    return SZ_OK;
}

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string nameOf(const CSzArEx& db, size_t index, std::vector<UInt16>& scratch) {
    const size_t length = SzArEx_GetFileNameUtf16(&db, index, nullptr);
    scratch.resize(length);
    SzArEx_GetFileNameUtf16(&db, index, scratch.data());
    std::string out;
    for (size_t i = 0; i + 1 < length; ++i) {
        uint32_t c = scratch[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 2 < length && scratch[i + 1] >= 0xDC00 &&
            scratch[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (scratch[i + 1] - 0xDC00);
            ++i;
        }
        appendUtf8(out, c);
    }
    return out;
}

Status findIn(CSzArEx& db, CLookToRead2& look,
              const std::function<bool(const std::string&)>& wanted, uint64_t maxFile,
              uint64_t maxBlock, bool extract, Found& out) {
    std::vector<UInt16> scratch;
    for (UInt32 i = 0; i < db.NumFiles; ++i) {
        if (SzArEx_IsDir(&db, i)) continue;
        std::string name = nameOf(db, i, scratch);
        if (!wanted(name)) continue;

        const uint64_t size = SzArEx_GetFileSize(&db, i);
        if (size > maxFile) return Status::TooBig;
        out.name = std::move(name);
        out.size = size;
        out.hasCrc = SzBitWithVals_Check(&db.CRCs, i);
        out.crc = out.hasCrc ? db.CRCs.Vals[i] : 0;
        if (!extract) return Status::Ok;

        const UInt32 folder = db.FileToFolder[i];
        if (folder != static_cast<UInt32>(-1) && SzAr_GetFolderUnpackSize(&db.db, folder) > maxBlock)
            return Status::TooBig;

        UInt32 blockIndex = 0xFFFFFFFF;
        Byte* block = nullptr;
        size_t blockSize = 0, offset = 0, processed = 0;
        const SRes res = SzArEx_Extract(&db, &look.vt, i, &blockIndex, &block, &blockSize, &offset,
                                        &processed, &kAlloc, &kAlloc);
        Status status = Status::Ok;
        if (res == SZ_ERROR_MEM) {
            status = Status::TooBig;
        } else if (res != SZ_OK) {
            status = Status::Damaged;
        } else {
            try {
                out.bytes.assign(block + offset, block + offset + processed);
            } catch (const std::bad_alloc&) {
                status = Status::TooBig;
            }
        }
        ISzAlloc_Free(&kAlloc, block);
        return status;
    }
    return Status::NoMatch;
}

}  // namespace

Status firstMatching(const uint8_t* archive, size_t size,
                     const std::function<bool(const std::string&)>& wanted, uint64_t maxFile,
                     uint64_t maxBlock, bool extract, Found& out) {
    static std::once_flag tables;
    std::call_once(tables, [] { CrcGenerateTable(); });

    MemoryStream stream{};
    stream.vt.Read = memoryRead;
    stream.vt.Seek = memorySeek;
    stream.data = archive;
    stream.size = size;

    CLookToRead2 look{};
    LookToRead2_CreateVTable(&look, False);
    look.buf = static_cast<Byte*>(std::malloc(kLookSize));
    if (!look.buf) return Status::TooBig;
    look.bufSize = kLookSize;
    look.realStream = &stream.vt;
    LookToRead2_INIT(&look);

    CSzArEx db;
    SzArEx_Init(&db);
    const SRes opened = SzArEx_Open(&db, &look.vt, &kAlloc, &kAlloc);
    Status status = opened == SZ_ERROR_MEM ? Status::TooBig : Status::Damaged;
    if (opened == SZ_OK) status = findIn(db, look, wanted, maxFile, maxBlock, extract, out);
    SzArEx_Free(&db, &kAlloc);
    std::free(look.buf);
    return status;
}

}  // namespace sevenz
