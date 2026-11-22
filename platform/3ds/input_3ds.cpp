#include "Input.h"
#include <3ds.h>
#include <algorithm>

extern bool CaneSwapBA3ds();

namespace {
bool s_started = false;
bool s_extraInput = false;
}

bool CaneStartInput3ds()
{
    if (!s_started) {
        s_extraInput = R_SUCCEEDED(irrstInit());
        s_started = true;
    }
    // Circle Pad and buttons remain available if IRRST is unsupported.
    return true;
}

void CaneStopInput3ds()
{
    if (s_started && s_extraInput) irrstExit();
    s_started = false;
    s_extraInput = false;
}

// Physical polling only. JOY retains the game's press/release handling,
// controller response, cheat-code processing and camera-relative movement.
void CanePollJoy3ds(JOY* joy)
{
    hidScanInput();
    if (s_extraInput) irrstScanInput();
    u32 held = hidKeysHeld();
    static bool s_lastSwap = true;
    static bool s_swapSynced = false;
    const bool swapBA = CaneSwapBA3ds();
    const u32 circleKey = swapBA ? KEY_B : KEY_A;
    const u32 crossKey  = swapBA ? KEY_A : KEY_B;
    const u32 bindings[BTN_MAX] = {
        KEY_DUP, KEY_DDOWN, KEY_DLEFT, KEY_DRIGHT,
        KEY_Y, circleKey, crossKey, KEY_X, KEY_START, KEY_SELECT,
        KEY_L, KEY_R, KEY_ZL, KEY_ZR, 0, 0
    };
    for (int i = 0; i < BTN_MAX; ++i)
        joy->current[static_cast<JOY_BUTTON>(i)] = (held & bindings[i]) != 0;
    if (s_swapSynced && swapBA != s_lastSwap) {
        // Carry held A/B state into previous so toggling Swap B/A via a face
        // button cannot synthesize a second press on the very next frame.
        joy->previous[BTN_CIRCLE] = joy->current[BTN_CIRCLE];
        joy->previous[BTN_CROSS]  = joy->current[BTN_CROSS];
    }
    s_lastSwap = swapBA;
    s_swapSynced = true;

    circlePosition pad{}, camera{};
    hidCircleRead(&pad);
    if (s_extraInput) irrstCstickRead(&camera);
    const auto normalized = [](s16 value) {
        return std::clamp(float(value) / 156.0f, -1.0f, 1.0f);
    };
    // Both the 3DS and STEP/JT use positive Y for forward/up.
    ApplyStickDeadZone(normalized(pad.dx), normalized(pad.dy),
                      &joy->calibration, &joy->x, &joy->y, &joy->uDeflect);
    ApplyStickDeadZone(normalized(camera.dx), normalized(camera.dy),
                      &joy->calibration2, &joy->x2, &joy->y2, &joy->uDeflect2);
    joy->joystickId = 0;
    joy->gamepadConnected = true;
    joy->hasGamepadState = false;
}
