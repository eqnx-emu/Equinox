#include "Updater.hpp"

#include "HttpDownload.hpp"

#include "Settings.hpp"
#include "helper.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>

#ifdef __SWITCH__
#include "download_service.hpp"
#include "emu/ResumeState.hpp"

#include <curl/curl.h>
#include <jansson.h>
#include <switch.h>
#endif

using namespace brls;

namespace Updater {

namespace {

const char* const RELEASES_URL =
    "https://api.github.com/repos/eqnx-emu/Equinox/releases/latest";
const char* const ASSET_NAME = "Equinox.nro";

std::string stripTag(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        return tag.substr(1);
    return tag;
}

#ifdef __SWITCH__

const char* const TASK_ID = "appupdate";

// The update the "appupdate" task downloads, from its submit to its end. UI
// thread only.
struct Pending {
    std::string version;
    std::string target;
    int percent = 0;
};

std::optional<Pending>& pending() {
    static std::optional<Pending> job;
    return job;
}

// Why the last attempt failed: written by the worker, read on the UI thread.
std::mutex errorMutex;
std::string lastError;

void setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex);
    lastError = error;
}

std::string takeError() {
    std::lock_guard<std::mutex> lock(errorMutex);
    std::string error;
    error.swap(lastError);
    return error;
}

std::string partPath(const std::string& target) { return target + ".new.part"; }

void discardPart(const std::string& target) {
    remove(partPath(target).c_str());
    emu::removeResume(partPath(target));
}

std::string stringField(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

bool fetchLatest(UpdateInfo& info, std::string& error) {
    std::string body;
    if (!httpGetToString(RELEASES_URL, body, 20, error))
        return false;

    json_error_t jsonError;
    json_t* root = json_loads(body.c_str(), 0, &jsonError);
    if (!root) {
        error = jsonError.text;
        return false;
    }

    bool found = false;
    const char* tag = json_string_value(json_object_get(root, "tag_name"));
    json_t* assets = json_object_get(root, "assets");
    if (tag && json_is_array(assets)) {
        size_t index;
        json_t* asset;
        json_array_foreach(assets, index, asset) {
            const char* name = json_string_value(json_object_get(asset, "name"));
            const char* url = json_string_value(
                json_object_get(asset, "browser_download_url"));
            if (name && url && strcmp(name, ASSET_NAME) == 0) {
                info.version = stripTag(tag);
                info.url = url;
                info.size = (size_t)json_integer_value(json_object_get(asset, "size"));
                found = true;
                break;
            }
        }
    }
    json_decref(root);

    if (!found)
        error = "release has no " + std::string(ASSET_NAME);
    return found;
}

// Worker thread. A network failure or a stop keeps the .part to continue it;
// a cancel or a file that is not the NRO removes it.
emu::DlResult runUpdate(const emu::DlTask& task, DlContext& context) {
    json_error_t jsonError;
    json_t* params = json_loads(task.params.c_str(), 0, &jsonError);
    if (!params) {
        setError("invalid update task");
        return emu::DlResult::Invalid;
    }
    const std::string url = stringField(params, "url");
    const std::string version = stringField(params, "version");
    const std::string target = stringField(params, "target");
    const size_t size = (size_t)json_integer_value(json_object_get(params, "size"));
    json_decref(params);
    if (url.empty() || version.empty() || target.empty()) {
        setError("invalid update task");
        return emu::DlResult::Invalid;
    }

    const std::string part = partPath(target);
    const Progress progress = [&context, size](uint64_t done, uint64_t total) {
        if (context.stop->load())
            return false;
        if (total == 0)
            total = size;
        if (total > 0)
            context.progress(static_cast<float>(std::min(done, total)) / total);
        return true;
    };
    const ResumableResult download = httpGetResumable(url, part, progress);
    if (context.stop->load() && context.abandoned()) {
        discardPart(target);
        return emu::DlResult::Cancelled;
    }
    if (!download.ok) {
        if (context.stop->load())
            return emu::DlResult::Cancelled;
        setError(download.error.empty() ? "download failed" : download.error);
        return download.writeFailed ? emu::DlResult::Disk
                                    : emu::DlResult::Transient;
    }

    if (!looksLikeNro(part, size)) {
        discardPart(target);
        setError("downloaded file is not a valid NRO");
        return emu::DlResult::Invalid;
    }
    if (!stage(target, version)) {
        discardPart(target);
        setError("could not save the update");
        return emu::DlResult::Invalid;
    }
    return emu::DlResult::Done;
}

// Swap the downloaded NRO in for the running one. The running NRO is still
// open as romfs, and Horizon will not rename an open file, so romfs is
// unmounted first and mounted again if anything fails.
bool install(const std::string& target, const std::string& downloaded,
             std::string& error) {
    const std::string backup = target + ".old";

    romfsExit();
    remove(backup.c_str());

    if (rename(target.c_str(), backup.c_str()) != 0) {
        error = "could not move the current NRO";
        romfsInit();
        return false;
    }

    if (rename(downloaded.c_str(), target.c_str()) != 0) {
        rename(backup.c_str(), target.c_str());
        error = "could not move the new NRO into place";
        romfsInit();
        return false;
    }

    remove(backup.c_str());
    return true;
}

void relaunch(const std::string& target) {
    if (envHasNextLoad())
        envSetNextLoad(target.c_str(), target.c_str());
    Application::quit();
}

// Stops every download first: the CA bundle they need lives in the romfs that
// install unmounts, and a background emulator install must not end after it.
void installStaged(const std::string& target) {
    DownloadService::instance().suspend([target] {
        std::string error;
        if (!install(target, target + ".new", error)) {
            clearStaged(target);
            DownloadService::instance().resumeAll();
            showError("update/failed"_i18n + "\n" + error);
            return;
        }
        remove(stagedVersionPath(target).c_str());
        brls::Logger::info("Updater: installed the staged update");
        relaunch(target);
    });
}

// "Later" keeps the staged NRO: it is offered again on the next start.
void askRestart(const std::string& version, const std::string& target) {
    auto* dialog = new Dialog(brls::getStr("update/ready", version));
    dialog->addButton("update/later"_i18n, [] {});
    dialog->addButton("update/restart_now"_i18n, [target] {
        brls::sync([target] { installStaged(target); });
    });
    dialog->open();
}

void fail() {
    pending().reset();
    // Out of the queue, so the next request starts a new task; the .part of
    // a network failure stays for it.
    DownloadService::instance().cancel(TASK_ID);
    showError("update/failed"_i18n + "\n" + takeError());
}

// For as long as the app runs, whatever screen is open.
void watchTask() {
    static bool watching = false;
    if (watching)
        return;
    watching = true;
    DownloadService::instance().subscribe([](const DlEvent& event) {
        if (event.id != TASK_ID || !pending())
            return;
        switch (event.state) {
        case emu::DlState::Running:
            pending()->percent = std::clamp((int)(event.progress * 100), 0, 100);
            break;
        case emu::DlState::Queued:
            break; // waits to try again
        case emu::DlState::Paused:
            if (event.result == emu::DlResult::Disk)
                fail();
            break;
        case emu::DlState::Failed:
            fail();
            break;
        case emu::DlState::Done: {
            const Pending job = *pending();
            pending().reset();
            if (event.result == emu::DlResult::Done) {
                brls::Logger::info("Updater: downloaded {}", job.version);
                askRestart(job.version, job.target);
            } else if (!DownloadService::instance().active(TASK_ID)) {
                // Cancelled while it waited; a running one removes it itself.
                discardPart(job.target);
            }
            break;
        }
        }
    });
}

void showProgress() {
    DownloadService& service = DownloadService::instance();
    // A full card may have paused it with every other download.
    service.resume(TASK_ID);
    service.setPriority(TASK_ID, emu::DlPriority::Interactive);
    Application::notify(brls::getStr("update/downloading_percent",
                                     pending()->percent));
}

void startDownload(const UpdateInfo& info) {
    if (pending()) {
        showProgress();
        return;
    }

    static bool registered = false;
    if (!registered) {
        DownloadService::instance().registerKind(emu::DlKind::AppUpdate, runUpdate);
        registered = true;
    }
    watchTask();

    const std::string target = Settings::instance().launch_path();
    pending() = Pending{info.version, target, 0};

    emu::DlTask task;
    task.id = TASK_ID;
    task.kind = emu::DlKind::AppUpdate;
    task.priority = emu::DlPriority::Interactive;
    task.skipWait = true;
    task.hosts = {emu::kHostGitHub};
    task.resumable = true;
    json_t* params = json_object();
    json_object_set_new(params, "url", json_string(info.url.c_str()));
    json_object_set_new(params, "version", json_string(info.version.c_str()));
    json_object_set_new(params, "target", json_string(target.c_str()));
    json_object_set_new(params, "size", json_integer((json_int_t)info.size));
    if (char* text = json_dumps(params, JSON_COMPACT)) {
        task.params = text;
        free(text);
    }
    json_decref(params);

    takeError();
    DownloadService::instance().submit(std::move(task));
    Application::notify("update/downloading"_i18n);
}

void offer(const UpdateInfo& info) {
    auto* dialog = new Dialog(fmt::format(
        fmt::runtime("update/available"_i18n), info.version, APP_VERSION));
    dialog->addButton("update/later"_i18n, [] {});
    dialog->addButton("update/install"_i18n,
                      [info] { brls::sync([info] { startDownload(info); }); });
    dialog->open();
}

#endif // __SWITCH__

} // namespace

