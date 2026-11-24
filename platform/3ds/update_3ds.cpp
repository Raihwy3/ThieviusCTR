#include "update_3ds.h"
#include "update_core.h"
#include "update_http.h"
#include "version_3ds.h"
#include "launcher_ui_3ds.h"
#include "update_ca.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <unistd.h>

namespace {

PrintConsole* sTopConsole = nullptr;
PrintConsole* sBottomConsole = nullptr;

struct UpdateJob {
    int cia = 0;
    int download = 0;
    int cancel = 0;
    int done = 0;
    int ok = 0;
    unsigned received = 0;
    unsigned total = 0;
    char metadata[65537]{};
    char error[160]{};
    char file[640]{};
    CaneUpdateRelease release{};
    Thread worker = nullptr;
};

UpdateJob sJob{};

void logUpdate(std::FILE* logFile, const char* fmt, ...) {
    if (!logFile) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(logFile, fmt, args);
    std::fputc('\n', logFile);
    std::fflush(logFile);
    va_end(args);
}

int isCancelled(void*) {
    return __atomic_load_n(&sJob.cancel, __ATOMIC_RELAXED);
}

void onProgress(void*, size_t done, size_t total) {
    __atomic_store_n(&sJob.received, unsigned(done), __ATOMIC_RELAXED);
    __atomic_store_n(&sJob.total, unsigned(total), __ATOMIC_RELAXED);
}

void updateWorker(void*) {
    void* socketBuffer = memalign(0x1000, 256u * 1024u);
    if (!socketBuffer) {
        std::snprintf(sJob.error, sizeof sJob.error, "Out of memory for network buffer.");
        __atomic_store_n(&sJob.done, 1, __ATOMIC_RELEASE);
        return;
    }
    if (R_FAILED(socInit(static_cast<u32*>(socketBuffer), 256u * 1024u))) {
        std::snprintf(sJob.error, sizeof sJob.error, "Failed initializing 3DS sockets.");
        free(socketBuffer);
        __atomic_store_n(&sJob.done, 1, __ATOMIC_RELEASE);
        return;
    }

    CaneUpdateTransfer t{};
    t.cancel = isCancelled;
    t.progress = onProgress;
    const char* url = CANE_UPDATE_API;
    if (sJob.download) {
        url = sJob.release.url;
        t.file = sJob.file;
        t.expected = sJob.release.bytes;
        t.digest = sJob.release.digest;
    } else {
        t.memory = sJob.metadata;
        t.capacity = sizeof sJob.metadata;
    }

    sJob.ok = cane_update_http(url, &t, cane_update_ca, sizeof(cane_update_ca) - 1u,
                               sJob.error, sizeof sJob.error);
    if (sJob.ok && !sJob.download) {
        sJob.ok = cane_update_release_for(sJob.metadata, t.length, CANE_RELEASE_TAG, sJob.cia, &sJob.release);
    }

    socExit();
    free(socketBuffer);
    __atomic_store_n(&sJob.done, 1, __ATOMIC_RELEASE);
}

void joinAndFreeWorker() {
    if (!sJob.worker) return;
    __atomic_store_n(&sJob.cancel, 1, __ATOMIC_RELAXED);
    threadJoin(sJob.worker, U64_MAX);
    threadFree(sJob.worker);
    sJob.worker = nullptr;
}

bool startJob(int download) {
    joinAndFreeWorker();
    sJob.download = download;
    __atomic_store_n(&sJob.cancel, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&sJob.done, 0, __ATOMIC_RELAXED);
    sJob.ok = 0;
    __atomic_store_n(&sJob.received, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&sJob.total, 0u, __ATOMIC_RELAXED);
    sJob.error[0] = '\0';
    // Joinable (detached = false) so sockets & socketBuffer are strictly released before game init.
    sJob.worker = threadCreate(updateWorker, nullptr, 128u * 1024u, 0x3f, 0, false);
    return sJob.worker != nullptr;
}

void drawActions(const char* action) {
    const char* back = std::strstr(action, "B:");
    if (std::strncmp(action, "A:", 2) == 0) {
        const int leftWidth = (back && back > action && (back - action) < 15) ? int(back - action) : 15;
        CaneLauncherText(21, 5, action, leftWidth);
    } else {
        CaneLauncherText(21, 5, "PLEASE WAIT", 15);
    }
    if (back) {
        CaneLauncherText(21, 23, back, 14);
    }
}

void drawUpdateUi(const char* stage, const char* message, const char* action,
                  unsigned done, unsigned total) {
    consoleSelect(sTopConsole);
    consoleClear();
    CaneLauncherTopChrome("LAUNCHER UPDATE", stage, nullptr);
    if (total > 0u) {
        CaneLauncherBar(20, 9, 30, done, total);
        std::printf("\x1b[22;23H%3u%%", CaneLauncherPercent(done, total));
    } else {
        CaneLauncherText(20, 14, "UPDATE SERVICE", 30);
    }
    CaneLauncherText(24, 5, "OPTIONAL LAUNCHER UPDATE", 40);
    CaneLauncherLine(26, 5, 40);
    CaneLauncherText(28, 5, "Installed " CANE_RELEASE_TAG, 40);

    consoleSelect(sBottomConsole);
    consoleClear();
    CaneLauncherBottomChrome("LAUNCHER UPDATE");
    CaneLauncherText(7, 5, "INSTALLED", 15);
    CaneLauncherText(8, 5, CANE_RELEASE_TAG, 15);
    if ((sJob.download || __atomic_load_n(&sJob.done, __ATOMIC_ACQUIRE)) && sJob.release.tag[0]) {
        CaneLauncherText(7, 23, "AVAILABLE", 14);
        CaneLauncherText(8, 23, sJob.release.tag, 14);
    }
    CaneLauncherMessage(12, message, 7);
    drawActions(action);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

int scanActionButtons() {
    hidScanInput();
    u32 keys = hidKeysDown();
    if (keys & KEY_TOUCH) {
        touchPosition p{};
        hidTouchRead(&p);
        if (p.py >= 145u && p.py <= 185u) {
            keys |= (p.px < 160u) ? KEY_A : KEY_B;
        }
    }
    return (keys & KEY_B) ? -1 : ((keys & KEY_A) ? 1 : 0);
}

int promptUser(const char* stage, const char* message, const char* action) {
    drawUpdateUi(stage, message, action, 0u, 0u);
    hidScanInput();
    while (aptMainLoop()) {
        const int key = scanActionButtons();
        if (key != 0) return key;
        gspWaitForVBlank();
    }
    return 0;
}

bool awaitJob(int download) {
    const u64 started = osGetTime();
    u64 paint = 0;
    while (aptMainLoop()) {
        if (__atomic_load_n(&sJob.done, __ATOMIC_ACQUIRE)) {
            const bool ok = sJob.ok != 0;
            joinAndFreeWorker();
            return ok;
        }
        if (scanActionButtons() < 0 || (!download && osGetTime() - started >= 4500ull)) {
            joinAndFreeWorker();
            return false;
        }
        const u64 now = osGetTime();
        if (now >= paint) {
            drawUpdateUi(
                download ? "DOWNLOADING RELEASE" : "CHECKING GITHUB RELEASES",
                download ? "Saving the launcher update..." : "Checking for a newer release.",
                download ? "B: Cancel" : "B: Skip",
                __atomic_load_n(&sJob.received, __ATOMIC_RELAXED),
                __atomic_load_n(&sJob.total, __ATOMIC_RELAXED));
            paint = now + 200ull;
        }
        gspWaitForVBlank();
    }
    joinAndFreeWorker();
    return false;
}

int stageProgress(size_t done, size_t total) {
    static u64 paint = 0;
    if (!aptMainLoop() || scanActionButtons() < 0) return 0;
    const u64 now = osGetTime();
    if (now >= paint) {
        drawUpdateUi("PREPARING UPDATE", "Checking the launcher files...", "B: Cancel",
                     unsigned(done), unsigned(total));
        paint = now + 100ull;
    }
    return 1;
}

} // namespace

bool CaneUpdateStartup3ds(const char* directory,
                          PrintConsole* top,
                          PrintConsole* bottom,
                          std::FILE* logFile,
                          bool installedTitle) {
    if (!directory || !*directory) return false;
    sTopConsole = top;
    sBottomConsole = bottom;
    sJob = UpdateJob{};
    sJob.cia = installedTitle;

    char error[160]{};
    if (!cane_update_recover(directory, error, sizeof error)) {
        logUpdate(logFile, "Updater recovery failed: %s", error);
        return promptUser("UPDATE RECOVERY", error, "A: Continue    B: Exit") > 0;
    }

    if (!installedTitle && !envIsHomebrew()) {
        logUpdate(logFile, "Updater skipped: not running from Homebrew (.3dsx)");
        return true;
    }

    char flag[640]{};
    const int flagLen = std::snprintf(flag, sizeof flag, "%s/no-update-check.flag", directory);
    if (flagLen > 0 && size_t(flagLen) < sizeof flag && access(flag, F_OK) == 0) {
        logUpdate(logFile, "Updater skipped: disabled by no-update-check.flag");
        return true;
    }

    u32 wifi = 0;
    if (R_FAILED(acInit())) {
        logUpdate(logFile, "Updater skipped: acInit unavailable");
        return true;
    }
    const Result connected = ACU_GetWifiStatus(&wifi);
    acExit();
    if (R_FAILED(connected) || wifi == 0u) {
        logUpdate(logFile, "Updater skipped: offline (no Wi-Fi connection)");
        return true;
    }

    logUpdate(logFile, "Updater: checking latest GitHub release (installed %s)", CANE_RELEASE_TAG);
    if (!startJob(0) || !awaitJob(0)) {
        joinAndFreeWorker();
        logUpdate(logFile, "Updater: no newer release applied (%s)",
                  sJob.error[0] ? sJob.error : "up to date, skipped, or timed out");
        return aptMainLoop();
    }

    logUpdate(logFile, "Updater: newer release available (%s, %zu bytes)",
              sJob.release.tag, sJob.release.bytes);
    const int choice = promptUser(
        "NEW RELEASE AVAILABLE",
        installedTitle ? "Download the CIA update?\n\nInstall it with FBI after closing\nthe game. Saves and settings stay." :
                         "Install this launcher update?\n\nGame files, saves and settings\nare kept intact.",
        installedTitle ? "A: Download    B: Not now" : "A: Install     B: Not now");
    if (choice <= 0) {
        logUpdate(logFile, "Updater: user deferred update %s", sJob.release.tag);
        return choice < 0;
    }

    const int partLen = std::snprintf(sJob.file, sizeof sJob.file, "%s/launcher-update.%s.part", directory, installedTitle ? "cia" : "zip");
    if (partLen <= 0 || size_t(partLen) >= sizeof sJob.file) {
        return promptUser("UPDATE NOT INSTALLED", "Update folder path is too long.", "A: Play        B: Exit") > 0;
    }

    if (!startJob(1) || !awaitJob(1)) {
        joinAndFreeWorker();
        logUpdate(logFile, "Updater: download failed (%s)", sJob.error[0] ? sJob.error : "cancelled");
        if (!aptMainLoop()) return false;
        return promptUser("UPDATE NOT INSTALLED",
                          "Download failed. The current\nlauncher has not been replaced.",
                          "A: Play        B: Exit") > 0;
    }

    if (installedTitle) {
        if (!cane_update_validate_cia(sJob.file, error, sizeof error)) {
            std::remove(sJob.file);
            logUpdate(logFile, "CIA updater: validation failed (%s)", error);
            return promptUser("CIA UPDATE NOT SAVED", error, "A: Play        B: Exit") > 0;
        }
        char cia[640]{};
        const int len = std::snprintf(cia, sizeof cia, "%s/launcher-update.cia", directory);
        if (len <= 0 || size_t(len) >= sizeof cia) {
            std::remove(sJob.file);
            return promptUser("CIA UPDATE NOT SAVED", "Update folder path is too long.", "A: Play        B: Exit") > 0;
        }
        std::remove(cia);
        if (std::rename(sJob.file, cia) != 0) {
            std::remove(sJob.file);
            return promptUser("CIA UPDATE NOT SAVED", "Unable to save update on SD card.", "A: Play        B: Exit") > 0;
        }
        logUpdate(logFile, "CIA updater: saved verified %s to %s", sJob.release.tag, cia);
        return promptUser("CIA UPDATE READY",
                          "Close the game. In FBI, install\n/3ds/ThieviusCTR/\nlauncher-update.cia.\nSaves and settings stay in place.",
                          "A: Exit        B: Play") < 0;
    }

    char archive[640]{};
    const int arcLen = std::snprintf(archive, sizeof archive, "%s/launcher-update.zip", directory);
    if (arcLen <= 0 || size_t(arcLen) >= sizeof archive) {
        std::remove(sJob.file);
        return promptUser("UPDATE NOT INSTALLED", "Update folder path is too long.", "A: Play        B: Exit") > 0;
    }
    std::remove(archive);
    if (std::rename(sJob.file, archive) != 0 ||
        !cane_update_install(archive, directory, stageProgress, error, sizeof error)) {
        std::remove(sJob.file);
        logUpdate(logFile, "Updater: install failed (%s)", error[0] ? error : "rename/install error");
        return promptUser("UPDATE NOT INSTALLED",
                          error[0] ? error : "Unable to save update on SD card.",
                          "A: Play        B: Exit") > 0;
    }

    logUpdate(logFile, "Updater: installed %s cleanly", sJob.release.tag);
    // Negative return means user pressed A: Exit to relaunch into the new .3dsx; B continues current build.
    return promptUser("UPDATE INSTALLED",
                      "Relaunch to use the new version.\n\nB continues this running build.",
                      "A: Exit        B: Play") < 0;
}
