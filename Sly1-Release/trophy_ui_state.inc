// Retail uses -1 as the ordinary gameplay base state, not just UIS_Playing.
namespace trophies::ui {
inline bool Playing(int current,int base) {
    if(base==0 || base==1)return false; // splash / title, including their menus
    return current>=2 && current<=6; // hub, gameplay and their pause transitions
}
}
