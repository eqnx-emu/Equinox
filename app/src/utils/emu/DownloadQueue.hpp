#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace emu {

enum class DlKind { Cover, GameData, Runner, AppUpdate, Media };
// Highest priority first: a lower enumerator runs before a higher one.
enum class DlPriority { Interactive, Visible, Batch, Background, Large };
enum class DlState { Queued, Running, Paused, Failed, Done };
enum class DlResult { Done, Gone, Transient, BadLogin, Quota, Disk, Invalid, Cancelled };

inline constexpr const char* kHostScreenScraper = "screenscraper";
inline constexpr const char* kHostLibretro = "libretro";
inline constexpr const char* kHostGitHub = "github";

// Whether a failed transfer is worth trying again: no HTTP error (no answer, a
// body cut short), a 5xx or a 429. Any other 4xx is the server's clear "no".
bool retryable(long httpStatus);

struct DlTask {
    std::string id;  // "gamedata:snes:Foo.sfc"
    DlKind kind = DlKind::Cover;
    DlPriority priority = DlPriority::Batch;
    std::string group;  // "snes", groups tasks on screen
    std::vector<std::string> hosts;
    bool resumable = false;
    std::string params;  // JSON owned by the task kind
    DlState state = DlState::Queued;
    int failures = 0;
    bool skipWait = false;  // an Interactive task may ignore a host backoff once
    // Paused by a full card, not by the user: a fresh request may resume it.
    bool diskPaused = false;
};

struct DlHostBlocks {
    bool badLogin = false;
    int64_t quotaUntil = 0;
};

// The tasks of one group and kind, counted by state, as the downloads screen
// shows them: one row per console and kind, not one per cover.
struct DlGroup {
    std::string group;
    DlKind kind = DlKind::Cover;
    size_t running = 0;
    size_t queued = 0;  // will run without the user
    size_t held = 0;    // Queued, but a host refused the login or is out of quota
    size_t paused = 0;
    size_t failed = 0;
    size_t finished = 0;  // done since the group last emptied
    std::string runningId;

    size_t remaining() const { return running + queued + held + paused + failed; }
    size_t total() const { return remaining() + finished; }
};

struct DlOverview {
    std::vector<DlGroup> groups;  // in the order their first task arrived
    // ScreenScraper blocks that hold a task back.
    bool badLogin = false;
    int64_t quotaUntil = 0;
    // A task is Paused because the card was full.
    bool diskFull = false;

    // Running and runnable Queued tasks: what runs without the user.
    size_t active() const;
};

// Scheduling state only: no threads, no I/O. The caller passes the clock in.
class DownloadQueue {
  public:
    // False when the id already exists and only the priority was raised (and,
    // for an Interactive request, its skipWait set).
    bool submit(DlTask task);
    // Many at once without the cost of submit per task. An id already known takes
    // the params and priority given here, unless it is Running; a Failed one goes
    // back to Queued, and so does one a full card paused; one the user paused
    // stays. Returns how many tasks became runnable.
    size_t submitAll(std::vector<DlTask> tasks);
    void setPriority(const std::string& id, DlPriority priority);
    // Marks the chosen task Running; nullptr if nothing can run or one is Running.
    DlTask* next(int64_t now);
    // True when the Running task is resumable and a higher priority task could run.
    bool shouldPreempt(int64_t now) const;
    // For Disk, returns the other tasks the full card paused; otherwise empty.
    std::vector<std::string> finish(const std::string& id, DlResult result,
                                    const std::string& host, int64_t now);
    void requeueRunning();  // suspend or preemption: back to Queued, no failure counted
    void pause(const std::string& id);
    // SD card full: pauses every Queued or Running task, marked as paused by
    // the card. Returns the ids it changed.
    std::vector<std::string> pauseAll();
    void resume(const std::string& id);
    void cancel(const std::string& id);
    void cancelGroup(const std::string& group, DlKind kind);
    // One pass each, for a whole group at once. Return the ids they changed.
    std::vector<std::string> pauseGroup(const std::string& group, DlKind kind);
    std::vector<std::string> resumeGroup(const std::string& group, DlKind kind);
    std::vector<std::string> retryGroup(const std::string& group, DlKind kind);  // Failed only
    std::vector<std::string> resumePaused();  // every Paused task, as after a full card
    void retryFailed();  // requeues Failed tasks, clears blocks and backoff
    // The user's "try now" for one host: clears its blocks and backoff and
    // requeues its Failed tasks. Returns the ids it held or failed.
    std::vector<std::string> retryHost(const std::string& host, int64_t now);
    // One Failed task back to Queued. False when it is not Failed.
    bool retry(const std::string& id);
    void clearLoginBlock();
    const std::vector<DlTask>& tasks() const { return tasks_; }
    DlHostBlocks blocks(const std::string& host) const;
    void setBlocks(const std::string& host, DlHostBlocks blocks);
    std::vector<std::pair<std::string, DlHostBlocks>> allBlocks() const;
    DlOverview overview(int64_t now) const;

  private:
    using GroupKey = std::pair<std::string, DlKind>;

    struct Backoff {
        int failures = 0;
        int64_t until = 0;
    };

    DlTask* find(const std::string& id);
    bool runnable(const DlTask& task, int64_t now, bool& usesSkipWait) const;
    bool hasRunning() const;
    bool held(const DlTask& task, int64_t now) const;
    // Forgets the group's finished count once it has no task left.
    void dropEmptyGroup(const GroupKey& key);

    std::vector<DlTask> tasks_;  // arrival order
    std::map<std::string, Backoff> backoff_;
    std::map<std::string, DlHostBlocks> blocks_;
    std::map<GroupKey, size_t> finished_;
};

}  // namespace emu
