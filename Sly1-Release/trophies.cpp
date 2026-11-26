#include "trophies.h"
#include "../platform/platform.h"
#include "game.h"
#include "gui_layout.h"
#include "prompt.h"
#include "screen.h"
#include "sw.h"
#include "trophy_record.inc"
#include "trophy_ui_state.inc"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#ifdef __3DS__
namespace cane_save3ds
{
bool Replace(const std::string &, const std::string &);
}
#endif
#ifdef __3DS__
extern bool CaneTrophiesGameplay3ds();
extern bool CaneTrophyPopups3ds();
extern void CanePlayTrophyChime3ds();
extern void CaneQuadNative(float, float, float, float, GLuint, const glm::vec4 &);
#endif
namespace trophies
{
namespace
{
uint64_t unlocked = 0, pending = 0, resetBlocked = 0;
bool loaded = false, dirty = false;
float retry = 0, toastTime = 0;
int toast = -1;
std::string File()
{
    return (cane::UserDirectory() / "trophies.dat").string();
}
// Text format, version and complement reject truncated or damaged records.
bool Read(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    uint64_t bits = 0;
    const bool valid = record::Read(f, bits);
    std::fclose(f);
    if (valid)
        unlocked = bits;
    return valid;
}
void Load()
{
    if (loaded)
        return;
    loaded = true;
    if (!Read(File()))
        Read(File() + ".bak");
}
bool Save()
{
    const std::string path = File(), tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    bool ok = record::Write(f, unlocked);
    if (std::fclose(f) != 0)
        ok = false;
    if (!ok)
        return false;
#ifdef __3DS__
    return cane_save3ds::Replace(tmp, path);
#else
    return cane::AtomicReplace(tmp, path);
#endif
}
void Earn(uint64_t bits)
{
    Load();
    uint64_t next = Merge(unlocked, bits & ~resetBlocked);
    pending |= next & ~unlocked;
    if (next != unlocked)
    {
        unlocked = next;
        dirty = true;
    }
}
bool Playing()
{
#ifdef __3DS__
    if (!CaneTrophiesGameplay3ds())
        return false;
#endif
    return ui::Playing(g_ui.uis, g_ui.uisPlaying);
}
const char *VisibleDescription(int index)
{
    // Story destinations, boss outcomes, and every gold award stay concealed.
    constexpr uint64_t spoilers = (uint64_t(1)<<5) | (uint64_t(1)<<6) | (uint64_t(1)<<7) |
        (uint64_t(1)<<18) | (uint64_t(1)<<21) | (uint64_t(1)<<22) | (uint64_t(1)<<23) |
        (uint64_t(1)<<24) | (uint64_t(1)<<26) | (uint64_t(1)<<27) | (uint64_t(1)<<29) | (uint64_t(1)<<31);
    return definitions[index].tier == Gold || (spoilers & (uint64_t(1)<<index))
        ? "???" : definitions[index].description;
}
const char *TierName(Tier tier)
{
    return tier == Gold ? "Gold" : tier == Silver ? "Silver" : tier == Platinum ? "Platinum" : "Bronze";
}
} // namespace
bool Unlocked(int i)
{
    Load();
    return i >= 0 && i < Count && (unlocked & (uint64_t(1) << i));
}
int UnlockedCount()
{
    Load();
    int n = 0;
    for (int i = 0; i < Count; ++i)
        n += Unlocked(i);
    return n;
}
bool Reset()
{
    Load();
    const uint64_t old = unlocked;
    unlocked = 0;
    if (!Save()) { unlocked = old; return false; }
    pending = 0; toast = -1; toastTime = 0; dirty = false;
    resetBlocked = All;
    return true;
}
const char *MenuTitle() { return "Trophies"; }
const char *Row(int i)
{
    static char labels[Count][128];
    if (i < 0 || i >= Count)
        return "";
    std::snprintf(labels[i], sizeof labels[i], "%s [%c] %s", Unlocked(i) ? "*" : "-", TierName(definitions[i].tier)[0],
                  definitions[i].title);
    return labels[i];
}
const char *Detail(int i)
{
    static char text[512];
    if (i < 0 || i >= Count)
        return "";
    std::string description = VisibleDescription(i);
    // Retail text drawing honours explicit line breaks, not textbox wrapping.
    for (size_t line = 0; description.size() - line > 34;)
    {
        size_t split = description.rfind(' ', line + 34);
        if (split == std::string::npos || split < line)
            break;
        description[split] = '\n';
        line = split + 1;
    }
    std::snprintf(text, sizeof text, "%s\n%s - %s\n\n%s", definitions[i].title, TierName(definitions[i].tier),
                  Unlocked(i) ? "Earned" : "Locked", description.c_str());
    return text;
}
void Coin(int coins)
{
    if (!Playing())
        return;
    Snapshot s;
    s.playing = true;
    s.coins = coins;
    Earn(Evaluate(s));
}
void Update(float dt)
{
    Load();
    if (g_pgsCur && g_psw && Playing())
    {
        Snapshot s;
        s.playing = true;
        s.vault = uint32_t(g_pgsCur->grfvault);
        s.coins = g_pgsCur->ccoin;
        for (int w = 0; w < 6; ++w)
        {
            if (g_pgsCur->aws[w].fws & FWS_Complete)
                s.completeWorlds |= 1u << w;
            if (g_pgsCur->aws[w].fws & FWS_Visited)
                s.visitedWorlds |= 1u << w;
        }
        s.allBottles = g_plsCur && g_psw->cclueAll > 0 && g_plsCur->abitClue.m_cbitSet >= g_psw->cclueAll;
        const uint64_t conditions = Evaluate(s);
        resetBlocked &= conditions;
        Earn(conditions);
    }
    dt = std::clamp(dt, 0.0f, 0.1f);
    retry -= dt;
    if (dirty && retry <= 0)
    {
        dirty = !Save();
        retry = 5;
    }
#ifdef __3DS__
    if(!CaneTrophyPopups3ds()) {
        pending=0;toast=-1;toastTime=0;
        return; // Tracking and saving above remain active.
    }
#endif
    if (toast >= 0)
    {
        toastTime -= dt;
        if (toastTime <= 0)
            toast = -1;
    }
    if (toast < 0 && pending)
    {
        for (int i = 0; i < Count; ++i)
            if (pending & (uint64_t(1) << i))
            {
                pending &= ~(uint64_t(1) << i);
                toast = i;
                toastTime = 6;
#ifdef __3DS__
                CanePlayTrophyChime3ds();
#endif
                break;
            }
    }
}

#ifdef __3DS__
namespace
{
glm::vec4 MedalColor(Tier tier)
{
    if (tier == Bronze)
        return glm::vec4(.78f, .46f, .23f, 1);
    if (tier == Silver)
        return glm::vec4(.74f, .82f, .9f, 1);
    if (tier == Gold)
        return glm::vec4(1, .73f, .16f, 1);
    return glm::vec4(.68f, .9f, 1, 1);
}
void Cup(float x, float y, float size, Tier tier, bool earned, float alpha)
{
    // Compact trophy pixel design adapted from Conker3DSRecomp's popup.
    static constexpr const char* shape[16]={
        "....OOOOOOOO....", "...OYYYYYYYYO...", ".OOYWYYYYYYAYOO.", "OYOYYWYYYYYAYOYO",
        "OYOYYWYYYYYAYOYO", ".OOYYYYYYYYAAOO.", "...OOYYYYAAOO...", "....OYYYYAAO....",
        ".....OYYAAO.....", "......OYAO......", "......OYAO......", ".....OYYYYO.....",
        "....OYYYYAAO....", "...OAAAAAAAAO...", "...OOOOOOOOOO...", "................"};
    glm::vec4 body=MedalColor(tier);
    if(!earned)body=glm::mix(glm::vec4(.28f,.28f,.28f,1),body,.25f);
    const float scale=size/16.f;
    for(int row=0;row<16;++row)for(int col=0;col<16;) {
        const char c=shape[row][col];int end=col+1;
        while(end<16 && shape[row][end]==c)++end;
        if(c!='.') {
            glm::vec4 color=c=='O'?glm::vec4(.055f,.04f,.025f,alpha):
                c=='W'?glm::mix(body,glm::vec4(1),.55f):c=='A'?body*.6f:body;
            color.a=alpha;
            CaneQuadNative(x+col*scale,y+row*scale,(end-col)*scale,scale,0,color);
        }
        col=end;
    }
}
// Use the same authored scaling, aspect correction and rich text as DrawPrompt.
float MenuText(const char *value, float x, float y, float w, float factor, glm::vec4 color, JH justify = JH_Center,
               bool wrap = false)
{
    auto *font = g_prompt.pfont;
    const auto scale = GetGuiScale();
    font->PushScaling(factor * scale.x, factor * scale.y);
    CRichText text(value, font);
    float height = text.DyWrap(wrap ? w : 0.0f);
    CTextBox box;
    box.SetPos(x, y);
    box.SetSize(w, height);
    box.SetTextColor(&color);
    box.SetHorizontalJust(justify);
    box.SetVerticalJust(JV_Top);
    text.Draw(&box, nullptr);
    font->PopScaling();
    return height;
}
float MenuWidth(const char *value, float factor)
{
    auto *font = g_prompt.pfont;
    const auto scale = GetGuiScale();
    font->PushScaling(factor * scale.x, factor * scale.y);
    CRichText text(value, font);
    const float width = text.DxMaxLine();
    font->PopScaling();
    return width;
}
float MenuHeight(const char *value, float width, float factor)
{
    auto *font = g_prompt.pfont;
    const auto scale = GetGuiScale();
    font->PushScaling(factor * scale.x, factor * scale.y);
    CRichText text(value, font);
    const float height = text.DyWrap(width);
    font->PopScaling();
    return height;
}
glm::vec4 Selection(float alpha)
{
    float pulse = std::sin(g_clock.tReal * 10.0f) * .5f + .5f;
    return glm::vec4(111.f / 128, 111.f / 128, (47.f + pulse * 64.f) / 128, alpha);
}
} // namespace
#endif
void DrawMenuBody(PROMPT *prompt, float x, float y, float w, float h, float alpha)
{
#ifdef __3DS__
    const auto scale = GetGuiScale();
    const float factor = 1.3f * prompt->uOn;
    const int selected = std::clamp(prompt->irespk, 0, Count);
    constexpr int rows=4;
    const int first=std::min(selected,Count-1)/rows*rows;
    const float rowHeight=MenuHeight("Trophy",0,factor)*1.15f;
    const float iconSize=16.f*prompt->uOn;
    const float gutter=iconSize+10.f*scale.x;
    const glm::vec4 normal(95.f/128,95.f/128,95.f/128,alpha);
    for(int row=0;row<rows && first+row<Count;++row) {
        const int i=first+row;
        const float yy=y+row*rowHeight;
        const bool earned=Unlocked(i),active=selected==i;
        Cup(x,yy+2.f*scale.y,iconSize,definitions[i].tier,earned,alpha);
        float titleFactor=factor;
        const float available=w-gutter;
        // Measure the actual pixel-spaced font rather than assuming widths
        // remain proportional at the smallest native sizes.
        for(int step=0;step<20 && MenuWidth(definitions[i].title,titleFactor)>available;++step)
            titleFactor*=.98f;
        MenuText(definitions[i].title,x+gutter,yy,available,titleFactor,
                 active?Selection(alpha):earned?glm::vec4(1,1,1,alpha):normal,JH_Left);

    }
    const float detailY=y+rows*rowHeight+3.f*scale.y;
    DrawLineScreen(x,detailY,0,x+w,detailY,0,normal,false);
    if(selected<Count) {
        const float infoFactor=1.05f*prompt->uOn;
        const float labelHeight=MenuText(definitions[selected].title,x,detailY+6.f*scale.y,
                                        w,infoFactor,glm::vec4(1,1,1,alpha));
        MenuText(VisibleDescription(selected),x,detailY+10.f*scale.y+labelHeight,
                 w,infoFactor,glm::vec4(1,1,1,alpha),JH_Center,true);
    }
    const float bottom=prompt->yOn+prompt->dy*.5f+prompt->dy*.5f*scale.y*prompt->uOn;
    const float footerY=bottom-MenuHeight("Back",0,factor);
    MenuText("Back",x,footerY,w,factor,selected==Count?Selection(alpha):normal);
    char progress[40];
    std::snprintf(progress,sizeof progress,"%d / %d",UnlockedCount(),Count);
    MenuText(progress,x,footerY+3.f*scale.y,w,.85f*prompt->uOn,normal,JH_Left);
    char page[24];
    std::snprintf(page,sizeof page,"%d / %d",first/rows+1,(Count+rows-1)/rows);
    MenuText(page,x,footerY+3.f*scale.y,w,.85f*prompt->uOn,normal,JH_Right);

#endif
}
void Draw()
{
    if (toast < 0 || !g_prompt.pfont)
        return;
#ifdef __3DS__
    if(!CaneTrophyPopups3ds())return;
    // Conker3DSRecomp render_conker_toast_rgba8 / draw_toast layout:
    // 212x40, six-pixel screen inset, cup (8,12), text (30,6)/(30,22).
    // Use Sly's font and a neutral dark interior; no Conker game texture.
    constexpr float w=212,h=40,x=400-6-w,y=240-6-h,textWidth=174;
    const float alpha=std::clamp(std::min((6.f-toastTime)*8.f,toastTime*5.f),0.f,1.f);
    // Sly prompt treatment: blue frame, translucent black, neutral text.
    const glm::vec4 frame(0,.48f,.72f,alpha);
    CaneQuadNative(x+2,y+2,w-4,h-4,0,glm::vec4(0,0,0,alpha*.78f));
    CaneQuadNative(x,y,w,2,0,frame);
    CaneQuadNative(x,y+h-2,w,2,0,frame);
    CaneQuadNative(x,y+2,2,h-4,0,frame);
    CaneQuadNative(x+w-2,y+2,2,h-4,0,frame);
    const char* heading="Trophy unlocked";
    float headingFactor=12.f/std::max(1.f,MenuHeight(heading,0,1));
    float titleFactor=17.f/std::max(1.f,MenuHeight(definitions[toast].title,0,1));
    for(int step=0;step<24 && MenuWidth(heading,headingFactor)>textWidth;++step)headingFactor*=.98f;
    for(int step=0;step<24 && MenuWidth(definitions[toast].title,titleFactor)>textWidth;++step)titleFactor*=.98f;
    Cup(x+8,y+12,16,definitions[toast].tier,true,alpha);
    MenuText(heading,x+30,y+5,textWidth,headingFactor,glm::vec4(glm::vec3(95.f/128),alpha),JH_Left);
    MenuText(definitions[toast].title,x+30,y+18,textWidth,titleFactor,glm::vec4(glm::vec3(127.f/128),alpha),JH_Left);


#endif
}
} // namespace trophies
