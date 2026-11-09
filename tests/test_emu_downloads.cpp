#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>

#include "emu/DownloadQueue.hpp"

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
