#pragma once
#include <algorithm>

namespace morphgui {
struct PanelRect {float x,y,w,h;};
struct PanelPair {PanelRect main,side;};
// Keep a right-hand companion inside the game's viewport; very narrow views
// retain an overlapping, reachable manager rather than moving it off screen.
inline PanelPair ArrangePanels(PanelRect viewport,PanelRect main,float gap){
    const auto clamp=[](float value,float low,float high){return (std::clamp)(value,low,(std::max)(low,high));};
    main.h=clamp(main.h,(std::min)(420.f,viewport.h),viewport.h);
    const bool beside=viewport.w>=960.f+gap;
    main.w=clamp(main.w,(std::min)(480.f,viewport.w),beside?viewport.w-480.f-gap:viewport.w);
    const float sideWidth=(std::min)(640.f,beside?viewport.w-main.w-gap:viewport.w);
    main.x=clamp(main.x,viewport.x,viewport.x+viewport.w-main.w-(beside?sideWidth+gap:0.f));
    main.y=clamp(main.y,viewport.y,viewport.y+viewport.h-main.h);
    return {main,{beside?main.x+main.w+gap:viewport.x+viewport.w-sideWidth,main.y,sideWidth,main.h}};
}
}
