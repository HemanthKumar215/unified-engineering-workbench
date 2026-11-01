// Diagnostic (not part of the test suite): per-worker, per-case solve times for one
// batch configuration, to explain poor scaling observed at large memory footprints.
//   diag_scaling <cases> <elements> <workers> <mode>
//   mode 0: persistent workspaces   1: fresh workspaces per run   2: mode 1 + compare against a reference
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "batch.h"
#include "fea1d.h"

static std::vector<wb::RodProblem> generateBatchCopy(int n) { return wb::generateBatch(20261101, n); }

int main(int argc, char** argv) {
    const int cases = argc > 1 ? std::atoi(argv[1]) : 192;
    const int elements = argc > 2 ? std::atoi(argv[2]) : 100000;
    const int workers = argc > 3 ? std::atoi(argv[3]) : 2;
    const int mode = argc > 4 ? std::atoi(argv[4]) : 0;
    wb::BatchResults reference;
    if (mode == 2) reference = wb::solveSerial(generateBatchCopy(cases), elements);
    const auto batch = wb::generateBatch(20261101, cases);
    wb::WorkerPool pool(workers);
    std::vector<wb::FeaWorkspace> ws(static_cast<std::size_t>(workers));
    wb::BatchResults out(batch.size());
    if (mode == 3) wb::solveSerial(batch, elements, out);
    std::vector<double> caseMs(batch.size());
    std::vector<int> caseWorker(batch.size());
    using clock = std::chrono::steady_clock;

    for (int rep = 0; rep < 3; ++rep) {
        const auto t0 = clock::now();
        if (mode >= 1) ws.assign(static_cast<std::size_t>(workers), wb::FeaWorkspace{});
        pool.parallelFor(batch.size(), [&](std::size_t i, int w) {
            const auto s = clock::now();
            wb::solveRodTemperatures(batch[i], elements, ws[static_cast<std::size_t>(w)], out[i]);
            caseMs[i] = std::chrono::duration<double, std::milli>(clock::now() - s).count();
            caseWorker[i] = w;
        });
        const double total = std::chrono::duration<double>(clock::now() - t0).count();
        std::printf("rep %d: wall %.4f s\n", rep, total);
        for (int w = 0; w < workers; ++w) {
            std::vector<double> mine;
            for (std::size_t i = 0; i < batch.size(); ++i)
                if (caseWorker[i] == w) mine.push_back(caseMs[i]);
            std::sort(mine.begin(), mine.end());
            if (mine.empty()) continue;
            double sum = 0;
            for (double v : mine) sum += v;
            std::printf("  worker %d: %3zu cases, busy %.4f s, per-case min %.3f / median %.3f / max %.3f ms\n", w,
                        mine.size(), sum / 1000, mine.front(), mine[mine.size() / 2], mine.back());
        }
    }
    return 0;
}
