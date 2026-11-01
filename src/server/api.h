// Maps HTTP routes onto the C++ core. JSON in/out only; no numerics here.
#pragma once
#include <atomic>
#include <chrono>
#include <string>

#include "hardware.h"
#include "http_server.h"
#include "jobs.h"

namespace wb {

struct AppContext {
    std::string appDir;   // static frontend files
    std::string dataDir;  // bundled example data
    std::chrono::steady_clock::time_point processStart;
    double listenReady_s = 0;          // process start -> socket listening
    std::atomic<double> uiReady_s{0};  // process start -> UI reported ready
    std::atomic<long long> lastHeartbeatMs{0};
    std::atomic<long long> goodbyeAtMs{0};
    std::atomic<bool> shutdownRequested{false};
    BenchmarkJobs bench;
    DeviceManager devices;

    long long nowMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - processStart).count();
    }
};

HttpResponse handleRequest(AppContext& ctx, const HttpRequest& req);

}  // namespace wb
