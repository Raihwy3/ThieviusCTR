// Native options live beside the launcher rather than in the desktop config.
// Keep this separate from game saves and the existing audio configuration.
static bool caneNewButtonIcons=true;
static bool caneSwapBA=true;
static bool caneCameraInvertX=true;
static bool caneCameraInvertY=false;
static bool caneBinocInvertX=false;
static bool caneBinocInvertY=true;
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
        std::fclose(file);
    }
    std::fprintf(stderr,"Button icons: %s (loaded)\n",caneNewButtonIcons?"New 3DS":"PS2");
    std::fprintf(stderr,"Swap B/A: %s (loaded)\n",caneSwapBA?"On":"Off");
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
        bool closed=std::fclose(file)==0;
        if(written && closed)saved=std::rename(temp,path)==0;
        if(!saved)std::remove(temp);
    }
    return saved;
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
