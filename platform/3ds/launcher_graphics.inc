// CPU-drawn startup panels work before game assets or the 3D renderer exist.
// No terminal grid, ANSI borders or bundled game artwork.
#include <algorithm>
#include <cmath>
#include <string>
namespace cane_launcher_graphics {
constexpr uint32_t White=0xe3e6e9, Grey=0x969fa9, Yellow=0xe2cf64, Blue=0x087faf;
struct Canvas {
    u8* pixels=nullptr;
    int width=0, bytes=0;
    Canvas(gfxScreen_t screen) {
        u16 stride=0,w=0;
        pixels=gfxGetFramebuffer(screen,GFX_LEFT,&stride,&w);
        const auto format=gfxGetScreenFormat(screen);
        if(stride!=240 || (w!=400 && w!=320) ||
           (format!=GSP_RGB565_OES && format!=GSP_BGR8_OES)) {pixels=nullptr;return;}
        width=w;bytes=format==GSP_RGB565_OES?2:3;
    }
    void pixel(int x,int y,uint32_t rgb) {
        if(!pixels || x<0 || x>=width || y<0 || y>=240)return;
        u8* p=pixels+(x*240+239-y)*bytes;
        if(bytes==2) {const u16 c=((rgb>>19)&31)<<11 | ((rgb>>10)&63)<<5 | ((rgb>>3)&31);p[0]=u8(c);p[1]=u8(c>>8);}
        else {p[0]=u8(rgb);p[1]=u8(rgb>>8);p[2]=u8(rgb>>16);}
    }
    void rect(int x,int y,int w,int h,uint32_t color) {
        for(int yy=std::max(0,y);yy<std::min(240,y+h);++yy)
            for(int xx=std::max(0,x);xx<std::min(width,x+w);++xx)pixel(xx,yy,color);
    }
    static const u8* glyph(unsigned char ch) {
        const auto& f=consoleGetDefault()->font;
        if(!f.gfx || ch<f.asciiOffset || ch>=f.asciiOffset+f.numChars)return nullptr;
        return f.gfx+(ch-f.asciiOffset)*8;
    }
    // Match Eyes of Gold's SDK text exactly: one 8x8 glyph per cell.
    // Do not enlarge glyphs, change character advances, or add text shadows.
    static float measure(const char* value,float) {
        return float(std::strlen(value?value:"")*8);
    }
    void lettering(const char* value,float x,float y,float,uint32_t color) {
        for(const unsigned char* p=reinterpret_cast<const unsigned char*>(value?value:"");*p;++p) {
            const u8* g=glyph(*p>=32 && *p<127?*p:' ');
            if(g)for(int gy=0;gy<8;++gy)for(int gx=0;gx<8;++gx)
                if(g[gy]&(0x80u>>gx))pixel(int(x)+gx,int(y)+gy,color);
            x+=8;
        }
    }
    void text(const char* value,int y,float=8,uint32_t color=White,bool center=true,int inset=32) {
        const int columns=std::max(0,(width-inset*2)/8);
        const std::string clipped=std::string(value?value:"").substr(0,size_t(columns));
        const int x=center?(width-int(clipped.size())*8)/2:inset;
        lettering(clipped.c_str(),float(x),float(y),8,color);
    }
    void paragraph(const char* value,int y,float size,int lines,uint32_t color=White) {
        size=8;
        const float available=float(std::min(width-64,32*8));
        std::string line,word;
        auto flush=[&](){if(lines<=0)return; text(line.c_str(),y,size,color,false);line.clear();y+=8;--lines;};
        const char* p=value?value:"";
        while(*p && lines>0) {
            if(*p=='\n'){flush();++p;continue;}
            if(*p==' '){++p;continue;}
            word.clear();while(*p && *p!=' ' && *p!='\n'){unsigned char c=*p++;word+=c>=32 && c<127?char(c):' ';}
            const std::string next=line.empty()?word:line+" "+word;
            if(!line.empty() && measure(next.c_str(),size)>available)flush();
            if(lines<=0)break;
            if(!line.empty())line+=' ';
            line+=word;
        }
        if(!line.empty() && lines>0)flush();
    }
    void panel(const char* title) {
        rect(0,0,width,240,0x02060b);
        text(title,34,15,White);
        // Same restrained header/footer treatment as Eyes of Gold.
        // Sly's blue menu colour, two horizontal strokes, no enclosing box.
        rect(28,65,width-56,1,Blue);
        rect(28,208,width-56,1,Blue);
    }

