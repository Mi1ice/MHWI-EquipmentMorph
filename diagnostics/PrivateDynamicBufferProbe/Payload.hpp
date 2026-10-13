#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
namespace privateprobe {
inline bool Translate(std::span<const unsigned char> basis,std::span<const std::uint32_t> offsets,
                      float weight,std::span<unsigned char> output) noexcept {
    if(!std::isfinite(weight)||weight<0||weight>1||basis.size()!=output.size()||basis.empty()||offsets.empty())return false;
    std::uint32_t previous=0;
    for(unsigned i=0;i<offsets.size();i++) {
        const auto offset=offsets[i];
        if(offset>basis.size()||basis.size()-offset<12||(i>0&&offset<previous+12))return false;
        float y=0;std::memcpy(&y,basis.data()+offset+4,4);
        if(!std::isfinite(y)||!std::isfinite(y+weight))return false;
        previous=offset;
    }
    std::memcpy(output.data(),basis.data(),basis.size());
    if(weight==0)return true;
    for(const auto offset:offsets) {
        float y=0;std::memcpy(&y,basis.data()+offset+4,4);y+=weight;
        std::memcpy(output.data()+offset+4,&y,4);
    }
    return true;
}
inline bool FreshUpload(std::uint64_t stamp,std::uint64_t completedBefore,std::uint64_t previousStamp) noexcept {
    constexpr auto removed=~std::uint64_t{0};
    return stamp!=removed&&completedBefore!=removed&&stamp>completedBefore&&stamp>previousStamp;
}
}
