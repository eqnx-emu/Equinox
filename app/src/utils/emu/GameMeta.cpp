#include "emu/GameMeta.hpp"

#include <jansson.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "emu/CoverStore.hpp"
#include "emu/PathUtf8.hpp"

namespace fs = std::filesystem;

namespace emu {

namespace {

const char* partName(PartState state) {
    switch (state) {
        case PartState::Ok: return "ok";
        case PartState::Missing: return "missing";
        case PartState::Skipped: return "skipped";
        default: return nullptr;
    }
}

PartState partFrom(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    if (!value || !json_is_string(value)) return PartState::Unknown;
    const std::string name = json_string_value(value);
    if (name == "ok") return PartState::Ok;
    if (name == "missing") return PartState::Missing;
    if (name == "skipped") return PartState::Skipped;
    return PartState::Unknown;
}

std::string stringField(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return value && json_is_string(value) ? json_string_value(value) : "";
}

json_int_t integerField(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return value && json_is_integer(value) ? json_integer_value(value) : 0;
}

void setPart(json_t* object, const char* key, PartState state) {
    if (const char* name = partName(state)) json_object_set_new(object, key, json_string(name));
}

}  // namespace

std::string metaPath(const ConsolePaths& paths, const std::string& title) {
    return paths.meta + "/" + title + ".json";
}

bool loadMeta(const std::string& path, GameMeta& out) {
    std::ifstream file(fromUtf8(path), std::ios::binary);
    if (!file) return false;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    json_error_t error;
    json_t* root = json_loadb(text.data(), text.size(), 0, &error);
    if (!root) return false;
    const bool ok = integerField(root, "v") == 1 && metaFromJson(root, out);
    json_decref(root);
    return ok;
}

bool metaFromJson(const json_t* object, GameMeta& out) {
    if (!json_is_object(object)) return false;
    GameMeta meta;
    const json_int_t size = integerField(object, "size");
    meta.size = size < 0 ? 0 : static_cast<uint64_t>(size);
    meta.mtime = integerField(object, "mtime");
    const json_t* identified = json_object_get(object, "identified");
    meta.identified = identified && json_is_true(identified);
    meta.crc = static_cast<uint32_t>(std::strtoul(stringField(object, "crc").c_str(), nullptr, 16));
    meta.canonical = stringField(object, "canonical");
    meta.snap = partFrom(object, "snap");
    meta.title = partFrom(object, "title");
    meta.synopsis = partFrom(object, "synopsis");
    meta.synopsisText = stringField(object, "synopsisText");
    meta.synopsisLang = stringField(object, "synopsisLang");
    out = std::move(meta);
    return true;
}

json_t* metaToJson(const GameMeta& meta) {
    json_t* root = json_object();
    if (!root) return nullptr;
    json_object_set_new(root, "size", json_integer(static_cast<json_int_t>(meta.size)));
    json_object_set_new(root, "mtime", json_integer(meta.mtime));
    json_object_set_new(root, "identified", json_boolean(meta.identified));
    char crc[16];
    std::snprintf(crc, sizeof(crc), "%08x", meta.crc);
    json_object_set_new(root, "crc", json_string(crc));
    json_object_set_new(root, "canonical", json_string(meta.canonical.c_str()));
    setPart(root, "snap", meta.snap);
    setPart(root, "title", meta.title);
    setPart(root, "synopsis", meta.synopsis);
    json_object_set_new(root, "synopsisText", json_string(meta.synopsisText.c_str()));
    json_object_set_new(root, "synopsisLang", json_string(meta.synopsisLang.c_str()));
    return root;
}

bool saveMeta(const std::string& path, const GameMeta& meta) {
    json_t* root = metaToJson(meta);
    if (!root) return false;
    json_object_set_new(root, "v", json_integer(1));
    char* dumped = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (!dumped) return false;
    const std::string text = dumped;
    std::free(dumped);

    std::error_code ec;
    fs::create_directories(fromUtf8(path).parent_path(), ec);
    const std::string part = path + ".part";
    {
        std::ofstream file(fromUtf8(part), std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << text;
        file.close();
        if (file.fail()) {
            fs::remove(fromUtf8(part), ec);
            return false;
        }
    }
    if (!replaceFile(part, path)) {
        fs::remove(fromUtf8(part), ec);
        return false;
    }
    return true;
}

bool romStat(const std::string& romPath, uint64_t& size, int64_t& mtime) {
    std::error_code ec;
    const fs::path path = fromUtf8(romPath);
    const auto bytes = fs::file_size(path, ec);
    if (ec) return false;
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) return false;
    size = bytes;
    mtime = std::chrono::duration_cast<std::chrono::seconds>(stamp.time_since_epoch()).count();
    return true;
}

bool metaFresh(const GameMeta& meta, uint64_t size, int64_t mtime) {
    return meta.size == size && meta.mtime == mtime;
}

}  // namespace emu
