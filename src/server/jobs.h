// Background benchmark job: runs on its own thread so HTTP requests (and the UI)
// stay responsive. Results are published only when a run completes; a cancelled or
// failed run never overwrites the last good report.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "batch.h"

namespace wb {

enum class JobState { Idle, Running, Done, Cancelled, Failed };
const char* toString(JobState s);

struct JobSnapshot {
    int id = 0;
    JobState state = JobState::Idle;
    double progress = 0;
    std::string phase;
    std::string error;
    double elapsed_s = 0;
    std::shared_ptr<const BenchmarkReport> lastReport;  // last *completed* report
};

class BenchmarkJobs {
public:
    ~BenchmarkJobs();
    // Returns an error message, or empty on success.
    std::string start(const BatchConfig& cfg, int& jobId);
    void cancel();
    JobSnapshot snapshot() const;
    bool running() const { return running_.load(); }

private:
    mutable std::mutex mu_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    JobSnapshot snap_;
    double startedAt_ = 0;
};

}  // namespace wb
