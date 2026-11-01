// Host description recorded alongside every benchmark.
#pragma once
#include <string>

namespace wb {

struct SystemInfo {
    std::string cpuName;
    int logicalCpus = 0;
    int physicalCores = 0;   // 0 if unknown
    double ramGiB = 0;       // 0 if unknown
    std::string os;
    std::string compiler;
    std::string optFlags;
};

SystemInfo querySystemInfo();

// Cumulative page faults (soft + hard) of this process; 0 where unsupported.
unsigned long long processPageFaults();

}  // namespace wb
