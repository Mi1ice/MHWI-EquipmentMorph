#pragma once
#include "../EquipmentMorphPerKeyCore/include/mhw_morph_api.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace mhmcontrol {
inline constexpr uint32_t Magic=0x434d484d, Version=1, MaxTargets=5, MaxKeys=320;
enum class Action:uint32_t { Poll=0, Set=1, Drop=2, Release=3 };
struct Request {
    uint32_t magic=Magic,version=Version,bytes=40;
    Action action=Action::Poll;
    uint64_t sequence=0,target=0;
    uint32_t key=0;float weight=0;
};
struct Target { MhmTargetInfo info{};uint64_t appliedRevision=0;uint32_t first=0,count=0; };
struct Key { uint64_t target=0;uint32_t index=0,owned=0;MhmKeyInfo info{};MhmWeightState state{};float contribution=0;uint32_t reserved=0; };
struct Response {
    uint32_t magic=Magic,version=Version,bytes=sizeof(Response);MhmResult result=MHM_OK;
    uint64_t sequence=0,source=0,revision=0;
    uint32_t pid=0,capabilities=0,targetCount=0,keyCount=0;
    std::array<Target,MaxTargets> targets{};std::array<Key,MaxKeys> keys{};
};
static_assert(sizeof(Request)==40 && sizeof(Target)==160 && sizeof(Key)==120);
static_assert(sizeof(Response)<65536);
inline bool Valid(const Request& r,uint64_t previous) {
    return r.magic==Magic&&r.version==Version&&r.bytes==sizeof(Request)&&r.sequence>previous
        &&static_cast<uint32_t>(r.action)<=3&&std::isfinite(r.weight);
}
inline bool Valid(const Response& r,uint64_t sequence,uint32_t pid) {
    if(r.magic!=Magic||r.version!=Version||r.bytes!=sizeof(Response)||r.sequence!=sequence||r.pid!=pid
       ||r.targetCount>MaxTargets||r.keyCount>MaxKeys)return false;
    for(uint32_t i=0;i<r.targetCount;i++){
        const auto& t=r.targets[i];
        if(!t.info.handle||t.info.part>4||t.count>64||t.count!=t.info.key_count||t.first>r.keyCount||t.count>r.keyCount-t.first
           ||t.info.model_id[127]!=0)return false;
        for(uint32_t k=0;k<t.count;k++){
            const auto& v=r.keys[t.first+k];
            if(v.target!=t.info.handle||v.index!=k||v.info.id[63]!=0||v.owned>1
               ||!std::isfinite(v.info.minimum)||!std::isfinite(v.info.maximum)||v.info.minimum>v.info.maximum
               ||!std::isfinite(v.state.requested)||!std::isfinite(v.state.applied)||!std::isfinite(v.contribution))return false;
        }
    }
    return true;
}
}
