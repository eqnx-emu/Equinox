#pragma once

#include <functional>
#include <string>

#include "emu/Console.hpp"
#include "emu/GameDb.hpp"
#include "emu/GameMeta.hpp"
#include "emu/RomScan.hpp"
#include "emu/ScreenScraper.hpp"

namespace emu {

enum class FetchResult { Ok, NotFound, Transient, Disk, Cancelled };

struct DataTransport {
    virtual ~DataTransport() = default;
    // Writes the body to `path`. NotFound: the server has no such file.
    // Transient: no answer or any other failure. Disk: the file could not be
    // written (full card, read-only). The file may be left behind.
    virtual FetchResult toFile(const std::string& url, const std::string& path) = 0;
    // Ok whenever the server answered, whatever the status: `httpStatus`
    // tells what it said. Transient when it did not answer.
    virtual FetchResult toString(const std::string& url, std::string& body, long& httpStatus) = 0;
};

enum class DataStep { Identify, Snap, Title, Synopsis, Done };
float stepProgress(DataStep step);  // 0, .25, .5, .75, 1

struct GameDataRequest {
    const Console* console;
    ConsolePaths paths;
    RomEntry rom;
    const GameDb* db;  // may be null
    SsConfig ss;
};

enum class DataStop { None, Offline, BadLogin, Quota, Disk, Cancelled };

struct DataOutcome {
    bool complete = false;
    DataStop stop = DataStop::None;
    GameMeta meta;
};

// Identifies the ROM, then downloads its snap and title screen from libretro.
// With a ScreenScraper account it then takes the synopsis and the facts, and
// the snap, title screen and cover that libretro does not have. The
// meta file is written only when every step has been decided; when the job
// stops, what was already downloaded stays but no meta is written.
// `onStep` runs on entering each step except Done; returning false cancels.
DataOutcome fetchGameData(const GameDataRequest& request, DataTransport& transport,
                          const std::function<bool(DataStep)>& onStep);

}  // namespace emu
