#include "batch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace wb {

namespace {
constexpr long long kMaxStoredValues = 40'000'000;  // ~320 MB of doubles per result set

// Maps a raw 64-bit draw to [lo, hi) using the top 53 bits (portable across compilers).
double uniform(std::mt19937_64& g, double lo, double hi) {
    const double u = static_cast<double>(g() >> 11) * (1.0 / 9007199254740992.0);
    return lo + (hi - lo) * u;
}

bool cancelled(const std::atomic<bool>* c) { return c && c->load(std::memory_order_relaxed); }
}  // namespace

std::string validate(const BatchConfig& c) {
    if (c.cases < 1 || c.cases > 100000) return "Case count must be between 1 and 100,000.";
    if (c.elements < 1 || c.elements > 2'000'000) return "Mesh size must be between 1 and 2,000,000 elements.";
    if (c.repetitions < 1 || c.repetitions > 50) return "Repetitions must be between 1 and 50.";
    if (c.maxWorkers < 1 || c.maxWorkers > 256) return "Max workers must be between 1 and 256.";
    if (static_cast<long long>(c.cases) * (c.elements + 1) > kMaxStoredValues)
        return "cases x (elements+1) exceeds 40 million stored temperatures; reduce cases or mesh size.";
    return {};
}

std::vector<RodProblem> generateBatch(std::uint64_t seed, int count) {
    std::mt19937_64 g(seed);
    std::vector<RodProblem> out(static_cast<std::size_t>(count));
    for (auto& p : out) {
        p.k_W_mK = uniform(g, 1.0, 400.0);       // polymer .. copper
        p.q_W_m3 = uniform(g, 1.0e3, 1.0e5);     // positive generation
    }
    return out;
}

// ---------------- WorkerPool ----------------

WorkerPool::WorkerPool(int workers) {
    if (workers < 1) throw std::invalid_argument("WorkerPool needs at least one worker");
    threads_.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { workerLoop(); });
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : threads_) t.join();
}

void WorkerPool::parallelFor(std::size_t count, const std::function<void(std::size_t)>& fn) {
    std::unique_lock<std::mutex> lk(mu_);
    task_ = &fn;
    count_ = count;
    next_.store(0);
    finished_ = 0;
    error_ = nullptr;
    ++generation_;
    wake_.notify_all();
    done_.wait(lk, [this] { return finished_ == size(); });
    task_ = nullptr;
    if (error_) std::rethrow_exception(error_);
}

void WorkerPool::workerLoop() {
    std::uint64_t seen = 0;
    for (;;) {
        const std::function<void(std::size_t)>* task;
        std::size_t count;
        {
            std::unique_lock<std::mutex> lk(mu_);
            wake_.wait(lk, [&] { return stop_ || generation_ != seen; });
            if (stop_) return;
            seen = generation_;
            task = task_;
            count = count_;
        }
        try {
            for (std::size_t i = next_.fetch_add(1); i < count; i = next_.fetch_add(1)) (*task)(i);
        } catch (...) {
            std::lock_guard<std::mutex> lk(mu_);
            if (!error_) error_ = std::current_exception();
            next_.store(count);  // stop handing out further work
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            ++finished_;
        }
        done_.notify_one();
    }
}

// ---------------- Solves ----------------

BatchResults solveSerial(const std::vector<RodProblem>& cases, int elements, const std::atomic<bool>* cancel) {
    BatchResults out(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (cancelled(cancel)) throw Cancelled();
        out[i] = solveRod(cases[i], elements).T_C;
    }
    return out;
}

BatchResults solveParallel(WorkerPool& pool, const std::vector<RodProblem>& cases, int elements,
                           const std::atomic<bool>* cancel) {
    BatchResults out(cases.size());  // each slot written by exactly one task -> deterministic order, no locks
    pool.parallelFor(cases.size(), [&](std::size_t i) {
        if (cancelled(cancel)) throw Cancelled();
        out[i] = solveRod(cases[i], elements).T_C;
    });
    return out;
}

double maxAbsDifference(const BatchResults& a, const BatchResults& b) {
    if (a.size() != b.size()) return INFINITY;
    double m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return INFINITY;
        for (std::size_t j = 0; j < a[i].size(); ++j) m = std::max(m, std::abs(a[i][j] - b[i][j]));
    }
    return m;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

BenchmarkReport runBenchmark(const BatchConfig& cfg, const std::atomic<bool>* cancel, const ProgressFn& progress) {
    const std::string err = validate(cfg);
    if (!err.empty()) throw std::invalid_argument(err);

    using clock = std::chrono::steady_clock;
    BenchmarkReport rep;
    rep.config = cfg;
    rep.logicalCpus = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int maxW = std::min(cfg.maxWorkers, rep.logicalCpus);

    std::vector<int> workerCounts;
    for (int w = 1; w <= maxW; w = (w < 2 ? w + 1 : w * 2)) workerCounts.push_back(w);
    if (workerCounts.back() != maxW) workerCounts.push_back(maxW);

    // Case generation is input preparation and is excluded from compute timings.
    const auto cases = generateBatch(cfg.seed, cfg.cases);
    const int runsPerConfig = cfg.repetitions + 1;  // +1 warm-up
    const double totalRuns = static_cast<double>(runsPerConfig) * (1 + workerCounts.size());
    int runsDone = 0;
    auto tick = [&](const std::string& phase) {
        ++runsDone;
        if (progress) progress(runsDone / totalRuns, phase);
    };

    // Serial reference.
    BatchResults reference;
    rep.serial.workers = 1;
    for (int r = 0; r < runsPerConfig; ++r) {
        const auto t0 = clock::now();
        auto res = solveSerial(cases, cfg.elements, cancel);
        const double s = std::chrono::duration<double>(clock::now() - t0).count();
        if (r == 0) reference = std::move(res);  // warm-up run doubles as the reference result
        else rep.serial.seconds.push_back(s);
        tick(r == 0 ? "serial warm-up" : "serial run " + std::to_string(r));
    }
    rep.serial.median = median(rep.serial.seconds);

    rep.agreementOk = true;
    for (int w : workerCounts) {
        WorkerPool pool(w);  // thread creation happens outside the timed region
        WorkerTiming t;
        t.workers = w;
        for (int r = 0; r < runsPerConfig; ++r) {
            const auto t0 = clock::now();
            auto res = solveParallel(pool, cases, cfg.elements, cancel);
            const double s = std::chrono::duration<double>(clock::now() - t0).count();
            if (r > 0) t.seconds.push_back(s);
            t.maxAbsDiffVsSerial = std::max(t.maxAbsDiffVsSerial, maxAbsDifference(reference, res));
            tick(std::to_string(w) + " worker(s) " + (r == 0 ? "warm-up" : "run " + std::to_string(r)));
        }
        t.median = median(t.seconds);
        if (t.maxAbsDiffVsSerial > 1e-8) rep.agreementOk = false;
        rep.pool.push_back(t);
    }
    const double t1 = rep.pool.front().median;
    for (auto& t : rep.pool) {
        t.speedup = t.median > 0 ? t1 / t.median : 0;
        t.efficiency = t.speedup / t.workers;
    }
    rep.serial.speedup = rep.serial.median > 0 ? t1 / rep.serial.median : 0;
    rep.serial.efficiency = rep.serial.speedup;
    return rep;
}

}  // namespace wb
