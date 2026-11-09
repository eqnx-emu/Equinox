#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "emu/DownloadQueue.hpp"
#include "emu/DownloadStore.hpp"
#include "emu/ResumeState.hpp"

using namespace emu;

namespace {

DlTask task(const std::string& id, DlPriority priority = DlPriority::Batch,
            const std::string& host = kHostLibretro) {
    DlTask t;
    t.id = id;
    t.kind = DlKind::Cover;
    t.priority = priority;
    t.group = "snes";
    t.hosts = {host};
    return t;
}

const DlTask* get(const DownloadQueue& q, const std::string& id) {
    for (const DlTask& t : q.tasks())
        if (t.id == id) return &t;
    return nullptr;
}

constexpr int64_t kNow = 1000;

}  // namespace

TEST_CASE("submit of the same id does not duplicate and raises the priority") {
    DownloadQueue q;
    CHECK(q.submit(task("a", DlPriority::Background)));
    CHECK_FALSE(q.submit(task("a", DlPriority::Visible)));
    REQUIRE(q.tasks().size() == 1);
    CHECK(q.tasks()[0].priority == DlPriority::Visible);
    CHECK_FALSE(q.submit(task("a", DlPriority::Large)));
    CHECK(q.tasks()[0].priority == DlPriority::Visible);
}

TEST_CASE("submitAll adds new ids once and counts what it changed") {
    DownloadQueue q;
    q.submit(task("a", DlPriority::Background));
    std::vector<DlTask> batch{task("a", DlPriority::Visible), task("b"), task("b"), task("c")};
    CHECK(q.submitAll(std::move(batch)) == 2);
    CHECK(q.tasks().size() == 3);
    CHECK(get(q, "a")->priority == DlPriority::Visible);
}

TEST_CASE("submitAll takes the new params of a Queued task and lowers its priority") {
    DownloadQueue q;
    DlTask first = task("a", DlPriority::Visible);
    first.params = "old";
    q.submit(first);
    DlTask again = task("a", DlPriority::Background);
    again.params = "new";
    CHECK(q.submitAll({again}) == 0);
    CHECK(get(q, "a")->params == "new");
    CHECK(get(q, "a")->priority == DlPriority::Background);
}

TEST_CASE("submitAll gives a Failed task another go, not a Paused or Running one") {
    DownloadQueue q;
    DlTask failed = task("failed");
    failed.state = DlState::Failed;
    failed.failures = 4;
    DlTask paused = task("paused");
    paused.state = DlState::Paused;
    q.submit(failed);
    q.submit(paused);
    q.submit(task("running", DlPriority::Interactive));
    REQUIRE(q.next(0)->id == "running");
    CHECK(q.submitAll({task("failed"), task("paused"), task("running", DlPriority::Background)}) == 1);
    CHECK(get(q, "failed")->state == DlState::Queued);
    CHECK(get(q, "failed")->failures == 0);
    CHECK(get(q, "paused")->state == DlState::Paused);
    CHECK(get(q, "running")->state == DlState::Running);
    CHECK(get(q, "running")->priority == DlPriority::Interactive);
}

TEST_CASE("submit keeps a saved Paused or Failed state") {
    DownloadQueue q;
    DlTask p = task("p");
    p.state = DlState::Paused;
    DlTask f = task("f");
    f.state = DlState::Failed;
    DlTask r = task("r");
    r.state = DlState::Running;
    q.submit(p);
    q.submit(f);
    q.submit(r);
    CHECK(get(q, "p")->state == DlState::Paused);
    CHECK(get(q, "f")->state == DlState::Failed);
    CHECK(get(q, "r")->state == DlState::Queued);
}

