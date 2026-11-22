#pragma once

#include <3ds.h>
#include <cstdio>

/* Runs startup update recovery and optional GitHub Release self-update before extraction/game startup.
 * Returns false only when the user chooses to exit without starting the game. */
bool CaneUpdateStartup3ds(const char* directory,
                          PrintConsole* top,
                          PrintConsole* bottom,
                          std::FILE* logFile);
