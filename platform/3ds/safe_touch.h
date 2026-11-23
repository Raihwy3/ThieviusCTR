#pragma once
#include <cstdlib>

// Relative swipes, not a held virtual stick: a stationary finger never repeats.
struct CaneSafeTouch {
    bool tracking = false;
    bool neutral = false;
    int anchorX = 0, anchorY = 0;

    void Update(bool active, bool contact, int x, int y, int& dx, int& dy) {
        dx = dy = 0;
        if (!active || !contact) { tracking = neutral = false; return; }
        if (!tracking) {
            tracking = true; anchorX = x; anchorY = y; return;
        }
        // Give JOY's directional selection latch a release between steps.
        if (neutral) { neutral = false; return; }
        const int horizontal = x - anchorX, vertical = y - anchorY;
        if (std::abs(horizontal) < 18 && std::abs(vertical) < 18) return;
        if (std::abs(horizontal) > std::abs(vertical))
            dx = horizontal < 0 ? -1 : 1;
        else
            dy = vertical < 0 ? 1 : -1; // JOY has positive Y up.
        anchorX = x; anchorY = y; neutral = true;
    }
};
