#pragma once
#include "../../runtime/EquipmentMorphControlBridge/Protocol.hpp"
#include <algorithm>
#include <set>
#include <string>
#include <vector>
namespace morphpanel {
inline constexpr uint32_t MaxTargets=5,KeysPerTarget=1024,MaxKeys=5120;
struct Snapshot {
    uint64_t source=0,revision=0;
    uint32_t capabilities=0,targetCount=0,keyCount=0;
    std::vector<mhmcontrol::Target> targets;
    std::vector<mhmcontrol::Key> keys;
};
template<size_t N>inline bool Identifier(const char(&s)[N]){
    if(s[N-1]!=0)return false;const auto end=std::find(s,s+N,'\0');if(end==s||end==s+N)return false;
    return std::all_of(s,end,[](unsigned char c){return c>=32&&c<=126;});
}
inline bool Valid(const Snapshot& s,const MhmLimits& limits){
    if(s.targetCount!=s.targets.size()||s.keyCount!=s.keys.size()||s.targetCount>(std::min)(MaxTargets,limits.targets)||s.keyCount>MaxKeys)return false;
    std::set<uint32_t> parts;std::set<uint64_t> handles;uint32_t first=0;
    for(const auto& t:s.targets){
        if(!t.info.handle||t.info.part>4||!parts.insert(t.info.part).second||!handles.insert(t.info.handle).second
           ||!t.count||t.count>(std::min)(KeysPerTarget,limits.keys_per_target)||t.count!=t.info.key_count||t.first!=first||first>s.keyCount||t.count>s.keyCount-first||!Identifier(t.info.model_id))return false;
        std::set<std::string> ids;
        for(uint32_t k=0;k<t.count;++k){const auto& v=s.keys[first+k];
            if(v.target!=t.info.handle||v.index!=k||!Identifier(v.info.id)||!ids.insert(v.info.id).second||v.owned>1
               ||!std::isfinite(v.info.minimum)||!std::isfinite(v.info.maximum)||v.info.minimum>v.info.maximum
               ||!std::isfinite(v.state.requested)||!std::isfinite(v.state.applied)||!std::isfinite(v.contribution))return false;
        }first+=t.count;
    }return first==s.keyCount;
}
}
