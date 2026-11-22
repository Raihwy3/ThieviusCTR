#pragma once

#include <3ds.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* Dual-screen console UI modeled on Eyes of Gold's native launcher chrome.
 * Coordinates target libctru's 8px console grid: top 50x30, bottom 40x30. */

inline const char* CaneLauncherConsoleName(int model) {
    switch (model) {
    case CFG_MODEL_3DS: return "Nintendo 3DS";
    case CFG_MODEL_3DSXL: return "Nintendo 3DS XL";
    case CFG_MODEL_N3DS: return "New Nintendo 3DS";
    case CFG_MODEL_2DS: return "Nintendo 2DS";
    case CFG_MODEL_N3DSXL: return "New Nintendo 3DS XL";
    case CFG_MODEL_N2DSXL: return "New Nintendo 2DS XL";
    default: return "Nintendo 3DS";
    }
}

inline int CaneLauncherDetectConsoleModel() {
    int model = -1;
    if (R_SUCCEEDED(cfguInit())) {
        u8 cfgModel = 0;
        if (R_SUCCEEDED(CFGU_GetSystemModel(&cfgModel)))
            model = int(cfgModel);
        cfguExit();
    }
    if (model < 0 || model > 5) {
        bool newer = false;
        if (R_SUCCEEDED(APT_CheckNew3DS(&newer)))
            model = newer ? CFG_MODEL_N3DS : CFG_MODEL_3DS;
        else
            model = CFG_MODEL_3DS;
    }
    return model;
}

inline unsigned CaneLauncherPercent(unsigned done, unsigned total) {
    if (!total) return 0;
    const uint64_t pct = (uint64_t(done) * 100u) / total;
    return pct > 100u ? 100u : unsigned(pct);
}

inline unsigned CaneLauncherTotalPercent(const char* stage, unsigned done, unsigned total, unsigned previous) {
    unsigned base = previous;
    unsigned span = 0;
    if (!stage) stage = "";
    if (std::strcmp(stage, "Checking game files") == 0) {
        base = 0; span = 2;
    } else if (std::strcmp(stage, "Checking disc image") == 0) {
        base = 2; span = 3;
    } else if (std::strcmp(stage, "Extracting levels") == 0) {
        base = 5; span = 70;
    } else if (std::strcmp(stage, "Extracting music & banks") == 0) {
        base = 75; span = 5;
    } else if (std::strcmp(stage, "Decoding voice clips") == 0) {
        base = 80; span = 16;
    } else if (std::strcmp(stage, "Decoding extra audio") == 0) {
        base = 96; span = 3;
    } else if (std::strcmp(stage, "Finalizing setup") == 0) {
        base = 99; span = 1;
    } else if (std::strcmp(stage, "Vault ready") == 0) {
        base = 100; span = 0;
    }
    if (total > 0) {
        const unsigned bounded = done > total ? total : done;
        base += unsigned((uint64_t(bounded) * span) / total);
    }
    if (base < previous) base = previous;
    return base > 100u ? 100u : base;
}

inline void CaneLauncherPrintSanitized(const char* s, int width) {
    for (int i = 0; s && s[i] && i < width; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::putchar(c >= 32 && c < 127 ? c : ' ');
    }
}

inline void CaneLauncherText(int row, int col, const char* s, int width) {
    std::printf("\x1b[%d;%dH", row, col);
    CaneLauncherPrintSanitized(s, width);
}

inline void CaneLauncherLine(int row, int col, int width) {
    std::printf("\x1b[36m\x1b[%d;%dH", row, col);
    for (int i = 0; i < width; ++i) std::putchar('-');
    std::printf("\x1b[0m");
}

inline void CaneLauncherBar(int row, int col, int width, unsigned done, unsigned total) {
    const unsigned filled = (CaneLauncherPercent(done, total) * unsigned(width)) / 100u;
    std::printf("\x1b[33m\x1b[%d;%dH[", row, col);
    for (int i = 0; i < width; ++i) std::putchar(unsigned(i) < filled ? '=' : '-');
    std::printf("]\x1b[0m");
}

/* Word-wrap safely within a 32-column panel without allowing control chars. */
inline void CaneLauncherMessage(int row, const char* s, int lines) {
    int column = 0;
    std::printf("\x1b[%d;5H", row);
    while (s && *s && lines > 0) {
        if (*s == '\n') {
            ++s;
            column = 0;
            ++row;
            if (--lines) std::printf("\x1b[%d;5H", row);
            continue;
        }
        while (*s == ' ' && column == 0) ++s;
        if (!*s) break;
        int length = 0;
        while (s[length] && s[length] != ' ' && s[length] != '\n') ++length;
        if (column > 0 && length <= 32 && length > 32 - column) {
            column = 0;
            ++row;
            if (!--lines) break;
            std::printf("\x1b[%d;5H", row);
        }
        do {
            const unsigned char c = static_cast<unsigned char>(*s++);
            std::putchar(c >= 32 && c < 127 ? c : ' ');
            if (++column == 32) {
                column = 0;
                ++row;
                if (!--lines) return;
                std::printf("\x1b[%d;5H", row);
            }
        } while (*s && *s != ' ' && *s != '\n');
        if (*s == ' ') {
            ++s;
            if (column > 0) {
                std::putchar(' ');
                if (++column == 32) {
                    column = 0;
                    ++row;
                    if (!--lines) return;
                    std::printf("\x1b[%d;5H", row);
                }
            }
        }
    }
}

