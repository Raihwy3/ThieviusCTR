#pragma once

#include "launcher_ui_3ds.h"
#include <3ds.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <sys/iosupport.h>

extern "C" u32 __ctru_linear_heap_size;

/* Flicker-free bottom-screen (40x30) FPS, CPU, GPU, and RAM monitor.
 * Redirects engine stdout/std::cout to stderr so only monitor text draws to GFX_BOTTOM,
 * always starting at column 1 and keeping row lengths <= 39 columns (no scroll/wrap). */
class CanePerfMonitor3ds {
public:
    static constexpr double kTicksPerMs = 268111.856;
    static constexpr u64 kRefreshTicks = 67027964ull; // ~250 ms
    static constexpr size_t kVramTotalBytes = 6u * 1024u * 1024u;

    ~CanePerfMonitor3ds() {
        restoreStdout();
    }

    void init(PrintConsole* console, int model) {
        bottom_ = console;
        consoleModel_ = model;
        initialized_ = (bottom_ != nullptr);
        if (!initialized_) return;

        std::fflush(stdout);
        if (!redirected_) {
            consoleDotab_ = const_cast<devoptab_t*>(devoptab_list[STD_OUT]);
            if (consoleDotab_ && consoleDotab_->write_r) {
                static devoptab_t sStderrStdoutDotab = makeRedirectDotab();
                devoptab_list[STD_OUT] = &sStderrStdoutDotab;
                redirected_ = true;
            }
        }

        consoleSelect(bottom_);
        conPrintf("\x1b[0m\x1b[2J\x1b[H");

        conPrintf("\x1b[36;1m"
                  "\x1b[2;1H +------------------------------------+"
                  "\x1b[3;1H |   \x1b[33;1mTHIEVIUS CTR    SYSTEM MONITOR\x1b[36;1m   |"
                  "\x1b[4;1H +------------------------------------+"
                  "\x1b[17;1H -- MEMORY ----------------------------"
                  "\x1b[25;1H --------------------------------------"
                  "\x1b[0m");
        conPrintf("\x1b[27;1H \x1b[37;1mSYS: %-32.32s\x1b[0m ", CaneLauncherConsoleName(consoleModel_));
        conPrintf("\x1b[29;1H \x1b[37;1mSELECT+START: Exit\x1b[0m                    ");

        lastSwap_ = false;
        haveSwap_ = false;
        resetWindow();
    }

    void setTargetFps(unsigned fps) { targetFps_ = (fps == 30u) ? 30u : 60u; }

    void restoreStdout() {
        if (!redirected_ || !consoleDotab_) return;
        std::fflush(stdout);
        devoptab_list[STD_OUT] = consoleDotab_;
        redirected_ = false;
    }

    void resetWindow() {
        winStartTick_ = svcGetSystemTick();
        winFrames_ = 0;
        winSimMs_ = 0.0;
        winPrepMs_ = 0.0;
        winCmdMs_ = 0.0;
        winCpuMs_ = 0.0;
        winGpuMs_ = 0.0;
    }

    void setStatus(const char* text) {
        if (!initialized_) return;
        consoleSelect(bottom_);
        conPrintf("\x1b[26;1H \x1b[37;1mLVL: %-32.32s\x1b[0m ", text ? text : "");
    }

    void recordFrame(double simMs, double prepMs, double cmdMs, double gpuMs) {
        ++winFrames_;
        winSimMs_ += simMs;
        winPrepMs_ += prepMs;
        winCmdMs_ += cmdMs;
        winCpuMs_ += simMs + prepMs + cmdMs;
        winGpuMs_ += gpuMs;
    }

