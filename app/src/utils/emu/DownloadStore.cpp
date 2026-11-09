#include "emu/DownloadStore.hpp"

#include <jansson.h>

#include <filesystem>
#include <memory>
#include <set>

#include "emu/PathUtf8.hpp"
#include "emu/ReplaceFile.hpp"

namespace emu {

namespace fs = std::filesystem;

namespace {

constexpr int kVersion = 1;

struct JsonDeleter {
    void operator()(json_t* j) const { json_decref(j); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

bool persists(DlKind kind) { return kind == DlKind::GameData || kind == DlKind::Media; }

const char* kindName(DlKind kind) { return kind == DlKind::Media ? "media" : "gamedata"; }

const char* stateName(DlState state) {
    switch (state) {
        case DlState::Paused: return "paused";
        case DlState::Failed: return "failed";
        default: return "queued";
    }
}

const char* priorityName(DlPriority p) {
    switch (p) {
        case DlPriority::Interactive: return "interactive";
        case DlPriority::Visible: return "visible";
        case DlPriority::Batch: return "batch";
        case DlPriority::Background: return "background";
        case DlPriority::Large: return "large";
    }
    return "batch";
}

bool parsePriority(const std::string& s, DlPriority& out) {
    for (DlPriority p : {DlPriority::Interactive, DlPriority::Visible, DlPriority::Batch,
                         DlPriority::Background, DlPriority::Large}) {
        if (s == priorityName(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

std::string stringField(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

bool parseTask(const json_t* item, DlTask& out) {
    if (!json_is_object(item)) return false;
    out.id = stringField(item, "id");
    if (out.id.empty()) return false;

    const std::string kind = stringField(item, "kind");
    if (kind == "gamedata") out.kind = DlKind::GameData;
    else if (kind == "media") out.kind = DlKind::Media;
    else return false;

    if (!parsePriority(stringField(item, "priority"), out.priority)) return false;

    const std::string state = stringField(item, "state");
    if (state == "paused") out.state = DlState::Paused;
    else if (state == "failed") out.state = DlState::Failed;
    else out.state = DlState::Queued;

    out.group = stringField(item, "group");
    out.params = stringField(item, "params");
    out.resumable = json_is_true(json_object_get(item, "resumable"));
    out.diskPaused = out.state == DlState::Paused && json_is_true(json_object_get(item, "disk"));
    const json_t* failures = json_object_get(item, "failures");
    out.failures = json_is_integer(failures) ? static_cast<int>(json_integer_value(failures)) : 0;

    const json_t* hosts = json_object_get(item, "hosts");
    if (json_is_array(hosts)) {
        size_t i;
        json_t* host;
        json_array_foreach(hosts, i, host) {
            if (json_is_string(host)) out.hosts.push_back(json_string_value(host));
        }
    }
    return true;
}

bool readFile(const fs::path& path, JsonPtr& root) {
    json_error_t err;
    root.reset(json_load_file(toUtf8(path).c_str(), 0, &err));
    return root && json_is_object(root.get());
}

}  // namespace

bool saveDownloads(const std::string& path, const DownloadQueue& queue) {
    JsonPtr root(json_object());
    json_object_set_new(root.get(), "version", json_integer(kVersion));

    json_t* tasks = json_array();
    for (const DlTask& t : queue.tasks()) {
        if (!persists(t.kind)) continue;
        json_t* item = json_object();
        json_object_set_new(item, "id", json_string(t.id.c_str()));
        json_object_set_new(item, "kind", json_string(kindName(t.kind)));
        json_object_set_new(item, "priority", json_string(priorityName(t.priority)));
        json_object_set_new(item, "group", json_string(t.group.c_str()));
        json_t* hosts = json_array();
        for (const std::string& h : t.hosts) json_array_append_new(hosts, json_string(h.c_str()));
        json_object_set_new(item, "hosts", hosts);
        json_object_set_new(item, "resumable", json_boolean(t.resumable));
        json_object_set_new(item, "params", json_string(t.params.c_str()));
        json_object_set_new(item, "state", json_string(stateName(t.state)));
        json_object_set_new(item, "failures", json_integer(t.failures));
        if (t.state == DlState::Paused && t.diskPaused)
            json_object_set_new(item, "disk", json_true());
        json_array_append_new(tasks, item);
    }
    json_object_set_new(root.get(), "tasks", tasks);

    json_t* blocks = json_object();
    for (const auto& [host, b] : queue.allBlocks()) {
        json_t* entry = json_object();
        json_object_set_new(entry, "badLogin", json_boolean(b.badLogin));
        json_object_set_new(entry, "quotaUntil", json_integer(b.quotaUntil));
        json_object_set_new(blocks, host.c_str(), entry);
    }
    json_object_set_new(root.get(), "blocks", blocks);

    const fs::path target = fromUtf8(path);
    const fs::path tmp = fromUtf8(path + ".tmp");
    const fs::path backup = fromUtf8(path + ".old");
    if (json_dump_file(root.get(), toUtf8(tmp).c_str(), JSON_COMPACT) != 0) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }
    if (!replaceKeepingBackup(tmp, target, backup)) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool loadDownloads(const std::string& path, DownloadQueue& queue) {
    std::error_code ec;
    fs::path source = fromUtf8(path);
    // Power loss between the two renames of a save leaves only the backup.
    if (!fs::exists(source, ec)) source = fromUtf8(path + ".old");

    JsonPtr root;
    if (!readFile(source, root)) return false;

    const json_t* tasks = json_object_get(root.get(), "tasks");
    if (!json_is_array(tasks)) return false;

    std::vector<DlTask> parsed;
    size_t i;
    json_t* item;
    json_array_foreach(tasks, i, item) {
        DlTask task;
        if (!parseTask(item, task)) return false;
        parsed.push_back(std::move(task));
    }

    for (DlTask& task : parsed) queue.submit(std::move(task));

    const json_t* blocks = json_object_get(root.get(), "blocks");
    if (json_is_object(blocks)) {
        const char* host;
        json_t* entry;
        json_object_foreach(const_cast<json_t*>(blocks), host, entry) {
            if (!json_is_object(entry)) continue;
            DlHostBlocks b;
            b.badLogin = json_is_true(json_object_get(entry, "badLogin"));
            const json_t* until = json_object_get(entry, "quotaUntil");
            b.quotaUntil = json_is_integer(until) ? json_integer_value(until) : 0;
            queue.setBlocks(host, b);
        }
    }
    return true;
}

std::vector<std::string> orphanPartials(const std::vector<std::string>& found,
                                        const DownloadQueue& queue) {
    std::set<std::string> owned;
    for (const DlTask& t : queue.tasks()) {
        json_error_t err;
        JsonPtr params(json_loads(t.params.c_str(), 0, &err));
        if (!params || !json_is_object(params.get())) continue;
        const json_t* part = json_object_get(params.get(), "part");
        if (json_is_string(part)) owned.insert(json_string_value(part));
    }

    std::vector<std::string> orphans;
    for (const std::string& path : found)
        if (!owned.count(path)) orphans.push_back(path);
    return orphans;
}

}  // namespace emu