TEST_CASE("next orders by priority then arrival") {
    DownloadQueue q;
    q.submit(task("large", DlPriority::Large));
    q.submit(task("bg", DlPriority::Background));
    q.submit(task("batch1", DlPriority::Batch));
    q.submit(task("batch2", DlPriority::Batch));
    q.submit(task("visible", DlPriority::Visible));
    q.submit(task("inter", DlPriority::Interactive));
    const char* order[] = {"inter", "visible", "batch1", "batch2", "bg", "large"};
    for (const char* id : order) {
        DlTask* t = q.next(kNow);
        REQUIRE(t != nullptr);
        CHECK(t->id == id);
        q.finish(t->id, DlResult::Done, kHostLibretro, kNow);
    }
    CHECK(q.next(kNow) == nullptr);
}

TEST_CASE("next returns at most one Running task") {
    DownloadQueue q;
    q.submit(task("a"));
    q.submit(task("b"));
    REQUIRE(q.next(kNow) != nullptr);
    CHECK(q.next(kNow) == nullptr);
}

TEST_CASE("shouldPreempt needs a resumable Running task and a higher priority queue") {
    DownloadQueue q;
    DlTask big = task("big", DlPriority::Large);
    big.resumable = true;
    q.submit(big);
    REQUIRE(q.next(kNow) != nullptr);
    CHECK_FALSE(q.shouldPreempt(kNow));
    q.submit(task("same", DlPriority::Large));
    CHECK_FALSE(q.shouldPreempt(kNow));
    q.submit(task("hi", DlPriority::Interactive));
    CHECK(q.shouldPreempt(kNow));

    DownloadQueue n;
    n.submit(task("plain", DlPriority::Large));
    n.next(kNow);
    n.submit(task("hi", DlPriority::Interactive));
    CHECK_FALSE(n.shouldPreempt(kNow));
}

TEST_CASE("Transient backs the host off by 5, 30 and 120 s without touching other hosts") {
    DownloadQueue q;
    q.submit(task("a", DlPriority::Batch, "A"));
    q.submit(task("b", DlPriority::Background, "B"));
    const int64_t waits[] = {5, 30, 120};
    int64_t now = kNow;
    for (int64_t wait : waits) {
        DlTask* t = q.next(now);
        REQUIRE(t != nullptr);
        REQUIRE(t->id == "a");
        q.finish("a", DlResult::Transient, "A", now);
        // Host A is blocked until now + wait, host B is not.
        DlTask* other = q.next(now + wait - 1);
        REQUIRE(other != nullptr);
        CHECK(other->id == "b");
        q.requeueRunning();
        now += wait;
    }
    CHECK(q.next(now)->id == "a");
}

TEST_CASE("the fourth consecutive failure marks the task Failed") {
    DownloadQueue q;
    q.submit(task("a"));
    int64_t now = kNow;
    for (int i = 0; i < 3; ++i) {
        q.next(now);
        q.finish("a", DlResult::Transient, kHostLibretro, now);
        CHECK(get(q, "a")->state == DlState::Queued);
        now += 1000;
    }
    q.next(now);
    q.finish("a", DlResult::Transient, kHostLibretro, now);
    CHECK(get(q, "a")->state == DlState::Failed);
    CHECK(q.next(now + 100000) == nullptr);
}

TEST_CASE("Done resets the host backoff") {
    DownloadQueue q;
    q.submit(task("a"));
    DlTask b = task("b", DlPriority::Interactive);
    b.skipWait = true;
    q.submit(b);
    q.next(kNow);
    q.finish("b", DlResult::Transient, kHostLibretro, kNow);  // 5 s
    q.next(kNow);  // b again through skipWait
    q.finish("b", DlResult::Done, kHostLibretro, kNow);
    // The backoff is gone, so "a" runs right away; before Done it was blocked.
    DlTask* a = q.next(kNow);
    REQUIRE(a != nullptr);
    CHECK(a->id == "a");
    q.finish("a", DlResult::Transient, kHostLibretro, kNow);
    CHECK(q.next(kNow + 4) == nullptr);
    CHECK(q.next(kNow + 5) != nullptr);  // first step again, 5 s and not 30 s
}