inline void CaneLauncherTopChrome(const char* subtitle, const char* stage, const char* detail) {
    std::printf("\x1b[36m"
                "\x1b[3;5H+----------------------------------------+"
                "\x1b[4;5H|           THIEVIUS RACCOONUS           |"
                "\x1b[5;5H+----------------------------------------+"
                "\x1b[0m\x1b[33m"
                "\x1b[8;19HC O O P E R   V A U L T"
                "\x1b[10;15HT H I E V I U S   C T R"
                "\x1b[0m");
    CaneLauncherText(13, 14, subtitle, 34);
    CaneLauncherText(16, 7, stage, 38);
    if (detail && detail[0])
        CaneLauncherText(18, 7, detail, 38);
}

inline void CaneLauncherBottomChrome(const char* title) {
    std::printf("\x1b[33m");
    CaneLauncherText(3, 5, title, 32);
    std::printf("\x1b[0m");
    CaneLauncherLine(5, 5, 32);
    CaneLauncherLine(24, 5, 32);
    CaneLauncherText(26, 5, "ThieviusCTR - Sly Cooper 3DS", 32);
}

/* mode: 0 = checking, 1 = extracting from ISO, 2 = vault ready */
inline void CaneLauncherDrawProgress(PrintConsole* top,
                                     PrintConsole* bottom,
                                     int consoleModel,
                                     const char* stage,
                                     const char* detail,
                                     unsigned itemDone,
                                     unsigned itemTotal,
                                     unsigned overallDone,
                                     unsigned overallTotal,
                                     unsigned cutsceneCount,
                                     int mode) {
    static unsigned overall = 0;
    if (mode == 0 && overallDone == 0 && std::strcmp(stage, "Checking game files") == 0)
        overall = 0;
    overall = CaneLauncherTotalPercent(stage, overallDone, overallTotal, overall);
    if (mode == 2) overall = 100;

    consoleSelect(top);
    consoleClear();
    CaneLauncherTopChrome(
        mode == 2 ? "VAULT PREPARED" : (mode == 1 ? "ON-CONSOLE SETUP" : "GAME LAUNCHER"),
        stage,
        detail);
    if (mode == 1 && itemTotal > 0) {
        CaneLauncherBar(20, 9, 32, itemDone, itemTotal);
        std::printf("\x1b[22;23H%3u%%", CaneLauncherPercent(itemDone, itemTotal));
    } else if (mode == 2) {
        CaneLauncherText(20, 14, "All core game files verified.", 34);
    } else {
        CaneLauncherText(20, 14, "Preparing your heist...", 34);
    }
    std::printf("\x1b[24;5HTOTAL PREPARATION %3u%%", overall);
    CaneLauncherBar(26, 5, 40, overall, 100);
    if (cutsceneCount > 0)
        std::printf("\x1b[28;5HCutscenes ready: %2u/18", cutsceneCount);
    else
        std::printf("\x1b[28;5HCutscenes: optional (PC extract_all.py)");

    consoleSelect(bottom);
    consoleClear();
    CaneLauncherBottomChrome(mode == 2 ? "VAULT READY" : (mode == 1 ? "DISC PREPARATION" : "CHECKING VAULT"));
    CaneLauncherText(7, 5, "CONSOLE", 32);
    CaneLauncherText(8, 5, CaneLauncherConsoleName(consoleModel), 32);
    if (mode == 2) {
        CaneLauncherText(12, 5, "Game files are ready.", 32);
        CaneLauncherText(14, 5, "Starting Sly Cooper...", 32);
        if (cutsceneCount == 0)
            CaneLauncherMessage(17, "Movies skipped unless prepared on PC with extract_all.py.", 2);
    } else if (mode == 1) {
        CaneLauncherText(11, 5, "EXTRACTING FROM DISC ISO", 32);
        CaneLauncherText(13, 5, stage, 32);
        CaneLauncherMessage(15, "Files are prepared from your ISO only when missing.", 2);
        CaneLauncherText(19, 5, "Keep the console powered on.", 32);
        CaneLauncherText(21, 5, "B: Cancel preparation", 32);
    } else {
        CaneLauncherText(12, 5, "CHECKING GAME FILES", 32);
        CaneLauncherText(14, 5, stage, 32);
        CaneLauncherMessage(16, "Verified Worlds and audio reuse existing SD files.", 3);
        CaneLauncherText(20, 5, "Please wait...", 32);
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

inline void CaneLauncherDrawPrompt(PrintConsole* top,
                                   PrintConsole* bottom,
                                   int consoleModel,
                                   const char* title,
                                   const char* detail,
                                   const char* message,
                                   const char* actions,
                                   unsigned cutsceneCount) {
    consoleSelect(top);
    consoleClear();
    CaneLauncherTopChrome("PRE-LAUNCH CHECK", title, detail);
    if (cutsceneCount > 0)
        std::printf("\x1b[28;5HCutscenes ready: %2u/18", cutsceneCount);
    else
        std::printf("\x1b[28;5HSD path: /3ds/ThieviusCTR/");

    consoleSelect(bottom);
    consoleClear();
    CaneLauncherBottomChrome(title);
    CaneLauncherText(7, 5, "CONSOLE", 32);
    CaneLauncherText(8, 5, CaneLauncherConsoleName(consoleModel), 32);
    CaneLauncherMessage(11, message, 8);
    CaneLauncherText(21, 5, actions, 32);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}
