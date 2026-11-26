#include "../../../Sly1-Release/font.h"
#include <cassert>
#include <cstdio>
bool CaneSwapBA3ds(){return false;}
int main(){
    BMP atlas{};
    g_afontBrx.resize(1);
    auto& font=g_afontBrx[0];font.m_pbmp=&atlas;
    font.m_dxSpaceUnscaled=8;font.m_dyUnscaled=32;
    for(int ch=33;ch<256;++ch){GLYFF glyph{};glyph.wch=ch;glyph.dx=10+ch%15;font.m_aglyff[ch]=glyph;}
    const char* sentences[]={
        "Use your binoculars to look around and find the next rooftop. Press the button to continue.",
        "There is a guard on the far side of the courtyard. Wait until he turns around before moving.",
        "Collect all of the clue bottles in this area to discover the combination for the safe.",
        "Try ~FF0000looking~. toward the tower, then follow the rooftops to the next checkpoint.",
        "Supercalifragilisticexpialidocious is an intentionally overlong test word.",
        "The caf\xc3\xa9 is across the courtyard.\nContinue toward the checkpoint."
    };
    bool reproduced=false;
    for(const auto* sentence:sentences){
        font.PushScaling(1.25f,1.25f);CRichText old(sentence,&font);old.ClineWrap(576.f);font.PopScaling();
        font.PushScaling(.625f,.625f);reproduced|=old.DxMaxLine()>288.f;font.PopScaling();
        for(auto scaleWidth:{std::pair{.625f,288.f},std::pair{.625f,140.f},std::pair{.5f,190.f}}){
            font.PushScaling(scaleWidth.first,scaleWidth.first);
            const float safe=scaleWidth.second-font.DxFromCh('|')-1.f;
            CRichText wrapped(sentence,&font);wrapped.ClineWrap(safe);
            if(wrapped.DxMaxLine()>safe)std::fprintf(stderr,"overflow: scale=%g safe=%g measured=%g text=%s\n",scaleWidth.first,safe,wrapped.DxMaxLine(),wrapped.m_achz);
            assert(wrapped.DxMaxLine()<=safe);
            auto compact=[](const std::string& text){std::string out;for(unsigned char c:text)if(c!=' ' && c!='\t' && c!='\n')out+=char(c);return out;};
            assert(compact(wrapped.m_achz)==compact(sentence));
            assert(wrapped.DxMaxLine()+font.DxFromCh('|')<scaleWidth.second);
            font.PopScaling();
        }
    }
    assert(reproduced);
    std::puts("PASS: reproduced scaled-wrap overflow; exact draw metrics and cursor fit bottom/top dialogue widths");
}