TEST_CASE("Interactive skipWait runs once during a backoff") {
    DownloadQueue q;
    DlTask t = task("a", DlPriority::Interactive);
    t.skipWait = true;
    q.submit(t);
    q.next(kNow);
    q.finish("a", DlResult::Transient, kHostLibretro, kNow);
    DlTask* again = q.next(kNow + 1);
    REQUIRE(again != nullptr);
    CHECK_FALSE(again->skipWait);
    q.finish("a", DlResult::Transient, kHostLibretro, kNow + 1);
    CHECK(q.next(kNow + 2) == nullptr);
}

TEST_CASE("skipWait does not help a task that is not Interactive") {
    DownloadQueue q;
    DlTask t = task("a", DlPriority::Visible);
    t.skipWait = true;
    q.submit(t);
    q.next(kNow);
    q.finish("a", DlResult::Transient, kHostLibretro, kNow);
    CHECK(q.next(kNow + 1) == nullptr);
}

TEST_CASE("Gone removes the task without a backoff") {
    DownloadQueue q;
    q.submit(task("a"));
    q.submit(task("b"));
    q.next(kNow);
    q.finish("a", DlResult::Gone, kHostLibretro, kNow);
    CHECK(get(q, "a") == nullptr);
    DlTask* b = q.next(kNow);
    REQUIRE(b != nullptr);
    CHECK(b->id == "b");
}

TEST_CASE("Quota blocks only that host for a day") {
    DownloadQueue q;
    q.submit(task("ss", DlPriority::Batch, kHostScreenScraper));
    q.submit(task("lr", DlPriority::Background, kHostLibretro));
    q.next(kNow);
    q.finish("ss", DlResult::Quota, kHostScreenScraper, kNow);
    CHECK(q.blocks(kHostScreenScraper).quotaUntil == kNow + 86400);
    CHECK(get(q, "ss")->failures == 0);
    DlTask* t = q.next(kNow + 1);
    REQUIRE(t != nullptr);
    CHECK(t->id == "lr");
    q.finish("lr", DlResult::Done, kHostLibretro, kNow + 1);
    CHECK(q.next(kNow + 86399) == nullptr);
    CHECK(q.next(kNow + 86400) != nullptr);
}

TEST_CASE("a quota stored too far ahead is capped to one day") {
    DownloadQueue q;
    q.submit(task("ss", DlPriority::Batch, kHostScreenScraper));
    q.setBlocks(kHostScreenScraper, {false, kNow + 10 * 86400});
    CHECK(q.next(kNow) == nullptr);  // the first look clamps the stored value
    CHECK(q.blocks(kHostScreenScraper).quotaUntil == kNow + 86400);
    CHECK(q.next(kNow + 86399) == nullptr);
    CHECK(q.next(kNow + 86400) != nullptr);
}

TEST_CASE("BadLogin blocks the host until clearLoginBlock") {
    DownloadQueue q;
    q.submit(task("ss", DlPriority::Batch, kHostScreenScraper));
    q.next(kNow);
    q.finish("ss", DlResult::BadLogin, kHostScreenScraper, kNow);
    CHECK(q.blocks(kHostScreenScraper).badLogin);
    CHECK(q.next(kNow + 100000) == nullptr);
    q.clearLoginBlock();
    CHECK_FALSE(q.blocks(kHostScreenScraper).badLogin);
    CHECK(q.next(kNow + 100000) != nullptr);
}

TEST_CASE("Disk pauses every task and resume brings them back") {
    DownloadQueue q;
    q.submit(task("a"));
    q.submit(task("b"));
    q.next(kNow);
    q.finish("a", DlResult::Disk, kHostLibretro, kNow);
    CHECK(get(q, "a")->state == DlState::Paused);
    CHECK(get(q, "b")->state == DlState::Paused);
    CHECK(q.next(kNow) == nullptr);
    q.retryFailed();  // Paused tasks are not Failed
    CHECK(q.next(kNow) == nullptr);
    q.resume("b");
    DlTask* b = q.next(kNow);
    REQUIRE(b != nullptr);
    CHECK(b->id == "b");
}

