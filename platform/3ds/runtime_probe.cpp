#include <3ds.h>
#include "brx.h"
#include "frm.h"
#include "game.h"
#include "transition.h"
#include "update.h"
#include "Input.h"
#include "jt.h"
#include "screen.h"
#include "jump.h"
#include "pipe.h"
#include "hide.h"
#include "target.h"
#include "render.h"
#include "save.h"
#include "logo.h"
#include "attract.h"
#include "aseg.h"
#include "asega.h"
#include "prompt.h"
#include "xform.h"
#include "bbmark.h"
#include <unwind.h>
#include "../platform.h"
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <malloc.h>
#include <sys/stat.h>
#include "launcher_extract_3ds.h"
#include "launcher_ui_3ds.h"
#include "perf_monitor_3ds.h"
#include "update_3ds.h"
#include "native_options.inc"
#include "runtime_graphics.inc"
extern "C" {
u32 __stacksize__=256*1024;
u32 __ctru_linear_heap_size=30*1024*1024;
}
namespace {
FILE* logFile=nullptr;
bool normalShutdown=false;
bool monitorActive=false;
void stage(const char* text) {
    if(!monitorActive)printf("%s\n",text);
    if(logFile) {fprintf(logFile,"%s\n",text);fflush(logFile);}
}
}
namespace {
_Unwind_Reason_Code traceFrame(_Unwind_Context* context,void* count) {
    unsigned* n=static_cast<unsigned*>(count);
    if((*n)++>=24)return _URC_END_OF_STACK;
    if(logFile) fprintf(logFile,"pc=%08lx\n",(unsigned long)_Unwind_GetIP(context));
    return _URC_NO_REASON;
}
void unexpectedExit(const char* operation,int code,void* caller) {
    if(normalShutdown)return;
    char message[128];snprintf(message,sizeof message,"%s code=%d caller=%p",operation,code,caller);
    stage(message);unsigned n=0;_Unwind_Backtrace(traceFrame,&n);if(logFile)fflush(logFile);
}
}
extern "C" {
[[noreturn]] void __real_abort();
[[noreturn]] void __real_exit(int);
[[noreturn]] void __real__exit(int);
[[noreturn]] void __wrap_abort() {
    unexpectedExit("abort",1,__builtin_return_address(0));
    __real__exit(1);
}
[[noreturn]] void __wrap_exit(int code) {
    unexpectedExit("exit",code,__builtin_return_address(0));
    __real_exit(code);
}
[[noreturn]] void __wrap__exit(int code) {
    unexpectedExit("_exit",code,__builtin_return_address(0));
    __real__exit(code);
}
}
extern bool CaneHasPendingMovie3ds();
extern int CaneTakeMovie3ds();
extern bool CaneStartAudio3ds();
extern void CaneStopAudio3ds();
extern void CaneAudioStats(unsigned*,unsigned*);
extern void CaneMusicStats3ds(unsigned*,unsigned*,unsigned*,unsigned*);
int main() {
    osSetSpeedupEnable(true);
    gfxInitDefault();
    static PrintConsole topConsole, bottomConsole;
    consoleInit(GFX_TOP, &topConsole);
    consoleInit(GFX_BOTTOM, &bottomConsole);
    consoleDebugInit(debugDevice_NULL);
    const bool sd=R_SUCCEEDED(fsInit());
    if(sd) {
        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/ThieviusCTR", 0777);
    }
    logFile=fopen("sdmc:/3ds/ThieviusCTR/runtime.log","w");
    freopen("sdmc:/3ds/ThieviusCTR/runtime-stderr.log","w",stderr);
    setvbuf(stderr,nullptr,_IONBF,0);
    {
        CaneLauncherLevelRef levelRefs[46]{};
        for(unsigned i=0;i<46;++i) {
            levelRefs[i].levelId=uint32_t(g_levelTable[i].levelID);
            levelRefs[i].name=g_levelTable[i].levelName.c_str();
        }
        bool launcherReady=sd;
#ifdef CANE_CIA
        constexpr bool installedTitle=true;
#else
        constexpr bool installedTitle=false;
#endif
        if(launcherReady)launcherReady=CaneUpdateStartup3ds("sdmc:/3ds/ThieviusCTR", &topConsole, &bottomConsole, logFile, installedTitle);
        if(!launcherReady || !CaneLauncherEnsureAssets3ds("sdmc:/3ds/ThieviusCTR", levelRefs, 46, &topConsole, &bottomConsole, logFile)) {
            stage("Launcher exited");
            normalShutdown=true;
            if(logFile) {fclose(logFile);logFile=nullptr;}
            if(sd)fsExit();
            gfxExit();
            return 0;
        }
        CaneLauncherRestoreTopDisplay3ds(&bottomConsole);
    }
    CanePerfMonitor3ds perfMonitor;
    try {
        stage("Original runtime: startup");
        printf("Heap: %lu MiB\n",(unsigned long)(envGetHeapSize()/1024/1024));
        cane::g_dataOverride="sdmc:/3ds/ThieviusCTR";
        cane::g_userOverride="sdmc:/3ds/ThieviusCTR";
        CaneLoadNativeOptions3ds();
        stage("Controller");StartupControllerInput();
        stage("DSP audio");CaneStartAudio3ds();
        // Replace the desktop window dimensions before NewCm recalculates
        // its projection. Never create the desktop OpenGL context.
        g_gl.width=g_gl.renderWidth=g_gl.outputWidth=g_gl.presentWidth=400;
        g_gl.height=g_gl.renderHeight=g_gl.outputHeight=g_gl.presentHeight=240;
        g_gl.aspectRatio=400.0f/240.0f;g_gl.aspectMode=FitToScreen;
        g_gl.uiScale=.5f;g_gl.uiOrigin=glm::vec2(40,0);
        stage("Clock / property tables");StartupClock();StartupBrx();
        stage("Original screen / UI state");StartupScreen();StartupUi();
        stage("Frame / targets / jumps");StartupFrame();StartupTarget();StartupJmt();StartupPipe();StartupHide();
        stage("Original save data / game startup");StartupSaveData(&g_saveData);StartupGame();StartupCodes();

        stage("Start native scene / character renderer");
        CaneRuntimeGraphics graphics;graphics.init();
        perfMonitor.init(&bottomConsole, CaneLauncherDetectConsoleModel());
        graphics.attachBottomConsole(&bottomConsole);
        perfMonitor.setTargetFps(CaneRenderCap3ds());
        u64 lastPresentedTick=0;
        bool startupTitleReached=false;caneStartupPresentation=true;
        bool skipStartupAnimation=CaneSkipIntro3ds();
        bool skippingTitle=false;float titleMasterVolume=1;
        monitorActive=true;
        std::string file="sdmc:/3ds/ThieviusCTR/Worlds/Splash/Splash.brx";
        extern void CanePrefetchLevel3ds(LEVELINFO*,const std::string&);
        LEVELINFO* level=nullptr;
        bool firstLevelFrame=false;
        u64 last=osGetTime(),report=last;u64 updateTicks=0,simTicks=0;unsigned frames=0;bool missionComplete=false;bool announcedAttract=false;u32 previousHeld=0;int lastState=-1;
        auto playPendingMovies=[&]() {
            bool played=false;
            if(CaneHasPendingMovie3ds() && g_wipe.wipes!=WIPES_WipingOut) {
                perfMonitor.setStatus("Playing cutscene...");
            }
            if(CaneHasPendingMovie3ds() && g_wipe.wipes!=WIPES_WipingOut)graphics.applyGameplayDisplay(0);
            while(CaneHasPendingMovie3ds() && g_wipe.wipes!=WIPES_WipingOut) {
                int movie=CaneTakeMovie3ds();
                if(movie<0)break;
                if(movie==17 && !startupTitleReached && skipStartupAnimation) {
                    stage("Skipping previously viewed startup video");continue;
                }
                if(movie==17)stage("Playing original startup video");
                else if(movie==0)stage("Playing original opening story");
                else {
                    char msg[64];
                    snprintf(msg,sizeof msg,"Playing cutscene %02d",movie);
                    stage(msg);
                }
                if(!graphics.movie(movie))stage("Requested movie is not prepared in this test");
                g_joy.Update(nullptr);
                g_joy.SetHandled(BTN_START);
                g_joy.SetHandled(BTN_CIRCLE);
                g_joy.SetHandled(BTN_CROSS);
                hidScanInput();
                previousHeld=hidKeysHeld();
                ResyncClockTick(&g_clock);
                last=osGetTime();
                played=true;
            }
            if(played) {
                perfMonitor.resetWindow();
                simTicks=0;updateTicks=0;
                perfMonitor.updateIfDue(
                    level?level->levelName.c_str():"Splash",
                    level?unsigned(level->levelID):2u,
                    graphics.drawCallCount,graphics.triangleCount,graphics.packetCount,
                    CaneSwapBA3ds(),true);
            }
            return played;
        };
        while(aptMainLoop() && !fQuitGame) {
            hidScanInput();
            const u32 held=hidKeysHeld(),pressed=held&~previousHeld;previousHeld=held;
            (void)pressed;
            if((held&(KEY_SELECT|KEY_START))==(KEY_SELECT|KEY_START))break;
            if(playPendingMovies())continue;
            if(g_transition.m_fPending && !missionComplete) {
                auto* pending=g_transition.m_plevelPending?g_transition.m_plevelPending:g_transition.m_plevelCurrent;
                if(pending) {
                    if(pending->levelID==2)stage("Loading original Splash world");
                    else if(pending->levelID==3)stage("Loading original Splash / Paris transition");
                    else {
                        char msg[160];
                        snprintf(msg,sizeof msg,"Loading level 0x%03X: %s",pending->levelID,pending->levelName.c_str());
                        stage(msg);
                    }
                    perfMonitor.setStatus(pending->levelName.c_str());
                    graphics.showLoading();
                    const u64 loadStart=osGetTime();
                    KillSoundSystem();graphics.resetWorld();
                    const u64 loadReset=osGetTime();
                    g_transition.Execute(file);
                    const u64 loadWorld=osGetTime();
                    if(!g_psw)throw std::runtime_error("Transition did not create a world");
                    if(g_transition.m_plevelCurrent!=pending) {
                        missionComplete=true;
                        char msg[160];
                        snprintf(msg,sizeof msg,"Level 0x%03X (%s) is unavailable on SD. Select + START exits.",pending->levelID,pending->levelName.c_str());
                        stage(msg);
                        ResyncClockTick(&g_clock);last=osGetTime();
                        perfMonitor.resetWindow();
                        continue;
                    }
                    if(!graphics.prepareTextureCache(file,[&](unsigned done,unsigned total) {
                        char msg[64];snprintf(msg,sizeof msg,"Preparing textures %u/%u",done,total);perfMonitor.setStatus(msg);
                    }))break;
                    std::fprintf(stderr,"Level load ms: reset=%llu world=%llu cache=%llu total=%llu level=0x%03X\n",
                        (unsigned long long)(loadReset-loadStart),(unsigned long long)(loadWorld-loadReset),
                        (unsigned long long)(osGetTime()-loadWorld),(unsigned long long)(osGetTime()-loadStart),unsigned(pending->levelID));
                    level=pending;g_psw->ambOwners.reserve(128);lastState=g_pjt?int(g_pjt->jts):-1;
                    firstLevelFrame=true;
                    ResyncClockTick(&g_clock);last=osGetTime();
                    perfMonitor.resetWindow();simTicks=0;updateTicks=0;
                    perfMonitor.updateIfDue(
                        level->levelName.c_str(),unsigned(level->levelID),
                        graphics.drawCallCount,graphics.triangleCount,graphics.packetCount,
                        CaneSwapBA3ds(),true);
                    playPendingMovies();
                    continue;
                }
                missionComplete=true;stage("No level target specified. Select + START exits.");
            }
            if(!startupTitleReached && level && level->levelID==3 && (pressed&KEY_START))skipStartupAnimation=true;
            const bool fastTitle=skipStartupAnimation && !startupTitleReached && level && level->levelID==3;
            if(fastTitle && !skippingTitle) {
                graphics.showLoading("Preparing title...");
                titleMasterVolume=GetMasterVolume();SetMasterVolume(0);skippingTitle=true;
                stage("Advancing startup script without rendering the intro");
            }
            const u64 wallNow=osGetTime();
            const u64 now=fastTitle?last+33*64:wallNow;
            if(!fastTitle && now-last>100)last=now-100;
            const u64 simT0=svcGetSystemTick();
            for(unsigned step=0;!missionComplete && g_psw && now-last>=33 && step<(fastTitle?64u:3u);++step) {
                last+=33;
                g_joy.Update(nullptr);
                UpdateUi(&g_ui);
                UpdateGameState(g_clock.dt);
                if(g_pcm)SetupCm(g_pcm);
                OpenFrame();
                constexpr float dt=1.0f/30.0f;
                g_clock.dtPrev=g_clock.dt;g_clock.dtReal=dt;
                g_clock.dt=g_clock.fEnabled?dt*g_rtClock*g_rtClockPowerUp*g_rtClockDebug:0;
                g_clock.t+=g_clock.dt;g_clock.tReal+=dt;g_clock.tickFrame=TickNow();
                UpdateCodes();
                u64 ut0=svcGetSystemTick();
                // Use the game's authored START-button transition so the title
                // camera, player placement and script state remain consistent.
                if(!startupTitleReached && level && level->levelID==3 &&
                   (skipStartupAnimation || (pressed&KEY_START))) {
                    skipStartupAnimation=true;
                    if(g_wipe.wipes==WIPES_Idle && g_wipe.transButton.fSet) {
                        JOY skipJoy{};skipJoy.current[BTN_START]=true;
                        if(FCatchWipeButtonTrans(&g_wipe,&skipJoy,WIPES_WipingOut))
                            stage("Skipped startup animation through original title transition");
                    }
                }
                UpdateSw(g_psw,g_clock.dt);
                updateTicks+=svcGetSystemTick()-ut0;
                if(g_ui.uis==UIS_Attract) {
                    if(!announcedAttract) {
                        announcedAttract=true;
                        if(!startupTitleReached) {startupTitleReached=true;caneStartupPresentation=false;CaneMarkIntroSeen3ds();}
                        stage("Original title menu: START plays; SELECT opens menu");
                        stage("Original gameplay with native graphics / DSP audio");
                        stage(CaneSwapBA3ds()
                            ? "Circle Pad: move; A: jump; Select+START: exit"
                            : "Circle Pad: move; B: jump; Select+START: exit");
                    }
                } else {
                    announcedAttract=false;
                }
                ++g_cframe;++frames;
                if(g_pjt) {
                    const auto p=g_pjt->xf.posWorld;
                    if(int(g_pjt->jts)!=lastState) {
                        if(logFile) {fprintf(logFile,"state=%d->%d update=%u pos=%.3f,%.3f,%.3f\n",lastState,int(g_pjt->jts),frames,p.x,p.y,p.z);fflush(logFile);}
                        lastState=int(g_pjt->jts);
                    }
                    if(g_pjt->jts==JTS_Jump && frames%3==0 && logFile)
                        fprintf(logFile,"jump update=%u pos=%.3f,%.3f,%.3f\n",frames,p.x,p.y,p.z);
                    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
                        throw std::runtime_error("Non-finite player position");
                }
                if((fastTitle && startupTitleReached) || g_transition.m_fPending || (CaneHasPendingMovie3ds() && g_wipe.wipes!=WIPES_WipingOut))break;
            }
            if(fastTitle)last=osGetTime();
            if(skippingTitle && startupTitleReached) {
                SetMasterVolume(titleMasterVolume);skippingTitle=false;ResyncClockTick(&g_clock);
                stage("Startup script ready at original title menu");
            }
            simTicks+=svcGetSystemTick()-simT0;
            if(now-report>=1000) {
                const auto p=g_pjt?g_pjt->xf.posWorld:glm::vec3(0);
                if(logFile) {
                    fprintf(logFile,"updates=%u pos=%.3f,%.3f,%.3f\n",frames,p.x,p.y,p.z);
                    fprintf(logFile,"ui=%d clock=%.3f input=%.2f,%.2f flags=%d usr=%d hands=%d jts=%d player=%d level=0x%03X\n",int(g_ui.uis),g_clock.dt,g_joy.x,g_joy.y,g_grfjoyt,g_grfusr,g_psw?g_psw->cHandsOff:-1,(g_pjt?int(g_pjt->jts):-1),PpoCur()?int(PpoCur()->oid):-1,g_transition.m_plevelCurrent?unsigned(g_transition.m_plevelCurrent->levelID):0u);
                    fprintf(logFile,"render packets=%u triangles=%u submit_ms=%.1f binoc=%d dialog=%d flags=%u\n",graphics.packetCount,graphics.triangleCount,graphics.cpuMs,int(g_binoc.binocs),g_binoc.pdialogPlaying?int(g_binoc.pdialogPlaying->dialogs):-1,g_binoc.pdialogPlaying?unsigned(g_binoc.pdialogPlaying->dp.grfdp):0);
                    fprintf(logFile,"dialog step=%d zoom=%.4f/%.4f lip=%d tv=%d,%d text=%.60s\n",g_binoc.pdialogPlaying?g_binoc.pdialogPlaying->ideCur:-1,g_pcm?g_pcm->cplook.uZoom:0.0f,g_binoc.uZoom,g_binoc.pdialogPlaying && g_binoc.pdialogPlaying->dp.pasegaLipsync!=nullptr,int(g_tvLeft.tvgs),int(g_tvRight.tvgs),g_binoc.achzDraw);
                    unsigned played=0,failed=0;CaneAudioStats(&played,&failed);
                    unsigned notes=0,active=0,bytes=0,drops=0;CaneMusicStats3ds(&notes,&active,&bytes,&drops);
                    fprintf(logFile,"music notes=%u active=%u sample_bytes=%u dropped=%u\n",notes,active,bytes,drops);
                    fprintf(logFile,"audio voices=%u failures=%u frame_ms=%.1f gpu_ms=%.2f dsp_frames=%lu hud_vertices=%u\n",played,failed,graphics.frameMs,graphics.gpuMs,(unsigned long)ndspGetFrameCount(),graphics.hudVertexCount);
                    fflush(logFile);
                }
                report=now;
            }
            if(fQuitGame)break;
            if(fastTitle && !startupTitleReached)continue;
            if(!fastTitle && g_wipe.wipes==WIPES_WipingOut && g_wipe.trans.fSet)
                CanePrefetchLevel3ds(g_wipe.trans.plevel,file);
            graphics.updateMs=float(double(updateTicks)/268111.856);
            updateTicks=0;
            if(!missionComplete && g_psw && g_pcm) {
                const u64 firstDrawStart=firstLevelFrame?osGetTime():0;
                graphics.draw();
                if(firstLevelFrame) {
                    std::fprintf(stderr,"Level first draw ms: %llu level=0x%03X (includes visible texture/mesh preparation)\n",
                        (unsigned long long)(osGetTime()-firstDrawStart),level?unsigned(level->levelID):0u);
                    firstLevelFrame=false;
                }
                perfMonitor.setTargetFps(CaneRenderCap3ds());
                // Citro3D's frame counter counts submitted/completed frames,
                // not VBlanks. Waiting for it here prevents the next frame
                // from ever being submitted. Pace against the system clock.
                const u64 interval=268111856ull/u64(CaneRenderCap3ds());
                const u64 tick=svcGetSystemTick();
                if(lastPresentedTick && tick-lastPresentedTick<interval) {
                    const u64 remaining=interval-(tick-lastPresentedTick);
                    svcSleepThread(s64(remaining*1000000000ull/268111856ull));
                }
                lastPresentedTick=svcGetSystemTick();
                perfMonitor.recordFrame(double(simTicks)/268111.856,graphics.cpuMs,graphics.drawCmdMs,graphics.gpuMs);
                simTicks=0;
            } else {
                simTicks=0;
            }
            perfMonitor.updateIfDue(
                level?level->levelName.c_str():"Splash",
                level?unsigned(level->levelID):2u,
                graphics.drawCallCount,graphics.triangleCount,graphics.packetCount,
                CaneSwapBA3ds());
        }
        stage("Native runtime probe exited");
        // Stop DSP and sensor users while the GPU/services are still alive.
        // graphics is destroyed when this scope ends, after audio has stopped.
        stage("Shutdown: audio");
        CaneStopAudio3ds();
        stage("Shutdown: input");
        ShutdownControllerInput();
        stage("Shutdown: graphics");
    } catch(const std::exception& e) {
        perfMonitor.restoreStdout();
        monitorActive=false;consoleClear();
        stage("Native runtime probe stopped");stage(e.what());
        printf("\nSTART: exit\n");
        while(aptMainLoop()) {hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    }
    // These are idempotent and also cover startup exceptions.
    CaneStopAudio3ds();ShutdownControllerInput();
    stage("Shutdown: services");
    normalShutdown=true;
    if(logFile) {fclose(logFile);logFile=nullptr;}
    perfMonitor.restoreStdout();
    if(sd)fsExit();gfxExit();
    // The desktop engine has global owners whose destructors access its world,
    // audio and renderer. They must not run after platform services are gone.
    // newlib _exit still performs the libctru/Homebrew Launcher exit path.
    __real__exit(0);
}