    void updateIfDue(const char* levelName,
                     unsigned levelId,
                     unsigned drawCalls,
                     unsigned triangles,
                     unsigned packets,
                     bool swapBA,
                     bool force = false) {
        if (!initialized_) return;
        const u64 nowTick = svcGetSystemTick();
        const u64 dtTicks = nowTick - winStartTick_;
        const bool swapChanged = !haveSwap_ || (swapBA != lastSwap_);
        if (!force && !swapChanged && dtTicks < kRefreshTicks) return;

        const double dtMs = std::max(1.0, double(dtTicks) / kTicksPerMs);
        const double invFrames = winFrames_ > 0 ? (1.0 / double(winFrames_)) : 0.0;

        const double fps = std::clamp(double(winFrames_) * 1000.0 / dtMs, 0.0, 99.9);
        const double avgFrameMs = winFrames_ > 0 ? std::clamp(dtMs * invFrames, 0.0, 999.9) : 0.0;
        const double targetFps = double(targetFps_);
        const unsigned fpsPct = unsigned(std::clamp(fps / targetFps * 100.0 + 0.5, 0.0, 999.0));

        const double cpuPct = std::clamp(winCpuMs_ / dtMs * 100.0, 0.0, 100.0);
        const double avgCpuMs = std::clamp(winCpuMs_ * invFrames, 0.0, 999.9);
        const double avgSimMs = std::clamp(winSimMs_ * invFrames, 0.0, 999.9);
        const double avgPrepMs = std::clamp(winPrepMs_ * invFrames, 0.0, 999.9);
        const double avgCmdMs = std::clamp(winCmdMs_ * invFrames, 0.0, 999.9);

        const double gpuPct = std::clamp(winGpuMs_ / dtMs * 100.0, 0.0, 100.0);
        const double avgGpuMs = std::clamp(winGpuMs_ * invFrames, 0.0, 999.99);

        const size_t heapTotal = std::max<size_t>(1u, size_t(envGetHeapSize()));
        const size_t heapUsed = std::min(heapTotal, size_t(mallinfo().uordblks));
        const unsigned heapPct = unsigned((uint64_t(heapUsed) * 100u) / heapTotal);

        const size_t linFree = size_t(linearSpaceFree());
        const size_t linTotal = std::max<size_t>(size_t(__ctru_linear_heap_size), linFree);
        const size_t linUsed = (linTotal > linFree) ? (linTotal - linFree) : 0u;
        const unsigned linPct = linTotal > 0 ? unsigned((uint64_t(linUsed) * 100u) / linTotal) : 0u;

        const size_t vramFree = std::min(kVramTotalBytes, size_t(vramSpaceFree()));
        const size_t vramUsed = kVramTotalBytes - vramFree;
        const unsigned vramPct = unsigned((uint64_t(vramUsed) * 100u) / kVramTotalBytes);

        consoleSelect(bottom_);

        if (winFrames_ > 0 || force) {
            const char* fpsColor = (fps >= 27.0) ? "32;1" : ((fps >= 20.0) ? "33;1" : "31;1");
            conPrintf("\x1b[6;1H \x1b[37;1mFPS  %4.1f /%2u.0      frame %5.1f ms\x1b[0m ", fps, targetFps_, avgFrameMs);
            printBarLine(7, 7, 20, fps / targetFps, fpsColor, std::min(fpsPct, 999u));

            const char* cpuColor = (cpuPct < 70.0) ? "32;1" : ((cpuPct < 90.0) ? "33;1" : "31;1");
            conPrintf("\x1b[9;1H \x1b[37;1mCPU %5.1f%%          frame %5.1f ms\x1b[0m ", cpuPct, avgCpuMs);
            printBarLine(10, 7, 20, cpuPct / 100.0, cpuColor, unsigned(std::clamp(cpuPct + 0.5, 0.0, 100.0)));
            conPrintf("\x1b[11;1H   Sim %5.1f  Prep %5.1f  Cmd %5.1f ms ", avgSimMs, avgPrepMs, avgCmdMs);

            const char* gpuColor = (gpuPct < 70.0) ? "32;1" : ((gpuPct < 90.0) ? "33;1" : "31;1");
            conPrintf("\x1b[13;1H \x1b[37;1mGPU %5.1f%%          draw  %5.2f ms\x1b[0m ", gpuPct, avgGpuMs);
            printBarLine(14, 7, 20, gpuPct / 100.0, gpuColor, unsigned(std::clamp(gpuPct + 0.5, 0.0, 100.0)));
            conPrintf("\x1b[15;1H   Draw %4u  Tri %6u  Pkt %5u   ",
                      std::min(drawCalls, 9999u),
                      std::min(triangles, 999999u),
                      std::min(packets, 99999u));
        }

        printMemBlock(18, "HEAP ", heapUsed, heapTotal, heapPct);
        printMemBlock(20, "FCRAM", linUsed, linTotal, linPct);
        printMemBlock(22, "VRAM ", vramUsed, kVramTotalBytes, vramPct);

        char lvlLine[40]{};
        std::snprintf(lvlLine, sizeof lvlLine, "%s (0x%03X)", levelName ? levelName : "Unknown", levelId & 0xFFFu);
        conPrintf("\x1b[26;1H \x1b[37;1mLVL: %-32.32s\x1b[0m ", lvlLine);
        conPrintf("\x1b[28;1H %-37.37s ",
                  swapBA ? "A:Jump B:Act Y:Cane START:Pause"
                         : "B:Jump A:Act Y:Cane START:Pause");

        lastSwap_ = swapBA;
        haveSwap_ = true;
        if (winFrames_ > 0 || dtTicks >= kRefreshTicks) resetWindow();
    }

private:
    unsigned targetFps_ = 60;
    static ssize_t redirectStdoutToStderr(struct _reent*, void*, const char* ptr, size_t len) {
        if (ptr && len > 0u && stderr) std::fwrite(ptr, 1, len, stderr);
        return ssize_t(len);
    }

