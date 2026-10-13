#pragma once
#include "group_smoothing.hpp"

namespace mhm {
// Host-only bank: independent state lifetimes, one time for each logical sample.
// Native buffer retirement and publication are the adapter's responsibility.
class PartSmoothing {
    struct Member {MhmTargetInfo descriptor{};GroupSmoothing response;};
    std::map<uint64_t,Member> members;
    uint64_t last=0;
public:
    MhmResult Reconcile(std::span<const MhmTargetInfo> descriptors,uint64_t now) {
        if(now<last||descriptors.size()>limits.targets)return MHM_INVALID;
        std::map<uint64_t,Member> candidate;
        std::array<bool,5> seen{};
        for(const auto& d:descriptors) {
            if(!d.handle||d.part>MHM_LEGS||seen[d.part]||!d.key_count
                    ||d.key_count>limits.keys_per_target||!Identifier(d.model_id,128))return MHM_INVALID;
            seen[d.part]=true;
            if(candidate.contains(d.handle))return MHM_INVALID;
            const auto old=members.find(d.handle);
            if(old!=members.end()) {
                const auto& original=old->second.descriptor;
                if(original.part!=d.part||original.key_count!=d.key_count
                        ||std::strcmp(original.model_id,d.model_id)!=0)return MHM_INVALID;
                candidate.emplace(d.handle,old->second);
            }else{
                Member member;member.descriptor=d;
                const auto result=member.response.Reset({&d,1},now);
                if(result!=MHM_OK)return result;
                candidate.emplace(d.handle,std::move(member));
            }
        }
        members.swap(candidate);last=now;return MHM_OK;
    }
    MhmResult SetTargets(std::span<const MhmUpdate> values,uint64_t revision,uint64_t now) {
        if(now<last)return MHM_INVALID;
        size_t count=0;for(const auto& [id,m]:members){(void)id;count+=m.descriptor.key_count;}
        if(values.size()!=count)return MHM_INVALID;
        auto candidate=members;
        for(auto& [id,m]:candidate) {
            std::vector<MhmUpdate> part;
            for(const auto& v:values)if(v.target==id)part.push_back(v);
            const auto result=m.response.SetTargets(part,revision,now);
            if(result!=MHM_OK)return result;
        }
        members.swap(candidate);last=now;return MHM_OK;
    }
    MhmResult Sample(uint64_t now,std::vector<MhmUpdate>& values) {
        if(now<last)return MHM_INVALID;
        auto candidate=members;std::vector<MhmUpdate> result;
        for(auto& [id,m]:candidate) {
            (void)id;std::vector<MhmUpdate> part;uint64_t revision=0;
            const auto status=m.response.Sample(now,part,revision);
            if(status!=MHM_OK)return status;
            result.insert(result.end(),part.begin(),part.end());
        }
        members.swap(candidate);last=now;values.swap(result);return MHM_OK;
    }
};
}
