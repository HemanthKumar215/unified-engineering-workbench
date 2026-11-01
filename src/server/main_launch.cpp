// Launch.exe entry point: starts the loopback server, opens the default browser on the UI,
// and exits when the UI says goodbye, the user clicks Quit, or no page has been seen for a while.
//
// Options (for testing / editor integration):  --no-browser   --port <first-port>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "api.h"
#include "http_server.h"

namespace {

std::string exeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    std::wstring w(buf, n);
    w = w.substr(0, w.find_last_of(L"\\/"));
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(len > 0 ? len - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

bool exists(const std::string& path) {
    const DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

// Packaged layout first (app/ next to Launch.exe), then the source tree when run from build/.
std::string locate(const std::string& base, const char* packaged, const char* dev) {
    for (const std::string& cand : {base + "/" + packaged, base + "/../" + dev, base + "/../../" + dev})
        if (exists(cand)) return cand;
    return {};
}

std::ofstream g_log;
void logLine(const std::string& msg) {
    if (!g_log) return;
    const std::time_t t = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    g_log << ts << "  " << msg << std::endl;
}

void fatal(const std::string& msg) {
    logLine("FATAL: " + msg);
    MessageBoxW(nullptr, widen(msg).c_str(), L"Unified Engineering Workbench", MB_OK | MB_ICONERROR);
}

}  // namespace

int main(int argc, char** argv) {
    wb::AppContext ctx;
    ctx.processStart = std::chrono::steady_clock::now();

    bool openBrowser = true;
    int firstPort = 8765;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-browser") == 0) openBrowser = false;
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) firstPort = std::atoi(argv[++i]);
    }

    const std::string base = exeDir();
    CreateDirectoryW(widen(base + "/logs").c_str(), nullptr);
    g_log.open(std::filesystem::path(widen(base + "/logs/workbench.log")), std::ios::app);
    if (!g_log) {  // read-only location (e.g. inside a ZIP viewer): fall back to %TEMP%
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        g_log.open(std::filesystem::path(std::wstring(tmp) + L"workbench.log"), std::ios::app);
    }
    logLine("Launch.exe starting from " + base);

    ctx.appDir = locate(base, "app", "src/frontend");
    ctx.dataDir = locate(base, "data", "data");
    if (ctx.appDir.empty()) {
        fatal("The 'app' folder was not found next to Launch.exe.\n\nExtract the whole ZIP (not just Launch.exe) and run Launch.exe from the extracted folder.");
        return 1;
    }
    if (ctx.dataDir.empty()) logLine("WARNING: data/ folder not found; example files will be unavailable.");

    wb::HttpServer server;
    const int port = server.listen(firstPort, firstPort + 40);
    if (port == 0) {
        fatal("Could not open a local port between " + std::to_string(firstPort) + " and " + std::to_string(firstPort + 40) +
              ".\n\nClose other running copies of the workbench and try again.");
        return 1;
    }
    ctx.listenReady_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - ctx.processStart).count();
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    logLine("Listening on " + url + " (launch-to-listen " + std::to_string(ctx.listenReady_s * 1000) + " ms)");
    // Editor/terminal runs: print the URL in case the browser does not open.
    std::printf("Unified Engineering Workbench running at %s\n", url.c_str());
    std::fflush(stdout);

    if (openBrowser) ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    // Lifetime watchdog. Browsers throttle timers in background tabs to ~1/min, so the idle
    // timeout is generous; an explicit goodbye (tab closed) shortens it to a grace period
    // that still survives a page reload.
    std::thread watchdog([&] {
        constexpr long long kFirstContactMs = 5 * 60 * 1000, kIdleMs = 4 * 60 * 1000, kGoodbyeGraceMs = 15 * 1000;
        while (!ctx.shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const long long now = ctx.nowMs(), last = ctx.lastHeartbeatMs.load(), bye = ctx.goodbyeAtMs.load();
            if (ctx.bench.running()) continue;
            if (bye && now - bye > kGoodbyeGraceMs && last <= bye + 1000) {
                logLine("UI closed; shutting down.");
                break;
            }
            if (last == 0 ? now > kFirstContactMs : now - last > kIdleMs) {
                logLine("No UI activity; shutting down.");
                break;
            }
        }
        ctx.shutdownRequested = true;
        server.stop();
    });

    server.serve([&](const wb::HttpRequest& r) { return wb::handleRequest(ctx, r); });
    watchdog.join();
    logLine("Stopped cleanly.");
    return 0;
}
