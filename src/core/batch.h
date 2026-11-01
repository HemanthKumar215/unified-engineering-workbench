// Batch of independent rod solves, executed serially or on a bounded worker pool.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fea1d.h"

namespace wb {

struct BatchConfig {
    std::uint64_t seed = 20261101;
    int cases = 64;
    int elements = 20000;
    int repetitions = 5;   // measured repetitions (one extra warm-up run is not measured)
    int maxWorkers = 4;    // capped at available logical CPUs
};

std::string validate(const BatchConfig& c);

// Reproducible case list: same seed -> identical k and q values on every machine
// (std::mt19937_64 is fully specified by the standard; we map its raw output ourselves
// instead of using std::uniform_real_distribution, whose algorithm is implementation-defined).
std::vector<RodProblem> generateBatch(std::uint64_t seed, int count);

struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("cancelled") {}
};

// Fixed-size pool of std::threads. parallelFor hands out indices dynamically through an
// atomic counter; the calling thread only waits, so exactly `workers` threads compute.
// The task receives (index, worker) so callers can keep per-worker scratch state.
class WorkerPool {
public:
    explicit WorkerPool(int workers);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    int size() const { return static_cast<int>(threads_.size()); }
    void parallelFor(std::size_t count, const std::function<void(std::size_t, int)>& fn);

private:
    void workerLoop(int worker);

    std::vector<std::thread> threads_;
    std::mutex mu_;
    std::condition_variable wake_, done_;
    const std::function<void(std::size_t, int)>* task_ = nullptr;
    std::size_t count_ = 0;
    std::atomic<std::size_t> next_{0};
    std::uint64_t generation_ = 0;
    int finished_ = 0;
    bool stop_ = false;
    std::exception_ptr error_;
};

using BatchResults = std::vector<std::vector<double>>;  // nodal temperatures, input order

// Both throw Cancelled if *cancel becomes true; partial results are never returned.
// The overloads taking `out` reuse its existing buffers (no allocation on repeated runs).
void solveSerial(const std::vector<RodProblem>& cases, int elements, BatchResults& out,
                 const std::atomic<bool>* cancel = nullptr);
void solveParallel(WorkerPool& pool, const std::vector<RodProblem>& cases, int elements, BatchResults& out,
                   const std::atomic<bool>* cancel = nullptr);
BatchResults solveSerial(const std::vector<RodProblem>& cases, int elements, const std::atomic<bool>* cancel = nullptr);
BatchResults solveParallel(WorkerPool& pool, const std::vector<RodProblem>& cases, int elements,
                           const std::atomic<bool>* cancel = nullptr);

double maxAbsDifference(const BatchResults& a, const BatchResults& b);

struct WorkerTiming {
    int workers = 0;
    std::vector<double> seconds;  // measured repetitions
    double median = 0;
    double speedup = 0;           // T1 / Tp
    double efficiency = 0;        // speedup / p
    double maxAbsDiffVsSerial = 0;
    unsigned long long pageFaults = 0;  // during measured runs (OS counter)
};

struct BenchmarkReport {
    BatchConfig config;
    int logicalCpus = 0;
    WorkerTiming serial;              // plain loop, no pool
    std::vector<WorkerTiming> pool;   // 1, 2, ... workers
    bool agreementOk = false;
};

using ProgressFn = std::function<void(double fraction, const std::string& phase)>;

BenchmarkReport runBenchmark(const BatchConfig& cfg, const std::atomic<bool>* cancel = nullptr,
                             const ProgressFn& progress = nullptr);

double median(std::vector<double> v);

}  // namespace wb
