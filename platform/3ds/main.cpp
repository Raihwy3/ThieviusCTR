#include <3ds.h>
#include <citro3d.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <string>
#include <sys/stat.h>
#include "bis.h"
#include "scene_3ds.h"
#include <algorithm>
extern "C" { extern const unsigned char cane_shader[]; extern const unsigned char cane_shader_end[]; }
static bool streamCheck() {
    // Synthetic bytes only: exercise the real engine reader, including its bounds checks.
    const unsigned char sample[]={0x50,0x43,0x42,0x52,17,0,0,0,0,0,0x80,0x3f,0x34,0x12,0,0};
    CBinaryInputStream stream; stream.OpenMemory(sizeof sample,sample);
    if(stream.U32Read()!=0x52424350 || stream.U32Read()!=17 || stream.F32Read()!=1.f || stream.U16Read()!=0x1234)return false;
    stream.Align(4);
    if(stream.GetOffset()!=sizeof sample)return false;
    try { stream.U8Read(); return false; } catch(const std::runtime_error&) {}
    stream.Seek(8); return stream.F32Read()==1.f;
}
int main() {
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);
    bool sd=R_SUCCEEDED(fsInit());bool extra=R_SUCCEEDED(irrstInit());
    printf("ThieviusCTR - Paris viewer\n\n");
    bool passed=false;try {passed=streamCheck();}catch(const std::exception& e){printf("Reader: %s\n",e.what());}
    printf("Original reader: %s\n",passed?"PASS":"FAIL");
    if(!C3D_Init(0x100000)){gfxExit();return 1;}
    auto* shader=DVLB_ParseFile((u32*)cane_shader,cane_shader_end-cane_shader);
    shaderProgram_s program{};shaderProgramInit(&program);
    C3D_RenderTarget* target=nullptr;
    CaneScene scene;bool ready=false;
    if(!shader||!shader->numDVLE)goto cleanup;
    shaderProgramSetVsh(&program,&shader->DVLE[0]);C3D_BindProgram(&program);
    target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(!target)goto cleanup;
    C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
    ready=scene.load("sdmc:/3ds/ThieviusCTR/Paris.cscene");
    if(!ready){
        PrintConsole errorConsole;consoleInit(GFX_TOP,&errorConsole);
        printf("\x1b[2JThieviusCTR\n\nParis scene could not be loaded.\n\nExpected on the emulated SD card:\n/3ds/ThieviusCTR/Paris.cscene\n\nOpening the .3dsx from a PC folder does\nnot copy its data onto Azahar's SD card.\n\nOn this PC, run:\npython3 platform/3ds/run_emulator.py\n\nSee the bottom screen for the error.\n\nSTART: exit\n");
        while(aptMainLoop()){
            hidScanInput();if(hidKeysDown()&KEY_START)break;
            gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
        }
        goto cleanup;
    }
    printf("\nStatic scene preview - no gameplay\n\nCircle Pad: move\nC-stick / D-pad: look\nTouch drag: look\nA/B: up/down  R: faster\nX: reset camera  START: exit\n");
    {
        auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,4);AttrInfo_AddLoader(attr,2,GPU_FLOAT,2);
        auto* env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
        C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);C3D_AlphaTest(true,GPU_GREATER,24);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
        int loc=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
        CaneCamera camera=caneResetCamera(scene.header);
        u64 last=osGetTime(),stats=last;unsigned frames=0,drawn=0;touchPosition previous{};bool touching=false;
        while(aptMainLoop()){
            hidScanInput();u32 held=hidKeysHeld(),down=hidKeysDown();if(down&KEY_START)break;
            if(extra)irrstScanInput();
            u64 now=osGetTime();float dt=float(now-last)*.001f;last=now;if(dt>.05f)dt=.05f;
            if(down&KEY_X)camera=caneResetCamera(scene.header);
            circlePosition pad{},stick{};hidCircleRead(&pad);if(extra)irrstCstickRead(&stick);
            camera.yaw+=(float(stick.dx)/156.f+float(bool(held&KEY_DRIGHT))-float(bool(held&KEY_DLEFT)))*dt*1.7f;
            camera.pitch+=(float(stick.dy)/156.f+float(bool(held&KEY_DUP))-float(bool(held&KEY_DDOWN)))*dt*1.5f;
            if(held&KEY_TOUCH){touchPosition point{};hidTouchRead(&point);if(touching){camera.yaw+=float(int(point.px)-int(previous.px))*.006f;camera.pitch-=float(int(point.py)-int(previous.py))*.006f;}previous=point;touching=true;}else touching=false;
            camera.pitch=std::clamp(camera.pitch,-1.5f,1.5f);camera.yaw=std::remainder(camera.yaw,6.2831853f);
            float move=(held&KEY_R?1600.f:450.f)*dt;
            float forward=std::abs(pad.dy)>12?float(pad.dy)/156.f:0,side=std::abs(pad.dx)>12?float(pad.dx)/156.f:0;
            camera.x+=(std::cos(camera.yaw)*forward-std::sin(camera.yaw)*side)*move;
            camera.y+=(std::sin(camera.yaw)*forward+std::cos(camera.yaw)*side)*move;
            camera.z+=(float(bool(held&KEY_A))-float(bool(held&KEY_B)))*move;
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);C3D_RenderTargetClear(target,C3D_CLEAR_ALL,0x12223bff,0);C3D_FrameDrawOn(target);
            if(ready)drawn=scene.draw(camera,loc);
            C3D_FrameEnd(0);++frames;
            if(now-stats>=500){printf("\x1b[20;1HDrawn: %u / %lu          \nFPS: %.1f (emulator/hardware)    \nPos: %.0f %.0f %.0f           ",drawn,(unsigned long)scene.batches.size(),float(frames)*1000.f/float(now-stats),camera.x,camera.y,camera.z);frames=0;stats=now;}
        }
    }
cleanup:
    C3D_FrameSync();scene.release();shaderProgramFree(&program);if(shader)DVLB_Free(shader);C3D_Fini();
    if(extra)irrstExit();
    if(sd)fsExit();
    gfxExit();return ready?0:1;
}