bool supported() {
#ifdef __SWITCH__
    const std::string path = Settings::instance().launch_path();
    return path.rfind("sdmc:/", 0) == 0 && path.size() > 4 &&
           path.compare(path.size() - 4, 4, ".nro") == 0;
#else
    return false;
#endif
}

void offerStaged() {
#ifdef __SWITCH__
    if (!supported() || pending())
        return;
    const std::string target = Settings::instance().launch_path();
    if (std::optional<std::string> version = stagedUpdate(target, APP_VERSION))
        askRestart(*version, target);
#endif
}

void checkAndPrompt(bool silent) {
#ifdef __SWITCH__
    if (!supported()) {
        if (!silent)
            showError("update/unsupported"_i18n);
        return;
    }

    if (pending()) {
        showProgress();
        return;
    }
    const std::string target = Settings::instance().launch_path();
    if (std::optional<std::string> version = stagedUpdate(target, APP_VERSION)) {
        askRestart(*version, target);
        return;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    brls::async([silent] {
        UpdateInfo info;
        std::string error;
        bool ok = fetchLatest(info, error);
        if (!ok)
            brls::Logger::warning("Updater: check failed: {}", error);

        brls::sync([silent, ok, info, error] {
            if (ok && isNewer(info.version, APP_VERSION)) {
                offer(info);
            } else if (!silent) {
                showAlert(ok ? "update/up_to_date"_i18n
                             : "update/failed"_i18n + "\n" + error);
            }
        });
    });
#else
    if (!silent)
        showError("update/unsupported"_i18n);
#endif
}

} // namespace Updater
