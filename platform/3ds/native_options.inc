// Native options live beside the launcher rather than in the desktop config.
// Keep this separate from game saves and the existing audio configuration.
extern void CaneStopTrophyChime3ds();
static bool caneTrophyPopups=false;
bool CaneTrophyPopups3ds(){return caneTrophyPopups;}
static bool caneNewButtonIcons=true;
static bool caneSwapBA=true;
static bool caneCameraInvertX=true;
static bool caneCameraInvertY=false;
static bool caneBinocInvertX=false;
static bool caneBinocInvertY=true;
static int caneDisplayMode=2; // Native, Wide, 3D, SSAA, PICA spatial AA, PICA sharpen, 3D 2x vertical
static bool caneBottomCoins=true,caneBottomHealth=true,caneBottomMonitor=false;
static bool caneBottomBottles=false;
bool CaneBottomCoins3ds() {return caneBottomCoins;}
bool CaneBottomHealth3ds() {return caneBottomHealth;}
bool CaneBottomBottles3ds() {return caneBottomBottles;}
bool CaneBottomMonitor3ds() {return caneBottomMonitor;}
static bool caneIntroSeen=false,caneSkipIntro=false;
bool CaneSkipIntro3ds() {return caneIntroSeen && caneSkipIntro;}
bool CaneIntroSeen3ds() {return caneIntroSeen;}
static int caneRenderCap=30;
static int caneStereoStrength=100;
static int caneStereoConvergence=100;
static int caneStereoPopout=100;
static int caneStereoHud=50;
static bool caneStereoAuto=true;
int CaneStereoOption3ds(int index) {switch(index){case 0:return caneDisplayMode==2 || caneDisplayMode==6;case 1:return caneStereoStrength;case 2:return caneStereoAuto;case 3:return caneStereoConvergence;case 4:return caneStereoPopout;default:return caneStereoHud;}}
static bool caneGyroEnabled=true;
static int caneGyroSensitivity=100;
static bool caneLowShadowDetail=false;
int CaneDisplayMode3ds() {return caneDisplayMode;}
int CaneRenderCap3ds() {return caneRenderCap;}
int CaneStereoStrength3ds() {return caneStereoStrength;}
bool CaneGyroEnabled3ds() {return caneGyroEnabled;}
int CaneGyroSensitivity3ds() {return caneGyroSensitivity;}
int CaneShadowSize3ds() {return caneLowShadowDetail?64:128;}
bool CaneUseNewButtons3ds() {return caneNewButtonIcons;}
bool CaneSwapBA3ds() {return caneSwapBA;}
bool CaneCameraInvertX3ds() {return caneCameraInvertX;}
bool CaneCameraInvertY3ds() {return caneCameraInvertY;}
bool CaneBinocInvertX3ds() {return caneBinocInvertX;}
bool CaneBinocInvertY3ds() {return caneBinocInvertY;}
void CaneLoadNativeOptions3ds() {
    if(FILE* file=std::fopen("sdmc:/3ds/ThieviusCTR/native-options.cfg","r")) {
        char key[64];int value;
        while(std::fscanf(file,"%63s %d",key,&value)==2)
            if(std::strcmp(key,"button_icons")==0 && (value==0 || value==1))caneNewButtonIcons=value!=0;
            else if(std::strcmp(key,"dialogue_skip_controls")==0 && (value==0 || value==1))g_fSkipBinocDialogs=value!=0;
            else if(std::strcmp(key,"swap_ba")==0 && (value==0 || value==1))caneSwapBA=value!=0;
            else if(std::strcmp(key,"camera_invert_x")==0 && (value==0 || value==1))caneCameraInvertX=value!=0;
            else if(std::strcmp(key,"camera_invert_y")==0 && (value==0 || value==1))caneCameraInvertY=value!=0;
            else if(std::strcmp(key,"binoc_invert_x")==0 && (value==0 || value==1))caneBinocInvertX=value!=0;
            else if(std::strcmp(key,"binoc_invert_y")==0 && (value==0 || value==1))caneBinocInvertY=value!=0;
            else if(std::strcmp(key,"display_mode")==0 && value>=0 && value<=6)caneDisplayMode=value;
            else if(std::strcmp(key,"bottom_coins")==0 && value>=0 && value<=1)caneBottomCoins=value;
            else if(std::strcmp(key,"bottom_health")==0 && value>=0 && value<=1)caneBottomHealth=value;
            else if(std::strcmp(key,"bottom_bottles")==0 && value>=0 && value<=1)caneBottomBottles=value;
            else if(std::strcmp(key,"bottom_monitor")==0 && value>=0 && value<=1)caneBottomMonitor=value;
            else if(std::strcmp(key,"intro_seen")==0 && value>=0 && value<=1)caneIntroSeen=value;
            else if(std::strcmp(key,"trophy_popups")==0 && (value==0 || value==1))caneTrophyPopups=value!=0;
            else if(std::strcmp(key,"skip_intro")==0 && value>=0 && value<=1)caneSkipIntro=value;
            else if(std::strcmp(key,"render_cap")==0 && (value==30 || value==60))caneRenderCap=value;
            else if(std::strcmp(key,"stereo_strength")==0 && value>=25 && value<=200)caneStereoStrength=value;
            else if(std::strcmp(key,"stereo_convergence")==0 && value>=25 && value<=200)caneStereoConvergence=value;
            else if(std::strcmp(key,"stereo_popout")==0 && value>=0 && value<=200)caneStereoPopout=value;
            else if(std::strcmp(key,"stereo_hud")==0 && value>=0 && value<=100)caneStereoHud=value;
            else if(std::strcmp(key,"stereo_auto")==0 && value>=0 && value<=1)caneStereoAuto=value;
            else if(std::strcmp(key,"gyro_enabled")==0 && (value==0 || value==1))caneGyroEnabled=value!=0;
            else if(std::strcmp(key,"gyro_sensitivity")==0 && value>=25 && value<=400)caneGyroSensitivity=value;
            else if(std::strcmp(key,"low_shadow_detail")==0 && (value==0 || value==1))caneLowShadowDetail=value!=0;
        std::fclose(file);
    }
    std::fprintf(stderr,"Button icons: %s (loaded)\n",caneNewButtonIcons?"New 3DS":"PS2");
    std::fprintf(stderr,"Swap B/A: %s (loaded)\n",caneSwapBA?"On":"Off");
    std::fprintf(stderr,"Trophy popups: %s (loaded)\n",caneTrophyPopups?"On":"Off");
    std::fprintf(stderr,"Invert camera=%d,%d binoc=%d,%d (loaded)\n",
        int(caneCameraInvertX),int(caneCameraInvertY),int(caneBinocInvertX),int(caneBinocInvertY));
}
static bool CaneSaveNativeOptions3ds() {
    const char* path="sdmc:/3ds/ThieviusCTR/native-options.cfg";
    const char* temp="sdmc:/3ds/ThieviusCTR/native-options.cfg.tmp";
    bool saved=false;
    if(FILE* file=std::fopen(temp,"w")) {
        bool written=std::fprintf(file,
            "button_icons %d\ndialogue_skip_controls %d\nswap_ba %d\n"
            "camera_invert_x %d\ncamera_invert_y %d\nbinoc_invert_x %d\nbinoc_invert_y %d\n",
            int(caneNewButtonIcons),int(g_fSkipBinocDialogs),int(caneSwapBA),
            int(caneCameraInvertX),int(caneCameraInvertY),int(caneBinocInvertX),int(caneBinocInvertY))>0;
        written=written && std::fprintf(file,
            "display_mode %d\nrender_cap %d\nstereo_strength %d\ngyro_enabled %d\ngyro_sensitivity %d\nlow_shadow_detail %d\n",
            caneDisplayMode,caneRenderCap,caneStereoStrength,int(caneGyroEnabled),caneGyroSensitivity,int(caneLowShadowDetail))>0;
        written=written && std::fprintf(file,"stereo_convergence %d\nstereo_popout %d\nstereo_hud %d\nstereo_auto %d\n",caneStereoConvergence,caneStereoPopout,caneStereoHud,int(caneStereoAuto))>0;
        written=written && std::fprintf(file,"bottom_coins %d\nbottom_health %d\nbottom_monitor %d\n",int(caneBottomCoins),int(caneBottomHealth),int(caneBottomMonitor))>0;
        written=written && std::fprintf(file,"bottom_bottles %d\n",int(caneBottomBottles))>0;
        written=written && std::fprintf(file,"intro_seen %d\nskip_intro %d\n",int(caneIntroSeen),int(caneSkipIntro))>0;
        written=written && std::fprintf(file,"trophy_popups %d\n",int(caneTrophyPopups))>0;
        bool closed=std::fclose(file)==0;
        if(written && closed)saved=std::rename(temp,path)==0;
        if(!saved)std::remove(temp);
    }
    return saved;
}
void CaneToggleTrophyPopups3ds(){
    caneTrophyPopups=!caneTrophyPopups;
    if(!caneTrophyPopups)CaneStopTrophyChime3ds();
    CaneSaveNativeOptions3ds();
}
void CaneSetNewButtons3ds(bool enabled) {
    caneNewButtonIcons=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Button icons: %s (saved=%d)\n",enabled?"New 3DS":"PS2",int(saved));
}
void CaneSetDialogueSkip3ds(bool enabled) {
    g_fSkipBinocDialogs=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Dialogue skip controls: %d (saved=%d)\n",int(enabled),int(saved));
}
void CaneSetSwapBA3ds(bool enabled) {
    caneSwapBA=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Swap B/A: %s (saved=%d)\n",enabled?"On":"Off",int(saved));
}
void CaneSetCameraInvertX3ds(bool enabled) {
    caneCameraInvertX=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Camera Invert X: %s (saved=%d)\n",enabled?"On":"Off",int(saved));
}
void CaneSetCameraInvertY3ds(bool enabled) {
    caneCameraInvertY=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Camera Invert Y: %s (saved=%d)\n",enabled?"On":"Off",int(saved));
}
void CaneSetBinocInvertX3ds(bool enabled) {
    caneBinocInvertX=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Binoc Invert X: %s (saved=%d)\n",enabled?"On":"Off",int(saved));
}
void CaneSetBinocInvertY3ds(bool enabled) {
    caneBinocInvertY=enabled;bool saved=CaneSaveNativeOptions3ds();
    std::fprintf(stderr,"Binoc Invert Y: %s (saved=%d)\n",enabled?"On":"Off",int(saved));
}

