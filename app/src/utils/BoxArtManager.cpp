#include "BoxArtManager.hpp"
#include "Data.hpp"
#include "Settings.hpp"
#include <CImg.h>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace {
// Hosts report 00:00:00:00:00:00 when they can't read the interface (VMs,
// VPNs): that says nothing about which PC it is.
bool usable_mac(const std::string& mac) {
    for (char c : mac)
        if (c != '0' && c != ':' && c != '-')
            return true;
    return false;
}

// Folder name for a PC: its MAC when known (stable across address
// changes), otherwise its address or name, kept to filename-safe characters.
std::string host_folder(const Host& host) {
    std::string source = usable_mac(host.mac)    ? host.mac
                         : !host.address.empty() ? host.address
                                                 : host.hostname;
    std::string folder;
    for (char c : source) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '.')
            folder += c;
        else if (c == ':')
            continue;
        else
            folder += '_';
    }
    return folder.empty() ? "host" : folder;
}
} // namespace

std::string BoxArtManager::get_texture_path(const Host& host, int app_id) {
    return (fs::path(Settings::instance().boxart_dir()) / host_folder(host) /
            (std::to_string(app_id) + ".png"))
        .make_preferred()
        .string();
}

bool BoxArtManager::has_boxart(const Host& host, int app_id) {
    const std::string path = get_texture_path(host, app_id);
    std::lock_guard<std::mutex> guard(m_mutex);
    auto it = m_has_boxart.find(path);
    if (it != m_has_boxart.end())
        return it->second;

    std::error_code ec;
    const bool exists = fs::file_size(path, ec) > 0 && !ec;
    m_has_boxart[path] = exists;
    return exists;
}

namespace {
// Whole file written and closed without error.
bool write_file(const std::string& path, const Data& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool written =
        data.size() == 0 ||
        fwrite(data.bytes(), data.size(), 1, f) == 1;
    return fclose(f) == 0 && written;
}
} // namespace

void BoxArtManager::set_data(const Host& host, Data data, int app_id) {
    const std::string path = get_texture_path(host, app_id);
    // The library and favorites can fetch the same cover at once: one
    // writer per file, while other covers are saved in parallel.
    std::shared_ptr<std::mutex> fileMutex;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto& slot = m_write_mutexes[path];
        if (!slot)
            slot = std::make_shared<std::mutex>();
        fileMutex = slot;
    }
    std::lock_guard<std::mutex> writeGuard(*fileMutex);

    std::error_code ec;
    if (fs::file_size(path, ec) > 0 && !ec) {
        // Another request saved it first.
        std::lock_guard<std::mutex> guard(m_mutex);
        m_has_boxart[path] = true;
        return;
    }
    ec.clear();
    fs::create_directories(fs::path(path).parent_path(), ec);

    // Written under a temp name so a reader never sees a half-written file.
    // It keeps the .png extension: CImg picks the format from it.
    const std::string temp =
        (fs::path(path).parent_path() / (std::to_string(app_id) + ".tmp.png"))
            .string();
    bool ok = data.size() > 0 && write_file(temp, data);
    if (ok) {
        try {
            compress_texture(temp);
        } catch (...) {
            // Not an image CImg can read, or saving it failed half-way:
            // keep the original bytes.
            ok = write_file(temp, data);
        }
    }

    if (ok) {
        ec.clear();
        fs::rename(temp, path, ec);
        std::error_code existsEc;
        if (ec && fs::exists(temp, existsEc)) {
            // Horizon won't rename over an existing file.
            fs::remove(path, ec);
            fs::rename(temp, path, ec);
        }
        ok = !ec;
    }
    if (!ok) {
        // A broken cover would never be fetched again: drop it.
        fs::remove(temp, ec);
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_has_boxart[path] = ok;
}

void BoxArtManager::compress_texture(const std::string& path) {
    using namespace cimg_library;

    /*
     * target width == 300
     * target height == 400
     * 0.75 == 300 / 400
     */

    CImg<unsigned char> pic(path.c_str());
    if (float(pic.width()) / float(pic.height()) < 0.75f) {
        pic = pic.resize(300, int(float(pic.height()) * 300.0f / float(pic.width())), 1, 3);
    } else {
        pic = pic.resize(int(float(pic.width()) * 400.0f / float(pic.height())), 400, 1, 3);
    }
    pic.save(path.c_str());
}
