#include "jobs.h"

#include <chrono>

namespace wb {

namespace {
double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

const char* toString(JobState s) {
    switch (s) {
        case JobState::Idle: return "idle";
        case JobState::Running: return "running";
        case JobState::Done: return "done";
        case JobState::Cancelled: return "cancelled";
        case JobState::Failed: return "failed";
    }
    return "?";
}

BenchmarkJobs::~BenchmarkJobs() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

std::string BenchmarkJobs::start(const BatchConfig& cfg, int& jobId) {
    const std::string err = validate(cfg);
    if (!err.empty()) return err;
    std::lock_guard<std::mutex> lk(mu_);
    if (running_) return "A benchmark is already running. Cancel it or wait for it to finish.";
    if (worker_.joinable()) worker_.join();  // previous run has finished; reap its thread

    cancel_ = false;
    running_ = true;
    jobId = ++snap_.id;
    snap_.state = JobState::Running;
    snap_.progress = 0;
    snap_.phase = "starting";
    snap_.error.clear();
    startedAt_ = nowSeconds();

    worker_ = std::thread([this, cfg] {
        JobState final = JobState::Done;
        std::string error;
        std::shared_ptr<const BenchmarkReport> report;
        try {
            auto rep = runBenchmark(cfg, &cancel_, [this](double f, const std::string& phase) {
                std::lock_guard<std::mutex> g(mu_);
                snap_.progress = f;
                snap_.phase = phase;
            });
            report = std::make_shared<const BenchmarkReport>(std::move(rep));
        } catch (const Cancelled&) {
            final = JobState::Cancelled;
        } catch (const std::exception& e) {
            final = JobState::Failed;
            error = e.what();
        }
        std::lock_guard<std::mutex> g(mu_);
        snap_.state = final;
        snap_.error = error;
        snap_.phase = final == JobState::Done ? "complete" : toString(final);
        if (report) {
            snap_.lastReport = report;
            snap_.progress = 1.0;
        }
        snap_.elapsed_s = nowSeconds() - startedAt_;
        running_ = false;
    });
    return {};
}

void BenchmarkJobs::cancel() { cancel_ = true; }

JobSnapshot BenchmarkJobs::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    JobSnapshot s = snap_;
    if (s.state == JobState::Running) s.elapsed_s = nowSeconds() - startedAt_;
    return s;
}

}  // namespace wb
