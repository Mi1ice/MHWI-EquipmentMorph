#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include "vertex_formats.hpp"

namespace mhm::meshcontract {
using vertexformats::Resolve;
inline uint32_t U32(std::span<const unsigned char> row,size_t offset){
    return uint32_t(row[offset])|(uint32_t(row[offset+1])<<8)|(uint32_t(row[offset+2])<<16)|(uint32_t(row[offset+3])<<24);
}
template<class Snapshot> bool FormatsMatch(std::span<const unsigned char> rows,std::span<const uint32_t> expected,const Snapshot& declarations){
    if(expected.empty()||expected.size()>4096||rows.size()!=expected.size()*80)return false;
    for(size_t i=0;i<expected.size();i++){
        const auto format=Resolve(expected[i]);const auto row=rows.subspan(i*80,80);
        const auto index=U32(row,24);
        if(!format||row[14]!=format->stride||index==0xffff||index>=declarations.entries.size())return false;
        const auto& actual=declarations.entries[index];
        if(!actual.verified||actual.disk!=format->disk||actual.stride!=format->stride)return false;
    }return true;
}
inline bool Normalize(std::vector<unsigned char>& rows,bool unified){
    if(rows.empty()||rows.size()%80)return false;
    for(size_t i=0;i<rows.size();i+=80){std::memset(rows.data()+i+72,0,8);if(unified){std::memset(rows.data()+i+24,0,4);rows[i+42]=0;}}
    return true;
}
}
