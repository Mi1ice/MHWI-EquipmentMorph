#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace mhm::meshcontract {
// Confirmed against exact-build runtime records. Never infer unknown registration IDs.
struct Format {uint32_t disk,native,stride;};
inline constexpr Format formats[]{
    {0xf637401c,4,32}, // IASkin4wt1UV
    {0x3c730760,6,36}, // IASkin4wt1UVColor
    {0x366995a7,10,44} // IASkin8wt1UVColor
};
inline std::optional<Format> Resolve(uint32_t disk){for(const auto& f:formats)if(f.disk==disk)return f;return {};}
inline uint32_t U32(std::span<const unsigned char> row,size_t offset){
    return uint32_t(row[offset])|(uint32_t(row[offset+1])<<8)|(uint32_t(row[offset+2])<<16)|(uint32_t(row[offset+3])<<24);
}
inline bool FormatsMatch(std::span<const unsigned char> rows,std::span<const uint32_t> expected){
    if(expected.empty()||rows.size()!=expected.size()*80)return false;
    for(size_t i=0;i<expected.size();i++){
        const auto format=Resolve(expected[i]);const auto row=rows.subspan(i*80,80);
        if(!format||row[14]!=format->stride||U32(row,24)!=format->native)return false;
    }return true;
}
inline bool Normalize(std::vector<unsigned char>& rows,bool unified){
    if(rows.empty()||rows.size()%80)return false;
    for(size_t i=0;i<rows.size();i+=80){std::memset(rows.data()+i+72,0,8);if(unified){std::memset(rows.data()+i+24,0,4);rows[i+42]=0;}}
    return true;
}
}