TEST_CASE("requeueRunning returns the task to Queued without a failure") {
    DownloadQueue q;
    q.submit(task("a"));
    q.next(kNow);
    q.requeueRunning();
    CHECK(get(q, "a")->state == DlState::Queued);
    CHECK(get(q, "a")->failures == 0);
    CHECK(q.next(kNow) != nullptr);
}

TEST_CASE("retryFailed requeues Failed tasks and clears blocks and backoff") {
    DownloadQueue q;
    q.submit(task("a", DlPriority::Batch, kHostScreenScraper));
    int64_t now = kNow;
    for (int i = 0; i < 4; ++i) {
        q.next(now);
        q.finish("a", DlResult::Transient, kHostScreenScraper, now);
        now += 1000;
    }
    REQUIRE(get(q, "a")->state == DlState::Failed);
    q.setBlocks(kHostScreenScraper, {true, now + 500});
    q.retryFailed();
    CHECK(get(q, "a")->state == DlState::Queued);
    CHECK(get(q, "a")->failures == 0);
    CHECK(q.allBlocks().empty());
    CHECK(q.next(now) != nullptr);
}

TEST_CASE("cancel and cancelGroup remove tasks") {
    DownloadQueue q;
    q.submit(task("a"));
    DlTask g = task("g");
    g.kind = DlKind::GameData;
    q.submit(g);
    DlTask o = task("o");
    o.group = "nes";
    q.submit(o);
    q.cancelGroup("snes", DlKind::Cover);
    CHECK(get(q, "a") == nullptr);
    CHECK(get(q, "g") != nullptr);
    CHECK(get(q, "o") != nullptr);
    q.cancel("o");
    CHECK(get(q, "o") == nullptr);
}

namespace {

namespace fs = std::filesystem;

std::string tmpPath(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "eqnx_dlstore_test";
    fs::create_directories(dir);
    fs::path p = dir / name;
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(p.string() + ".tmp", ec);
    fs::remove(p.string() + ".old", ec);
    return p.string();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}

DlTask persisted(const std::string& id, DlKind kind) {
    DlTask t = task(id, DlPriority::Visible, kHostScreenScraper);
    t.kind = kind;
    t.hosts = {kHostScreenScraper, kHostLibretro};
    t.resumable = true;
    t.params = "{\"part\":\"/sd/" + id + ".part\",\"n\":1}";
    return t;
}

}  // namespace

TEST_CASE("store round trip keeps id, priority, group, hosts and params") {
    const std::string path = tmpPath("roundtrip.json");
    DownloadQueue q;
    q.submit(persisted("gamedata:snes:Foo.sfc", DlKind::GameData));
    REQUIRE(saveDownloads(path, q));

    DownloadQueue loaded;
    REQUIRE(loadDownloads(path, loaded));
    REQUIRE(loaded.tasks().size() == 1);
    const DlTask& t = loaded.tasks()[0];
    CHECK(t.id == "gamedata:snes:Foo.sfc");
    CHECK(t.kind == DlKind::GameData);
    CHECK(t.priority == DlPriority::Visible);
    CHECK(t.group == "snes");
    CHECK(t.hosts == std::vector<std::string>{kHostScreenScraper, kHostLibretro});
    CHECK(t.resumable);
    CHECK(t.params == "{\"part\":\"/sd/gamedata:snes:Foo.sfc.part\",\"n\":1}");
}

TEST_CASE("store saves only GameData and Media tasks") {
    const std::string path = tmpPath("kinds.json");
    DownloadQueue q;
    q.submit(persisted("cover", DlKind::Cover));
    q.submit(persisted("gd", DlKind::GameData));
    q.submit(persisted("runner", DlKind::Runner));
    q.submit(persisted("upd", DlKind::AppUpdate));
    q.submit(persisted("media", DlKind::Media));
    REQUIRE(saveDownloads(path, q));

    DownloadQueue loaded;
    REQUIRE(loadDownloads(path, loaded));
    REQUIRE(loaded.tasks().size() == 2);
    CHECK(get(loaded, "gd"));
    CHECK(get(loaded, "media"));
}

