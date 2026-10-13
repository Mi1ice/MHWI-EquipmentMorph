#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace launchpool {
// One independently bound part, with zero padding beyond its declared keys.
struct MorphVersion {
    std::array<float,64> weights{};
    std::uint64_t frame=0,revision=0;
    bool operator==(const MorphVersion&) const = default;
    bool Valid()const{for(float w:weights)if(!std::isfinite(w)||w<0||w>1)return false;return true;}
    bool IsZero()const{for(float w:weights)if(w!=0)return false;return true;}
};
}
