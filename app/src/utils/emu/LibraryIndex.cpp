#include "emu/LibraryIndex.hpp"

#include <jansson.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

#include "emu/CoverStore.hpp"
#include "emu/PathUtf8.hpp"

namespace fs = std::filesystem;

namespace emu {

namespace {

constexpr int kIndexVersion = 1;

std::string text(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return value && json_is_string(value) ? json_string_value(value) : "";
}

bool flag(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return value && json_is_true(value);
}

}  // namespace

std::string libraryIndexPath(const ConsolePaths& paths) {
    return paths.emulator + "/library-index.json";
}

bool loadLibraryIndex(const ConsolePaths& paths, const std::string& appVersion,
                      std::vector<LibraryGame>& out) {
    std::ifstream file(fromUtf8(libraryIndexPath(paths)), std::ios::binary);
    if (!file) return false;
    const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    json_error_t error;
    json_t* root = json_loadb(body.data(), body.size(), 0, &error);
    if (!root) return false;

    std::vector<LibraryGame> games;
    const json_t* version = json_object_get(root, "v");
    const json_t* list = json_object_get(root, "games");
    if (json_is_object(root) && version && json_is_integer(version) &&
        json_integer_value(version) == kIndexVersion && text(root, "app") == appVersion &&
        list && json_is_array(list)) {
        games.reserve(json_array_size(list));
        size_t i;
        const json_t* entry;
        json_array_foreach(list, i, entry) {
            const std::string name = text(entry, "file");
            if (name.empty()) continue;
            LibraryGame game;
            game.rom = romEntryFor(paths.roms, name);
            game.hasCover = flag(entry, "cover");
            game.missed = flag(entry, "missed");
            game.snapPath = text(entry, "snap");
            game.titlePath = text(entry, "shot");
            const json_t* meta = json_object_get(entry, "meta");
            if (meta && metaFromJson(meta, game.meta)) game.hasData = true;
            games.push_back(std::move(game));
        }
    }
    json_decref(root);
    if (games.empty()) return false;
    out = std::move(games);
    return true;
}

bool saveLibraryIndex(const ConsolePaths& paths, const std::string& appVersion,
                      const std::vector<LibraryGame>& games) {
    json_t* list = json_array();
    if (!list) return false;
    for (const LibraryGame& game : games) {
        json_t* entry = json_object();
        if (!entry) continue;
        json_object_set_new(entry, "file", json_string(game.rom.fileName.c_str()));
        json_object_set_new(entry, "cover", json_boolean(game.hasCover));
        json_object_set_new(entry, "missed", json_boolean(game.missed));
        json_object_set_new(entry, "snap", json_string(game.snapPath.c_str()));
        json_object_set_new(entry, "shot", json_string(game.titlePath.c_str()));
        if (game.hasData) json_object_set_new(entry, "meta", metaToJson(game.meta));
        json_array_append_new(list, entry);
    }
    json_t* root = json_object();
    if (!root) {
        json_decref(list);
        return false;
    }
    json_object_set_new(root, "v", json_integer(kIndexVersion));
    json_object_set_new(root, "app", json_string(appVersion.c_str()));
    json_object_set_new(root, "games", list);
    char* dumped = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (!dumped) return false;
    const std::string body = dumped;
    std::free(dumped);

    static std::mutex writing;
    std::lock_guard<std::mutex> lock(writing);
    const std::string path = libraryIndexPath(paths);
    const std::string part = path + ".part";
    std::error_code ec;
    {
        std::ofstream file(fromUtf8(part), std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << body;
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

LibraryDiff diffLibrary(std::vector<std::string> known, std::vector<std::string> onCard) {
    std::sort(known.begin(), known.end());
    std::sort(onCard.begin(), onCard.end());
    LibraryDiff diff;
    std::set_difference(onCard.begin(), onCard.end(), known.begin(), known.end(),
                        std::back_inserter(diff.added));
    std::set_difference(known.begin(), known.end(), onCard.begin(), onCard.end(),
                        std::back_inserter(diff.removed));
    return diff;
}

}  // namespace emu
