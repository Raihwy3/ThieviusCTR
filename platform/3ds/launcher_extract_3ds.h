#pragma once

#include <3ds.h>
#include <cstdint>
#include <cstdio>

struct CaneLauncherLevelRef {
    uint32_t levelId;
    const char* name;
};

/* Validates or extracts required game assets before starting the runtime engine.
 * Returns true if the game should proceed to launch, false to exit cleanly. */
bool CaneLauncherEnsureAssets3ds(const char* baseDir,
                                 const CaneLauncherLevelRef* levels,
                                 unsigned levelCount,
                                 PrintConsole* top,
                                 PrintConsole* bottom,
                                 FILE* logFile);

/* Restores top LCD format/buffering after libctru text-mode launcher screens
 * so Citro3D can attach its BGR8/RGBA8 render target cleanly. */
void CaneLauncherRestoreTopDisplay3ds(PrintConsole* bottom);
