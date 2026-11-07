#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "emu/CoverName.hpp"
#include "emu/GameDataJob.hpp"
#include "emu/PathUtf8.hpp"

using namespace emu;
namespace fs = std::filesystem;

namespace {

const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "image data";
const std::string kSynopsis =
    R"({"response":{"jeu":{"synopsis":[{"langue":"en","text":"English"},{"langue":"pt","text":"Sinopse"}]}}})";

struct Answer {
    FetchResult result = FetchResult::NotFound;
    std::string content;
    long status = 200;
};

// Serves files from a URL map (anything else is a 404) and records every request.
struct FakeTransport : DataTransport {
    std::map<std::string, Answer> files;
    Answer api{FetchResult::Ok, kSynopsis, 200};
    std::vector<std::string> requested;

    FetchResult toFile(const std::string& url, const std::string& path) override {
        requested.push_back(url);
        auto it = files.find(url);
        if (it == files.end()) return FetchResult::NotFound;
        if (it->second.result == FetchResult::Ok || it->second.result == FetchResult::Transient ||
            it->second.result == FetchResult::Disk)
            std::ofstream(fromUtf8(path), std::ios::binary) << it->second.content;
        return it->second.result;
    }
    FetchResult toString(const std::string& url, std::string& body, long& httpStatus) override {
        requested.push_back(url);
        body = api.content;
        httpStatus = api.status;
        return api.result;
    }
    bool asked(const std::string& needle) const {
        for (const auto& url : requested)
            if (url.find(needle) != std::string::npos) return true;
        return false;
    }
};

struct Fixture {
    fs::path root;
    const Console* console = findConsole("snes");
    GameDataRequest req;
    GameDb db;
    FakeTransport net;

    explicit Fixture(const char* name, const std::string& title = "Test Game") {
        root = fs::temp_directory_path() / name;
        fs::remove_all(root);
        fs::create_directories(root);
        req.console = console;
        req.paths = pathsFor(*console, toUtf8(root));
        REQUIRE(ensureFolders(req.paths));
        req.rom.fileName = title + ".sfc";
        req.rom.title = title;
        req.rom.path = req.paths.roms + "/" + req.rom.fileName;
        std::ofstream(fromUtf8(req.rom.path), std::ios::binary) << "123456789";
        db.loadText("#equinox-gamedb\t1\tabc\ncbf43926\tTest Game (USA)\tPuzzle\t1\tDev\tPub\t1994\t\n");
        req.db = &db;
        req.ss = {"dev", "pass", "Equinox", "me", "secret", "pt"};
    }

    void serve(ThumbKind kind, const std::string& name) {
        net.files[thumbUrl(*console, kind, name)] = {FetchResult::Ok, kPng};
    }
    DataOutcome run(const std::function<bool(DataStep)>& onStep = [](DataStep) { return true; }) {
        return fetchGameData(req, net, onStep);
    }
    bool exists(const std::string& path) const { return fs::exists(fromUtf8(path)); }
    // Any ".part" left in the snaps or titles folders.
    bool partLeft() const {
        for (const std::string& dir : {req.paths.snaps, req.paths.titles})
            for (const auto& entry : fs::directory_iterator(fromUtf8(dir)))
                if (entry.path().extension() == ".part") return true;
        return false;
    }
};

}  // namespace

TEST_CASE("stepProgress goes from 0 to 1 in quarters") {
    CHECK(stepProgress(DataStep::Identify) == 0.0f);
    CHECK(stepProgress(DataStep::Snap) == 0.25f);
    CHECK(stepProgress(DataStep::Title) == 0.5f);
    CHECK(stepProgress(DataStep::Synopsis) == 0.75f);
    CHECK(stepProgress(DataStep::Done) == 1.0f);
}