    void bar(int y,unsigned done,unsigned total) {
        const int x=28,w=width-56;
        rect(x,y,w,4,0x303b46);
        if(total) {
            const int filled=int(uint64_t(std::min(done,total))*(w-2)/total);
            rect(x+1,y+1,filled,2,Yellow);
        } else {
            const int span=std::max(1,(w-2)/5);
            const int offset=int((osGetTime()/35)%unsigned(w-2-span));
            rect(x+1+offset,y+1,span,2,Blue);
        }
    }

};
inline void present() {gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
inline const char* stageName(const char* stage) {return stage && std::strcmp(stage,"Vault ready")==0?"Ready":stage?stage:"";}
}

inline void CaneLauncherDrawProgress(PrintConsole*,PrintConsole*,int consoleModel,
    const char* stage,const char* detail,unsigned itemDone,unsigned itemTotal,
    unsigned overallDone,unsigned overallTotal,unsigned cutsceneCount,int mode) {
    using namespace cane_launcher_graphics;
    static unsigned overall=0;
    if(mode==0 && overallDone==0 && stage && std::strcmp(stage,"Checking game files")==0)overall=0;
    overall=CaneLauncherTotalPercent(stage,overallDone,overallTotal,overall);
    if(mode==2)overall=100;
    Canvas top(GFX_TOP),bottom(GFX_BOTTOM);
    top.panel("ThieviusCTR");
    top.text(stageName(stage),85,14);
    top.paragraph(detail,112,12,2,Grey);
    if(itemTotal>0 && mode==1) {
        char label[64];std::snprintf(label,sizeof label,"Current task: %u%%",CaneLauncherPercent(itemDone,itemTotal));
        top.text(label,143,11,Grey,false);top.bar(158,itemDone,itemTotal);
    } else top.text(mode==2?"Ready to play":"Please wait...",149,13,Yellow);
    char progress[64];std::snprintf(progress,sizeof progress,"Progress: %u%%",overall);
    top.text(progress,178,12,White,false);top.bar(195,overall,100);
    if(cutsceneCount) {char movies[48];std::snprintf(movies,sizeof movies,"Movies ready: %u / 18",cutsceneCount);top.text(movies,212,10,Grey);}
    bottom.panel(mode==1?"Preparing files":mode==2?"Ready":"Loading");
    if(mode==1) {
        bottom.paragraph("Preparing game files from your disc image. This only needs to run once.",86,13,5);
        bottom.text("Keep the console powered on.",171,11,Grey);
        bottom.text("B: Cancel",190,12,Yellow);
    } else if(mode==2) {
        bottom.text("Starting Sly Cooper...",99,15);
        if(!cutsceneCount)bottom.paragraph("Some movies are not installed.",135,12,2,Grey);
    } else {
        bottom.paragraph("Checking your installed game files.",92,15,3);
        bottom.text("Please wait...",156,13,Yellow);
    }
    bottom.text(CaneLauncherConsoleName(consoleModel),212,10,Grey);
    present();
}
inline void CaneLauncherDrawPrompt(PrintConsole*,PrintConsole*,int consoleModel,
    const char* title,const char* detail,const char* message,const char* actions,unsigned) {
    using namespace cane_launcher_graphics;
    Canvas top(GFX_TOP),bottom(GFX_BOTTOM);
    top.panel("ThieviusCTR");top.text(title,85,14);top.paragraph(detail,117,13,5,Grey);
    top.text("SD: /3ds/ThieviusCTR/",212,11,Grey);
    bottom.panel("Game files");bottom.paragraph(message,81,12,7);
    bottom.text(actions,193,12,Yellow);bottom.text(CaneLauncherConsoleName(consoleModel),212,10,Grey);
    present();
}
inline void CaneLauncherDrawUpdate(const char* stage,const char* message,const char* actions,
    unsigned done,unsigned total,const char* installed,const char* available) {
    using namespace cane_launcher_graphics;
    Canvas top(GFX_TOP),bottom(GFX_BOTTOM);
    top.panel("ThieviusCTR");top.text(stage,87,14);top.text("Launcher update",121,13,Grey);
    top.bar(156,done,total);
    if(total) {char pct[32];std::snprintf(pct,sizeof pct,"%u%%",CaneLauncherPercent(done,total));top.text(pct,177,13,Yellow);}
    const std::string versions=std::string(installed?installed:"")+(available && *available?"  ->  "+std::string(available):"");
    top.text(versions.c_str(),212,11,Grey);
    bottom.panel("Update");bottom.paragraph(message,81,12,7);bottom.text(actions,193,12,Yellow);
    present();
}
