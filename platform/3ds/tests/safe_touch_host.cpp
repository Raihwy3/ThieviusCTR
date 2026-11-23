#include "../safe_touch.h"
#include <cassert>
#include <iostream>

int main() {
    CaneSafeTouch touch;
    int dx,dy;
    auto sample=[&](bool active,bool held,int x,int y,int wantX,int wantY) {
        touch.Update(active,held,x,y,dx,dy);
        assert(dx==wantX && dy==wantY);
    };
    sample(true,true,160,120,0,0); // Touchdown and small jitter do nothing.
    sample(true,true,162,117,0,0);
    sample(true,true,160,100,0,1); // Up changes the digit like Circle Pad up.
    sample(true,true,160,100,0,0);
    sample(true,true,160,100,0,0); // Holding does not repeat.
    sample(true,true,160,80,0,1); // Continuing the swipe changes it again.
    sample(true,true,160,80,0,0);
    sample(true,true,160,105,0,-1);
    sample(true,true,160,105,0,0);
    sample(true,true,185,107,1,0); // Horizontal swipe selects the next slot.
    sample(true,true,185,107,0,0);
    sample(true,true,160,107,-1,0);
    sample(true,false,0,0,0,0);
    sample(true,true,20,20,0,0); // A new contact starts a fresh gesture.
    sample(false,true,20,80,0,0); // No safe input in another context.
    sample(true,true,20,80,0,0);
    sample(true,true,42,115,0,-1); // Diagonals choose one dominant axis.
    std::cout<<"PASS safe swipes: directions, repeat, jitter, release and context reset\n";
}
