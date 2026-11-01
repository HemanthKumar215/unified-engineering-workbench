#include "sysinfo.h"

#include <cstring>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#endif

#ifndef WB_COMPILER
#define WB_COMPILER "unknown"
#endif
#ifndef WB_OPT_FLAGS
#define WB_OPT_FLAGS "unknown"
#endif

namespace wb {

namespace {

std::string cpuBrand() {
    unsigned int regs[12] = {};
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    if (__get_cpuid_max(0x80000000, nullptr) < 0x80000004) return "unknown";
    for (unsigned int i = 0; i < 3; ++i)
        __get_cpuid(0x80000002 + i, &regs[4 * i], &regs[4 * i + 1], &regs[4 * i + 2], &regs[4 * i + 3]);
#elif defined(_MSC_VER)
    int r[4];
    for (int i = 0; i < 3; ++i) {
        __cpuid(r, 0x80000002 + i);
        std::memcpy(&regs[4 * i], r, sizeof(r));
    }
#else
    return "unknown";
#endif
    char brand[49] = {};
    std::memcpy(brand, regs, 48);
    std::string s(brand);
    const auto b = s.find_first_not_of(' ');
    return b == std::string::npos ? "unknown" : s.substr(b);
}

}  // namespace

SystemInfo querySystemInfo() {
    SystemInfo si;
    si.cpuName = cpuBrand();
    si.logicalCpus = static_cast<int>(std::thread::hardware_concurrency());
    si.compiler = WB_COMPILER;
    si.optFlags = WB_OPT_FLAGS;
#if defined(_WIN32)
    si.os = "Windows";
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len) {
        std::vector<char> buf(len);
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &len)) {
            for (DWORD off = 0; off < len;) {
                auto* e = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
                if (e->Relationship == RelationProcessorCore) ++si.physicalCores;
                off += e->Size;
            }
        }
    }
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) si.ramGiB = static_cast<double>(ms.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
#else
    si.os = "non-Windows";
#endif
    return si;
}

unsigned long long processPageFaults() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.PageFaultCount;
#endif
    return 0;
}

}  // namespace wb
