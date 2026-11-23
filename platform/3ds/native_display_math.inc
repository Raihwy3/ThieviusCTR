#pragma once
#include <algorithm>
#include <cmath>
struct CaneStereoWarp {float z,w,limit;};
// Apply parallel-eye/off-axis disparity after projection. Because the shift
// is homogeneous in clip Z/W, CPU-projected cel outlines receive the same
// depth as the mesh without repeating animation or outline preparation.
inline CaneStereoWarp CaneMakeStereoWarp(float nearClip,float farClip,float reference,float amount,int eye) {
    if(!(nearClip>0) || !(farClip>nearClip) || !std::isfinite(reference) || !std::isfinite(amount))return {0,0,0};
    reference=std::clamp(reference,nearClip*3,std::max(nearClip*3,farClip*.25f));
    const float convergence=reference*1.5f;
    const float a=nearClip/(nearClip-farClip),b=nearClip*farClip/(nearClip-farClip);
    const float k=(eye?1.f:-1.f)*reference*.025f*std::clamp(amount,0.f,2.f);
    return {k/b,k*(a/b-1/convergence),.025f*std::clamp(amount,0.f,1.f)};
}