TEST_CASE("identified game downloads everything") {
    Fixture f("equinox_gamedata_all");
    f.serve(ThumbKind::Snap, "Test Game (USA)");
    f.serve(ThumbKind::Title, "Test Game (USA)");

    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK(out.stop == DataStop::None);
    CHECK(out.meta.identified);
    CHECK(out.meta.crc == 0xCBF43926);
    CHECK(out.meta.canonical == "Test Game (USA)");
    CHECK(out.meta.snap == PartState::Ok);
    CHECK(out.meta.title == PartState::Ok);
    CHECK(out.meta.synopsis == PartState::Ok);
    CHECK(out.meta.synopsisText == "Sinopse");
    CHECK(out.meta.synopsisLang == "pt");
    CHECK(out.meta.size == 9);
    CHECK(f.exists(thumbPath(f.req.paths, ThumbKind::Snap, "Test Game")));
    CHECK(f.exists(thumbPath(f.req.paths, ThumbKind::Title, "Test Game")));
    CHECK_FALSE(f.partLeft());

    GameMeta saved;
    REQUIRE(loadMeta(metaPath(f.req.paths, "Test Game"), saved));
    CHECK(saved.crc == 0xCBF43926);
    CHECK(saved.size == out.meta.size);
    CHECK(saved.mtime == out.meta.mtime);
    uint64_t size = 0;
    int64_t mtime = 0;
    REQUIRE(romStat(f.req.rom.path, size, mtime));
    CHECK(metaFresh(saved, size, mtime));

    REQUIRE_FALSE(f.net.requested.empty());
    CHECK(f.net.requested.front().find("Test%20Game%20%28USA%29") != std::string::npos);
}

TEST_CASE("unknown rom still gets images by file name") {
    Fixture f("equinox_gamedata_unknown", "Plain Name");
    std::ofstream(fromUtf8(f.req.rom.path), std::ios::binary | std::ios::trunc) << "something else";
    f.serve(ThumbKind::Snap, "Plain Name");
    f.serve(ThumbKind::Title, "Plain Name");

    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK_FALSE(out.meta.identified);
    CHECK(out.meta.canonical.empty());
    CHECK(out.meta.snap == PartState::Ok);
    CHECK(out.meta.title == PartState::Ok);
    CHECK(f.net.requested.front().find("Plain%20Name") != std::string::npos);
    CHECK(f.net.requested.front().find("Test%20Game") == std::string::npos);
}

TEST_CASE("no snap anywhere") {
    Fixture f("equinox_gamedata_nosnap");
    f.serve(ThumbKind::Title, "Test Game (USA)");

    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK(out.meta.snap == PartState::Missing);
    CHECK(out.meta.title == PartState::Ok);
    CHECK(out.meta.synopsis == PartState::Ok);
    CHECK_FALSE(f.exists(thumbPath(f.req.paths, ThumbKind::Snap, "Test Game")));
    CHECK(f.exists(metaPath(f.req.paths, "Test Game")));
}

TEST_CASE("offline in the middle") {
    Fixture f("equinox_gamedata_offline");
    f.serve(ThumbKind::Snap, "Test Game (USA)");
    f.net.files[thumbUrl(*f.console, ThumbKind::Title, "Test Game (USA)")] = {FetchResult::Transient, "xx"};

    const DataOutcome out = f.run();
    CHECK_FALSE(out.complete);
    CHECK(out.stop == DataStop::Offline);
    CHECK_FALSE(f.partLeft());
    CHECK_FALSE(f.exists(metaPath(f.req.paths, "Test Game")));
    CHECK_FALSE(f.net.asked("screenscraper"));
}

TEST_CASE("a disk failure stops with disk, keeps earlier parts and marks nothing missing") {
    Fixture f("equinox_gamedata_disk");
    f.serve(ThumbKind::Snap, "Test Game (USA)");
    f.net.files[thumbUrl(*f.console, ThumbKind::Title, "Test Game (USA)")] = {FetchResult::Disk, "xx"};

    const DataOutcome out = f.run();
    CHECK_FALSE(out.complete);
    CHECK(out.stop == DataStop::Disk);
    CHECK(out.meta.title != PartState::Missing);
    CHECK(f.exists(thumbPath(f.req.paths, ThumbKind::Snap, "Test Game")));
    CHECK_FALSE(f.exists(thumbPath(f.req.paths, ThumbKind::Title, "Test Game")));
    CHECK_FALSE(f.partLeft());
    CHECK_FALSE(f.exists(metaPath(f.req.paths, "Test Game")));
    CHECK_FALSE(f.net.asked("screenscraper"));
    // The second candidate name is never tried after a write failure.
    CHECK_FALSE(f.net.asked("Test%20Game.png"));
}

