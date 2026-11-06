#include "Updater.hpp"

#include "Settings.hpp"
#include "helper.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef __SWITCH__
#include <curl/curl.h>
#include <jansson.h>
#include <switch.h>
#endif

using namespace brls;

namespace Updater {

namespace {

const char* const RELEASES_URL =
    "https://api.github.com/repos/luizfbalves/Equinox/releases/latest";
const char* const ASSET_NAME = "Equinox.nro";

std::vector<int> parseVersion(std::string version) {
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V'))
        version.erase(0, 1);

    std::vector<int> parts;
    std::stringstream stream(version);
    std::string part;
    while (std::getline(stream, part, '.')) {
        try {
            parts.push_back(std::stoi(part));
        } catch (...) {
            return {};
        }
    }
    return parts;
}

std::string stripTag(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        return tag.substr(1);
    return tag;
}

#ifdef __SWITCH__

const char* const CA_BUNDLE = "romfs:/certs/cacert.pem";

size_t writeToString(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t writeToFile(char* data, size_t size, size_t count, void* user) {
    return fwrite(data, 1, size * count, static_cast<FILE*>(user));
}

// GET `url` over verified TLS, following redirects (release assets redirect
// to GitHub's object storage).
bool httpGet(const std::string& url, curl_write_callback write, void* user,
             long timeout, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Equinox/" APP_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, CA_BUNDLE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, user);

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK)
        error = curl_easy_strerror(code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

bool fetchLatest(UpdateInfo& info, std::string& error) {
    std::string body;
    if (!httpGet(RELEASES_URL, writeToString, &body, 20, error))
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

// Accept only a complete NRO: the advertised size and the "NRO0" magic.
bool looksLikeNro(const std::string& path, size_t expectedSize) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;

    char header[0x14] = {};
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header) &&
              memcmp(header + 0x10, "NRO0", 4) == 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return ok && size > 0 && (expectedSize == 0 || (size_t)size == expectedSize);
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

void downloadAndInstall(const UpdateInfo& info) {
    const std::string target = Settings::instance().launch_path();
    auto* loader = createLoadingDialog("update/downloading"_i18n);
    loader->open();

    brls::async([info, target, loader] {
        const std::string downloaded = target + ".new";
        std::string error;
        bool ok = false;

        FILE* file = fopen(downloaded.c_str(), "wb");
        if (!file) {
            error = "could not write " + downloaded;
        } else {
            ok = httpGet(info.url, writeToFile, file, 300, error);
            ok = fclose(file) == 0 && ok;
            if (ok && !looksLikeNro(downloaded, info.size)) {
                ok = false;
                error = "downloaded file is not a valid NRO";
            }
            if (!ok)
                remove(downloaded.c_str());
        }

        brls::sync([ok, error, info, target, downloaded, loader] {
            loader->close([ok, error, info, target, downloaded] {
                if (!ok) {
                    showError("update/failed"_i18n + "\n" + error);
                    return;
                }

                std::string installError;
                if (!install(target, downloaded, installError)) {
                    remove(downloaded.c_str());
                    showError("update/failed"_i18n + "\n" + installError);
                    return;
                }

                brls::Logger::info("Updater: installed {}", info.version);
                showAlert(fmt::format(fmt::runtime("update/installed"_i18n),
                                      info.version),
                          [target] { relaunch(target); });
            });
        });
    });
}

void offer(const UpdateInfo& info) {
    auto* dialog = new Dialog(fmt::format(
        fmt::runtime("update/available"_i18n), info.version, APP_VERSION));
    dialog->addButton("update/later"_i18n, [] {});
    dialog->addButton("update/install"_i18n,
                      [info] { brls::sync([info] { downloadAndInstall(info); }); });
    dialog->open();
}

#endif // __SWITCH__

} // namespace

bool isNewer(const std::string& candidate, const std::string& current) {
    std::vector<int> a = parseVersion(candidate);
    std::vector<int> b = parseVersion(current);
    if (a.empty() || b.empty())
        return false;

    size_t count = std::max(a.size(), b.size());
    a.resize(count, 0);
    b.resize(count, 0);
    return a > b;
}

bool supported() {
#ifdef __SWITCH__
    const std::string path = Settings::instance().launch_path();
    return path.rfind("sdmc:/", 0) == 0 && path.size() > 4 &&
           path.compare(path.size() - 4, 4, ".nro") == 0;
#else
    return false;
#endif
}

void checkAndPrompt(bool silent) {
#ifdef __SWITCH__
    if (!supported()) {
        if (!silent)
            showError("update/unsupported"_i18n);
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
