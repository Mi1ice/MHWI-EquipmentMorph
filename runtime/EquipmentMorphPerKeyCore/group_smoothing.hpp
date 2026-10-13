#pragma once
#include "core.hpp"

namespace mhm {
// Same analytic critically damped response (omega=8) as the visually accepted
// body prototype. All keys advance at one host timestamp, with complete-group
// validation before changing any member. No GPU scheduling belongs here.
class GroupSmoothing {
    struct Weight {double position=0,velocity=0,target=0;};
    struct Member {uint64_t target=0;std::vector<Weight> weights;};
    std::vector<Member> members;
    uint64_t last=0,revision=0;
    bool initialized=false;
    static bool Advance(std::vector<Member>& candidate,uint64_t elapsed) {
        const double dt=static_cast<double>(elapsed)/1000,omega=8,decay=std::exp(-omega*dt);
        for(auto& m:candidate)for(auto& w:m.weights) {
            const double displacement=w.position-w.target,coefficient=w.velocity+omega*displacement;
            w.position=w.target+(displacement+coefficient*dt)*decay;
            w.velocity=(w.velocity-omega*coefficient*dt)*decay;
            if(!std::isfinite(w.position)||!std::isfinite(w.velocity))return false;
            if(w.position<0){w.position=0;if(w.velocity<0)w.velocity=0;}
            if(w.position>1){w.position=1;if(w.velocity>0)w.velocity=0;}
            if(std::abs(w.position-w.target)<=1e-6&&std::abs(w.velocity)<=1e-5){w.position=w.target;w.velocity=0;}
        }
        return true;
    }
public:
    MhmResult Reset(std::span<const MhmTargetInfo> descriptors,uint64_t now) {
        if(descriptors.empty()||descriptors.size()>limits.targets)return MHM_INVALID;
        std::vector<Member> candidate;
        for(const auto& d:descriptors) {
            if(!d.handle||!d.key_count||d.key_count>limits.keys_per_target||d.part>MHM_LEGS)return MHM_INVALID;
            for(const auto& other:descriptors)if(&other!=&d&&(other.handle==d.handle||other.part==d.part))return MHM_INVALID;
            candidate.push_back({d.handle,std::vector<Weight>(d.key_count)});
        }
        std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.target<b.target;});
        members.swap(candidate);last=now;revision=0;initialized=true;return MHM_OK;
    }
    MhmResult SetTargets(std::span<const MhmUpdate> values,uint64_t requestedRevision,uint64_t now) {
        if(!initialized)return MHM_NOT_READY;
        if(now<last)return MHM_INVALID;
        if(requestedRevision<revision)return MHM_STALE;
        size_t count=0;for(const auto& m:members)count+=m.weights.size();
        if(values.size()!=count)return MHM_INVALID;
        std::map<std::pair<uint64_t,uint32_t>,float> complete;
        for(const auto& v:values) {
            const auto it=std::find_if(members.begin(),members.end(),[&](const auto& m){return m.target==v.target;});
            if(it==members.end())return MHM_STALE;
            if(v.key>=it->weights.size()||!std::isfinite(v.weight)||v.weight<0||v.weight>1||
               !complete.emplace(std::pair{v.target,v.key},v.weight).second)return MHM_INVALID;
            if(revision&&requestedRevision==revision&&static_cast<double>(v.weight)!=it->weights[v.key].target)return MHM_INVALID;
        }
        auto candidate=members;if(!Advance(candidate,now-last))return MHM_INVALID;
        for(auto& m:candidate)for(uint32_t k=0;k<m.weights.size();k++)m.weights[k].target=complete.at({m.target,k});
        members.swap(candidate);last=now;revision=requestedRevision;return MHM_OK;
    }
    MhmResult Sample(uint64_t now,std::vector<MhmUpdate>& output,uint64_t& requestedRevision) {
        if(!initialized)return MHM_NOT_READY;
        if(now<last)return MHM_INVALID;
        auto candidate=members;if(!Advance(candidate,now-last))return MHM_INVALID;
        std::vector<MhmUpdate> result;
        for(const auto& m:candidate)for(uint32_t k=0;k<m.weights.size();k++)
            result.push_back({m.target,k,static_cast<float>(m.weights[k].position)});
        members.swap(candidate);last=now;output.swap(result);requestedRevision=revision;return MHM_OK;
    }
    void Invalidate(){members.clear();last=revision=0;initialized=false;}
};
}