TEST_CASE("quota stops the queue") {
    Fixture f("equinox_gamedata_quota");
    f.serve(ThumbKind::Snap, "Test Game (USA)");
    f.serve(ThumbKind::Title, "Test Game (USA)");
    f.net.api = {FetchResult::Ok, "", 430};

    const DataOutcome out = f.run();
    CHECK_FALSE(out.complete);
    CHECK(out.stop == DataStop::Quota);
    CHECK(f.exists(thumbPath(f.req.paths, ThumbKind::Snap, "Test Game")));
    CHECK(f.exists(thumbPath(f.req.paths, ThumbKind::Title, "Test Game")));
    CHECK_FALSE(f.exists(metaPath(f.req.paths, "Test Game")));
}

TEST_CASE("bad login stops the queue") {
    Fixture f("equinox_gamedata_login");
    f.net.api = {FetchResult::Ok, "", 403};
    const DataOutcome out = f.run();
    CHECK_FALSE(out.complete);
    CHECK(out.stop == DataStop::BadLogin);
}

TEST_CASE("synopsis not found is recorded as missing") {
    Fixture f("equinox_gamedata_ssmiss");
    f.net.api = {FetchResult::Ok, "", 404};
    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK(out.meta.synopsis == PartState::Missing);
}

TEST_CASE("screenscraper unreachable is offline") {
    Fixture f("equinox_gamedata_ssoffline");
    f.net.api = {FetchResult::Transient, "", 0};
    const DataOutcome out = f.run();
    CHECK_FALSE(out.complete);
    CHECK(out.stop == DataStop::Offline);
    CHECK_FALSE(f.exists(metaPath(f.req.paths, "Test Game")));
}

TEST_CASE("no screenscraper account") {
    Fixture f("equinox_gamedata_noss");
    f.req.ss = {};

    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK(out.meta.synopsis == PartState::Skipped);
    CHECK(out.meta.synopsisText.empty());
    CHECK_FALSE(f.net.asked("screenscraper"));
}

TEST_CASE("cancel") {
    Fixture f("equinox_gamedata_cancel");
    f.serve(ThumbKind::Snap, "Test Game (USA)");
    f.serve(ThumbKind::Title, "Test Game (USA)");

    const DataOutcome out = f.run([](DataStep step) { return step != DataStep::Title; });
    CHECK_FALSE(out.complete);
    CHECK(out.stop == DataStop::Cancelled);
    CHECK_FALSE(f.partLeft());
    CHECK_FALSE(f.exists(thumbPath(f.req.paths, ThumbKind::Title, "Test Game")));
    CHECK_FALSE(f.exists(metaPath(f.req.paths, "Test Game")));
}

TEST_CASE("a transport that reports cancelled ends the job") {
    Fixture f("equinox_gamedata_cancelled_transport");
    f.net.files[thumbUrl(*f.console, ThumbKind::Snap, "Test Game (USA)")] = {FetchResult::Cancelled};
    const DataOutcome out = f.run();
    CHECK(out.stop == DataStop::Cancelled);
    CHECK_FALSE(out.complete);
}

TEST_CASE("200 that is not a png") {
    Fixture f("equinox_gamedata_notpng");
    f.net.files[thumbUrl(*f.console, ThumbKind::Snap, "Test Game (USA)")] = {FetchResult::Ok, "<html>"};
    f.serve(ThumbKind::Title, "Test Game (USA)");

    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK(out.meta.snap == PartState::Missing);
    CHECK_FALSE(f.exists(thumbPath(f.req.paths, ThumbKind::Snap, "Test Game")));
    CHECK_FALSE(f.partLeft());
}

TEST_CASE("falls back to the file name when the canonical name has no image") {
    Fixture f("equinox_gamedata_fallback");
    f.serve(ThumbKind::Snap, "Test Game");

    const DataOutcome out = f.run();
    CHECK(out.complete);
    CHECK(out.meta.snap == PartState::Ok);
    CHECK(f.net.requested.size() >= 2);
}
