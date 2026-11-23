#include "Input.h"
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include "binoc.h"
#include "ui.h"
#include "prompt.h"
#include "po.h"
#include "safe_touch.h"

extern bool CaneDialogueOnBottom3ds();
extern bool CaneSwapBA3ds();
extern bool CaneGyroEnabled3ds();
extern int CaneGyroSensitivity3ds();

namespace {
bool s_started = false;
bool s_extraInput = false;
bool s_gyroStarted=false, s_gyroFailed=false;
float s_gyroCoefficient=0, s_yaw=0, s_pitch=0, s_biasYaw=0, s_biasPitch=0;
float s_filteredYaw=0, s_filteredPitch=0;
u64 s_gyroTime=0,s_calibrateUntil=0;
double s_calibrateYaw=0,s_calibratePitch=0;
unsigned s_calibrateCount=0;
void pollGyro() {
    const u64 now=osGetTime();
    const bool needed=CaneGyroEnabled3ds() || s_calibrateUntil;
    if(needed && !s_gyroStarted && !s_gyroFailed) {
        s_gyroStarted=R_SUCCEEDED(HIDUSER_EnableGyroscope());
        if(!s_gyroStarted || R_FAILED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&s_gyroCoefficient)) || !(s_gyroCoefficient>0)) {
            if(s_gyroStarted)HIDUSER_DisableGyroscope();
            s_gyroStarted=false;s_gyroFailed=true;
        }
    }
    if(!needed && s_gyroStarted) {HIDUSER_DisableGyroscope();s_gyroStarted=false;}
    const float dt=s_gyroTime && now>s_gyroTime && now-s_gyroTime<=100 ? float(now-s_gyroTime)*.001f:0;
    s_gyroTime=now;
    angularRate rate{};if(s_gyroStarted)hidGyroRead(&rate);
    const float yaw=s_gyroStarted?float(rate.z)/s_gyroCoefficient:0;
    const float pitch=s_gyroStarted?-float(rate.x)/s_gyroCoefficient:0;
    if(s_calibrateUntil) {
        if(std::abs(yaw)<3 && std::abs(pitch)<3 && dt>0 && s_gyroStarted) {
            s_calibrateYaw+=yaw;s_calibratePitch+=pitch;++s_calibrateCount;
        }
        if(now>=s_calibrateUntil) {
            if(s_calibrateCount>=8) {s_biasYaw=float(s_calibrateYaw/s_calibrateCount);s_biasPitch=float(s_calibratePitch/s_calibrateCount);}
            s_calibrateUntil=0;
        }
    }
    const bool active=s_gyroStarted && CaneGyroEnabled3ds() && !s_calibrateUntil &&
        (g_ui.uis==UIS_Playing || g_ui.uis==UIS_Hub) && g_binoc.fActive &&
        (g_binoc.binocs==BINOCS_Peek || g_binoc.binocs==BINOCS_Sniper) && (g_grfjoyt&2);
    if(!active || dt==0) {
        s_yaw=s_pitch=s_filteredYaw=s_filteredPitch=0;
        if(s_gyroStarted && !active && !s_calibrateUntil && std::abs(yaw)<1 && std::abs(pitch)<1) {
            const float a=dt/(2+dt);s_biasYaw+=a*(yaw-s_biasYaw);s_biasPitch+=a*(pitch-s_biasPitch);
        }
        return;
    }
    float y=yaw-s_biasYaw,p=pitch-s_biasPitch;
    if(std::abs(y)<.15f)y=0;if(std::abs(p)<.15f)p=0;
    const float a=dt/(.012f+dt);
    s_filteredYaw+=a*(y-s_filteredYaw);s_filteredPitch+=a*(p-s_filteredPitch);
    const float radians=.01745329252f*(CaneGyroSensitivity3ds()*.01f)*dt;
    s_yaw=std::clamp(s_yaw+std::clamp(s_filteredYaw,-600.f,600.f)*radians,-.15f,.15f);
    s_pitch=std::clamp(s_pitch+std::clamp(s_filteredPitch,-600.f,600.f)*radians,-.15f,.15f);
}
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
    if(s_gyroStarted)HIDUSER_DisableGyroscope();
    s_gyroStarted=s_gyroFailed=false;s_gyroTime=s_calibrateUntil=0;s_yaw=s_pitch=0;
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
    pollGyro();
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
    // A floating touch stick preserves camera motion while the finger is held.
    static bool touching=false;
    static int originX=0,originY=0;
    static CaneSafeTouch safeTouch;
    const bool contact=(held&KEY_TOUCH)!=0;
    const PO* controlled=PpoCur();
    const bool safeActive=controlled && controlled->pvtlo && controlled->pvtlo->cid==CID_VAULT &&
        g_prompt.blots==BLOTS_Hidden &&
        (g_ui.uis==UIS_Playing || g_ui.uis==UIS_Hub);
    touchPosition point{};
    if(contact)hidTouchRead(&point);
    int safeX=0,safeY=0;
    safeTouch.Update(safeActive,contact,point.px,point.py,safeX,safeY);
    if(contact) {
        if(!touching) {originX=point.px;originY=point.py;}
        if(g_prompt.blots!=BLOTS_Hidden) {
            // Menus remain on the top screen and use physical controls.
        } else if(safeActive) {
            // Vault instructions can keep the dialogue HUD visible. Route
            // touch to the combination before the generic dialogue tap.
            if(safeX || safeY) {
                joy->x=float(safeX);joy->y=float(safeY);joy->uDeflect=1.f;
            }
        } else if(CaneDialogueOnBottom3ds()) {
            if(!touching)joy->current[BTN_CROSS]=true;
        } else if(g_ui.uis==UIS_Attract) {
            if(!touching && point.py>=156 && point.py<206)joy->current[point.px<160?BTN_START:BTN_SELECT]=true;
        } else if(g_ui.uis==UIS_Playing || g_ui.uis==UIS_Hub) {
            const float dx=float(int(point.px)-originX),dy=float(originY-int(point.py));
            const float distance=std::hypot(dx,dy);
            const float strength=std::clamp((distance-3.f)/45.f,0.f,1.f);
            if(distance>0) {
                joy->x2=std::clamp(joy->x2+dx/distance*strength,-1.f,1.f);
                joy->y2=std::clamp(joy->y2+dy/distance*strength,-1.f,1.f);
                joy->uDeflect2=std::min(1.f,std::hypot(joy->x2,joy->y2));
            }
        }
    }
    touching=contact;
    joy->joystickId = 0;
    joy->gamepadConnected = true;
    joy->hasGamepadState = false;
}

void CaneConsumeGyro3ds(float* yaw,float* pitch) {*yaw=s_yaw;*pitch=s_pitch;s_yaw=s_pitch=0;}
void CaneCalibrateGyro3ds() {
    s_calibrateYaw=s_calibratePitch=0;s_calibrateCount=0;s_calibrateUntil=osGetTime()+1500;
    s_yaw=s_pitch=s_filteredYaw=s_filteredPitch=0;s_gyroFailed=false;
}
bool CaneGyroCalibrating3ds() {return s_calibrateUntil!=0;}