void CaneCycleDisplayMode3ds(int direction) {
    // Retain filter implementations/config IDs, but hide the experimental
    // PICA-SR choices from the user-facing display selector for now.
    constexpr int visible[]={0,1,2,3,6};
    int index=-1;
    for(int i=0;i<5;++i)if(visible[i]==caneDisplayMode)index=i;
    caneDisplayMode=index<0?(direction<0?3:6):visible[(index+(direction<0?4:1))%5];
    CaneSaveNativeOptions3ds();
}
void CaneToggleBottomOption3ds(int option) {if(option==0)caneBottomCoins=!caneBottomCoins;else if(option==1)caneBottomHealth=!caneBottomHealth;else caneBottomMonitor=!caneBottomMonitor;CaneSaveNativeOptions3ds();}
void CaneCycleBottomRight3ds(int direction) {
    int state=caneBottomHealth?(caneBottomBottles?2:1):0;
    state=(state+(direction<0?2:1))%3;
    caneBottomHealth=state!=0;caneBottomBottles=state==2;CaneSaveNativeOptions3ds();
}
void CaneAdjustStereoOption3ds(int index,int direction) {
    const int step=direction<0?-1:1;
    switch(index) {
    case 0:caneDisplayMode=(caneDisplayMode==2 || caneDisplayMode==6)?0:2;break;
    case 1:caneStereoStrength=std::clamp(caneStereoStrength+step*25,25,200);break;
    case 2:caneStereoAuto=!caneStereoAuto;break;
    case 3:caneStereoConvergence=std::clamp(caneStereoConvergence+step*25,25,200);break;
    case 4:caneStereoPopout=std::clamp(caneStereoPopout+step*25,0,200);break;
    case 5:caneStereoHud=std::clamp(caneStereoHud+step*10,0,100);break;
    }
    CaneSaveNativeOptions3ds();
}
void CaneToggleRenderCap3ds() {caneRenderCap=caneRenderCap==30?60:30;CaneSaveNativeOptions3ds();}
void CaneCycleStereoStrength3ds(int direction) {caneStereoStrength+=direction<0?-25:25;if(caneStereoStrength<25)caneStereoStrength=200;if(caneStereoStrength>200)caneStereoStrength=25;CaneSaveNativeOptions3ds();}
void CaneToggleGyro3ds() {caneGyroEnabled=!caneGyroEnabled;CaneSaveNativeOptions3ds();}
void CaneCycleGyroSensitivity3ds(int direction) {caneGyroSensitivity+=direction<0?-25:25;if(caneGyroSensitivity<25)caneGyroSensitivity=400;if(caneGyroSensitivity>400)caneGyroSensitivity=25;CaneSaveNativeOptions3ds();}
void CaneToggleShadowDetail3ds() {caneLowShadowDetail=!caneLowShadowDetail;CaneSaveNativeOptions3ds();}

void CaneMarkIntroSeen3ds() {
    if(caneIntroSeen)return;
    caneIntroSeen=true;caneSkipIntro=true;CaneSaveNativeOptions3ds();
}
void CaneToggleSkipIntro3ds() {
    if(caneIntroSeen)caneSkipIntro=!caneSkipIntro;
    CaneSaveNativeOptions3ds();
}
