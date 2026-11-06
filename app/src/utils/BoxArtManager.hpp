#include "Singleton.hpp"
#include <map>
#include <mutex>
#include <string>
#pragma once

struct Data;
struct Host;

// Cover art cached on the SD card, one folder per PC: two PCs can use the
// same app id for different games.
class BoxArtManager : public Singleton<BoxArtManager> {
  public:
    // Cheap: checks the file once, then remembers the answer.
    bool has_boxart(const Host& host, int app_id);

    // Writes and shrinks the cover. Slow (decode, resize, encode): call it
    // off the UI thread. Safe from any thread.
    void set_data(const Host& host, Data data, int app_id);
    static std::string get_texture_path(const Host& host, int app_id);

  private:
    std::mutex m_mutex;
    // Serializes the file work: the library and favorites can fetch the
    // same cover at once.
    std::mutex m_write_mutex;
    std::map<std::string, bool> m_has_boxart;
    static void compress_texture(const std::string& path);
};