TEST_CASE("store restores Running as Queued and keeps Failed and Paused") {
    const std::string path = tmpPath("states.json");
    DownloadQueue q;
    q.submit(persisted("running", DlKind::GameData));
    q.submit(persisted("failed", DlKind::GameData));
    q.submit(persisted("paused", DlKind::Media));
    q.submit(persisted("queued", DlKind::Media));
    REQUIRE(q.next(kNow));  // "running" is first in arrival order
    q.finish("failed", DlResult::Invalid, kHostScreenScraper, kNow);
    q.pause("paused");
    REQUIRE(get(q, "running")->state == DlState::Running);
    REQUIRE(get(q, "failed")->state == DlState::Failed);
    REQUIRE(saveDownloads(path, q));

    DownloadQueue loaded;
    REQUIRE(loadDownloads(path, loaded));
    CHECK(get(loaded, "running")->state == DlState::Queued);
    CHECK(get(loaded, "failed")->state == DlState::Failed);
    CHECK(get(loaded, "paused")->state == DlState::Paused);
    CHECK(get(loaded, "queued")->state == DlState::Queued);
}

TEST_CASE("store restores login and quota blocks") {
    const std::string path = tmpPath("blocks.json");
    DownloadQueue q;
    q.submit(persisted("a", DlKind::GameData));
    q.setBlocks(kHostScreenScraper, {true, 0});
    q.setBlocks(kHostLibretro, {false, 5000});
    REQUIRE(saveDownloads(path, q));

    DownloadQueue loaded;
    REQUIRE(loadDownloads(path, loaded));
    CHECK(loaded.blocks(kHostScreenScraper).badLogin);
    CHECK(loaded.blocks(kHostLibretro).quotaUntil == 5000);
    CHECK_FALSE(loaded.blocks(kHostLibretro).badLogin);
}

TEST_CASE("store refuses truncated and empty files and leaves the queue alone") {
    const std::string path = tmpPath("bad.json");
    DownloadQueue q;
    q.submit(persisted("a", DlKind::GameData));
    REQUIRE(saveDownloads(path, q));
    std::string good;
    {
        std::ifstream in(path, std::ios::binary);
        good.assign(std::istreambuf_iterator<char>(in), {});
    }

    DownloadQueue target;
    target.submit(persisted("keep", DlKind::Media));

    writeFile(path, good.substr(0, good.size() / 2));
    CHECK_FALSE(loadDownloads(path, target));
    writeFile(path, "");
    CHECK_FALSE(loadDownloads(path, target));
    CHECK_FALSE(loadDownloads(tmpPath("missing.json"), target));
    REQUIRE(target.tasks().size() == 1);
    CHECK(target.tasks()[0].id == "keep");
}

TEST_CASE("store falls back to the backup when only it survived") {
    const std::string path = tmpPath("backup.json");
    DownloadQueue q;
    q.submit(persisted("a", DlKind::GameData));
    REQUIRE(saveDownloads(path, q));
    fs::rename(path, path + ".old");  // power loss between the two renames

    DownloadQueue loaded;
    REQUIRE(loadDownloads(path, loaded));
    CHECK(get(loaded, "a"));
}

TEST_CASE("saving twice replaces the file and leaves no temp or backup") {
    const std::string path = tmpPath("twice.json");
    DownloadQueue q;
    q.submit(persisted("a", DlKind::GameData));
    REQUIRE(saveDownloads(path, q));
    q.submit(persisted("b", DlKind::Media));
    REQUIRE(saveDownloads(path, q));

    DownloadQueue loaded;
    REQUIRE(loadDownloads(path, loaded));
    CHECK(loaded.tasks().size() == 2);
    CHECK_FALSE(fs::exists(path + ".tmp"));
    CHECK_FALSE(fs::exists(path + ".old"));
}

