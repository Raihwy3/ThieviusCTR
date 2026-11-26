// Route the game's original HUD batches, as the Conker3DSRecomp renderer
// routes tagged spans. This does not copy or scale a top-screen framebuffer.
bool CaneDialogueOnBottom3ds() {
    if(!caneBottomAvailable || g_binoc.binocs==BINOCS_Confront)return false;
    return g_binoc.binocs==BINOCS_Dialog || g_binoc.binocs==BINOCS_Instruct ||
        (g_tvLeft.pspeakerDraw && g_tvLeft.tvgs!=TVGS_Closed) ||
        (g_tvRight.pspeakerDraw && g_tvRight.tvgs!=TVGS_Closed);
}
void CaneDialogueHudNative(bool enabled) {
    caneHudLayer=enabled && CaneDialogueOnBottom3ds()?CaneHudLayer::Dialogue:CaneHudLayer::Top;
    if(caneHudLayer!=CaneHudLayer::Dialogue)return;
    CaneQuadNative(0,48,320,96,0,glm::vec4(.015f,.025f,.045f,.94f));
    const glm::vec4 border(.12f,.38f,.65f,.9f);
    CaneQuadNative(0,48,320,1,0,border);CaneQuadNative(0,143,320,1,0,border);
    CaneQuadNative(0,48,1,96,0,border);CaneQuadNative(319,48,1,96,0,border);
}
static void CaneBottomTextNative(const char* text,float x,float y,float width,JH justify=JH_Left) {
    CFontBrx* font=PfontFromFont(0);if(!font)return;
    font->PushScaling(.65f,.65f);
    CTextBox box;box.SetPos(x,y);box.SetSize(width,24);
    glm::vec4 color(.78f,.91f,1,1);box.SetTextColor(&color);
    box.SetHorizontalJust(justify);box.SetVerticalJust(JV_Top);
    font->DrawPchz(text,&box);font->PopScaling();
}
static void CaneBottomBackgroundNative() {
    caneHudLayer=CaneHudLayer::Dialogue;
    CaneQuadNative(0,0,320,240,0,glm::vec4(.045f,.08f,.125f,1));
    for(int y=0;y<240;y+=16)
        CaneQuadNative(0,float(y),320,1,0,glm::vec4(.09f,.16f,.23f,.35f));
    caneHudLayer=CaneHudLayer::Top;
}
static void CaneBottomStatusNative() {
    caneHudLayer=CaneHudLayer::Dialogue;
    if(caneLoading)CaneBottomTextNative(caneLoadingText,120,214,192,JH_Right);
    else if(!CaneDialogueOnBottom3ds() && g_prompt.blots==BLOTS_Hidden) {
        if(g_ui.uis==UIS_Attract) {
            CaneQuadNative(8,156,96,50,0,glm::vec4(.04f,.09f,.15f,1));
            CaneQuadNative(112,156,96,50,0,glm::vec4(.04f,.09f,.15f,1));
            CaneQuadNative(216,156,96,50,0,glm::vec4(.04f,.09f,.15f,1));
            CaneBottomTextNative("Trophies",216,169,96,JH_Center);
            CaneBottomTextNative("Play",8,169,96,JH_Center);
            CaneBottomTextNative("Options",112,169,96,JH_Center);
        }
    }
    caneHudLayer=CaneHudLayer::Top;
}
static void CanePrepareBottomHud() {
    if(!caneBottomActive)return;
    caneHud.erase(std::remove_if(caneHud.begin(),caneHud.end(),[](const CaneHudBatch& h) {
        return (h.layer==CaneHudLayer::Coins && !CaneBottomCoins3ds()) ||
               (h.layer==CaneHudLayer::Health && !CaneBottomHealth3ds());
    }),caneHud.end());
    auto place=[](CaneHudLayer layer,TV* portrait,float left,float top,float maxWidth,float maxHeight,bool right) {
        glm::vec2 lo(1e9f),hi(-1e9f);
        for(const auto& h:caneHud)if(h.layer==layer && h.portrait==portrait)
            for(const auto& v:h.vertices) {lo=glm::min(lo,glm::vec2(v.position));hi=glm::max(hi,glm::vec2(v.position));}
        const glm::vec2 size=hi-lo;if(!(size.x>0) || !(size.y>0))return;
        const float scale=std::min(maxWidth/size.x,maxHeight/size.y);
        const float x=right?left-size.x*scale:left;
        for(auto& h:caneHud)if(h.layer==layer && h.portrait==portrait) {
            h.scale=scale;h.dx=x-lo.x*scale;h.dy=top-lo.y*scale;
        }
    };
    place(CaneHudLayer::Coins,nullptr,8,8,128,40,false);
    place(CaneHudLayer::Health,nullptr,312,8,128,40,true);
    place(CaneHudLayer::Dialogue,&g_tvLeft,0,148,156,92,false);
    place(CaneHudLayer::Dialogue,&g_tvRight,320,148,156,92,true);
}