    static devoptab_t makeRedirectDotab() {
        devoptab_t d{};
        d.name = "stdout_log";
        d.write_r = redirectStdoutToStderr;
        return d;
    }

    void conPrintf(const char* fmt, ...) const {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);
        if (n <= 0) return;
        const size_t len = std::min(size_t(n), sizeof(buf) - 1u);
        if (consoleDotab_ && consoleDotab_->write_r) {
            consoleDotab_->write_r(nullptr, nullptr, buf, len);
        } else {
            std::fwrite(buf, 1, len, stdout);
            std::fflush(stdout);
        }
    }

    void printBarLine(int row, int col, int width, double ratio, const char* colorCode, unsigned pctText) const {
        const double clamped = std::clamp(ratio, 0.0, 1.0);
        const int filled = std::clamp(int(clamped * double(width) + 0.5), 0, width);
        char bar[32]{};
        for (int i = 0; i < width && i < int(sizeof(bar) - 1); ++i) {
            bar[i] = (i < filled) ? '=' : '-';
        }
        const int lead = std::max(0, col - 1);
        if (pctText != ~0u) {
            conPrintf("\x1b[%d;1H%*s[\x1b[%sm%s\x1b[0m] %3u%%    ", row, lead, "", colorCode, bar, pctText);
        } else {
            conPrintf("\x1b[%d;1H%*s[\x1b[%sm%s\x1b[0m]         ", row, lead, "", colorCode, bar);
        }
    }

    void printMemBlock(int row, const char* label, size_t usedBytes, size_t totalBytes, unsigned pct) const {
        const double usedMiB = double(usedBytes) / (1024.0 * 1024.0);
        const double totalMiB = double(totalBytes) / (1024.0 * 1024.0);
        const unsigned clampedPct = std::min(pct, 100u);
        const char* color = (clampedPct < 75u) ? "32;1" : ((clampedPct < 90u) ? "33;1" : "31;1");
        conPrintf("\x1b[%d;1H \x1b[37;1m%-5.5s %5.1f / %5.1f MiB        %3u%%\x1b[0m ",
                  row, label, usedMiB, totalMiB, clampedPct);
        printBarLine(row + 1, 8, 20, double(clampedPct) / 100.0, color, ~0u);
    }

    PrintConsole* bottom_ = nullptr;
    devoptab_t* consoleDotab_ = nullptr;
    int consoleModel_ = CFG_MODEL_N3DS;
    bool initialized_ = false;
    bool redirected_ = false;
    bool lastSwap_ = false;
    bool haveSwap_ = false;
    u64 winStartTick_ = 0;
    unsigned winFrames_ = 0;
    double winSimMs_ = 0.0;
    double winPrepMs_ = 0.0;
    double winCmdMs_ = 0.0;
    double winCpuMs_ = 0.0;
    double winGpuMs_ = 0.0;
};