TEST_CASE("orphanPartials returns parts no saved task owns") {
    DownloadQueue q;
    q.submit(persisted("owned", DlKind::GameData));
    DlTask noPart = task("nopart");
    noPart.params = "{\"x\":1}";
    q.submit(noPart);
    DlTask badParams = task("bad");
    badParams.params = "not json";
    q.submit(badParams);

    auto orphans = orphanPartials(
        {"/sd/owned.part", "/sd/stray.part", "/sd/nopart.part"}, q);
    CHECK(orphans == std::vector<std::string>{"/sd/stray.part", "/sd/nopart.part"});
}

namespace {

std::string partIn(const char* name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "eqx_resume_test";
    std::filesystem::create_directories(dir);
    std::string part = (dir / name).string();
    std::filesystem::remove(part);
    std::filesystem::remove(part + ".resume");
    return part;
}

void writePart(const std::string& part, size_t bytes) {
    std::ofstream(part, std::ios::binary) << std::string(bytes, 'x');
}

}  // namespace

TEST_CASE("resumeOffset is 0 without a control file") {
    std::string part = partIn("none.part");
    writePart(part, 100);
    CHECK(resumeOffset(part, "https://h/a") == 0);
}

TEST_CASE("resumeOffset is 0 when the url differs") {
    std::string part = partIn("url.part");
    writePart(part, 100);
    REQUIRE(saveResume(part, {"https://h/a", "\"etag\"", 500}));
    CHECK(resumeOffset(part, "https://h/b") == 0);
}

TEST_CASE("resumeOffset is 0 when the part is larger than total") {
    std::string part = partIn("big.part");
    writePart(part, 600);
    REQUIRE(saveResume(part, {"https://h/a", "\"etag\"", 500}));
    CHECK(resumeOffset(part, "https://h/a") == 0);
}

TEST_CASE("resumeOffset returns the part size when valid") {
    std::string part = partIn("ok.part");
    writePart(part, 100);
    REQUIRE(saveResume(part, {"https://h/a", "\"etag\"", 500}));
    CHECK(resumeOffset(part, "https://h/a") == 100);
}

TEST_CASE("ResumeState round-trips and is removed") {
    std::string part = partIn("rt.part");
    REQUIRE(saveResume(part, {"https://h/a", "W/\"v\"", 42}));
    auto loaded = loadResume(part);
    REQUIRE(loaded.has_value());
    CHECK(loaded->url == "https://h/a");
    CHECK(loaded->validator == "W/\"v\"");
    CHECK(loaded->total == 42);
    removeResume(part);
    CHECK_FALSE(loadResume(part).has_value());
}

TEST_CASE("resumeOffset is 0 for an empty part") {
    std::string part = partIn("empty.part");
    writePart(part, 0);
    REQUIRE(saveResume(part, {"https://h/a", "\"etag\"", 500}));
    CHECK(resumeOffset(part, "https://h/a") == 0);
}

TEST_CASE("resumeOffset is 0 for a corrupt control file") {
    std::string part = partIn("corrupt.part");
    writePart(part, 100);
    std::ofstream(part + ".resume") << "{not json";
    CHECK(resumeOffset(part, "https://h/a") == 0);
    CHECK_FALSE(loadResume(part).has_value());
}

TEST_CASE("only no answer, a cut body, 5xx and 429 are worth retrying") {
    CHECK(retryable(0));
    CHECK(retryable(200));
    CHECK(retryable(429));
    CHECK(retryable(500));
    CHECK(retryable(503));
    CHECK_FALSE(retryable(400));
    CHECK_FALSE(retryable(403));
    CHECK_FALSE(retryable(404));
    CHECK_FALSE(retryable(410));
    CHECK_FALSE(retryable(416));
}
