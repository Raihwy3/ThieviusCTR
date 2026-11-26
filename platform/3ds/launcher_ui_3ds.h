#pragma once

#include <3ds.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* Graphical pre-asset launcher; shares the retail menu palette and framing. */

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


#include "launcher_graphics.inc"
