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
};

struct DlHostBlocks {
    bool badLogin = false;
    int64_t quotaUntil = 0;
};

// Scheduling state only: no threads, no I/O. The caller passes the clock in.
class DownloadQueue {
  public:
    // False when the id already exists and only the priority was raised.
    bool submit(DlTask task);
    // Many at once without the cost of submit per task. An id already known takes
    // the params and priority given here, unless it is Running; a Failed or Paused
    // one goes back to Queued. Returns how many tasks became runnable.
    size_t submitAll(std::vector<DlTask> tasks);
    void setPriority(const std::string& id, DlPriority priority);
    // Marks the chosen task Running; nullptr if nothing can run or one is Running.
    DlTask* next(int64_t now);
    // True when the Running task is resumable and a higher priority task could run.
    bool shouldPreempt(int64_t now) const;
    void finish(const std::string& id, DlResult result, const std::string& host, int64_t now);
    void requeueRunning();  // suspend or preemption: back to Queued, no failure counted
    void pause(const std::string& id);
    void pauseAll();  // SD card full
    void resume(const std::string& id);
    void cancel(const std::string& id);
    void cancelGroup(const std::string& group, DlKind kind);
    void retryFailed();  // requeues Failed tasks, clears blocks and backoff
    void clearLoginBlock();
    const std::vector<DlTask>& tasks() const { return tasks_; }
    DlHostBlocks blocks(const std::string& host) const;
    void setBlocks(const std::string& host, DlHostBlocks blocks);
    std::vector<std::pair<std::string, DlHostBlocks>> allBlocks() const;

  private:
    struct Backoff {
        int failures = 0;
        int64_t until = 0;
    };

    DlTask* find(const std::string& id);
    bool runnable(const DlTask& task, int64_t now, bool& usesSkipWait) const;
    bool hasRunning() const;

    std::vector<DlTask> tasks_;  // arrival order
    std::map<std::string, Backoff> backoff_;
    std::map<std::string, DlHostBlocks> blocks_;
};

}  // namespace emu
