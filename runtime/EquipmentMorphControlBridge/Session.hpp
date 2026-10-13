#pragma once
#include "Protocol.hpp"
#include <algorithm>
#include <map>
#include <vector>

namespace mhmcontrol {
class Session {
    MhmApi api;uint64_t source=0,revision=0;
    using Identity=std::pair<uint64_t,uint32_t>;
    std::map<Identity,float> owned;
public:
    explicit Session(const MhmApi& table):api(table){}
    ~Session(){Release();}
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    uint64_t Source()const{return source;}
    MhmResult Release(){
        const auto result=source?api.close_source(source):MHM_OK;source=0;owned.clear();
        return result==MHM_STALE?MHM_OK:result;
    }
    MhmResult Heartbeat(){
        const auto result=source?api.heartbeat(source):MHM_OK;
        if(result==MHM_STALE){source=0;owned.clear();}return result;
    }
    MhmResult Snapshot(Response& output){
        Response next;next.capabilities=api.capabilities;
        for(uint32_t i=0;i<MaxTargets;i++){
            auto& t=next.targets[i];const auto result=api.target_at(i,&t.info);
            if(result==MHM_NOT_READY)break;if(result!=MHM_OK)return result;
            if(t.info.part>4||t.info.key_count>64||next.keyCount+t.info.key_count>MaxKeys)return MHM_CAPACITY;
            t.first=next.keyCount;t.count=t.info.key_count;
            std::array<MhmWeightState,64> states{};uint32_t count=0;uint64_t requested=0;
            auto read=api.read_weights(t.info.handle,states.data(),64,&count,&requested,&t.appliedRevision);
            if(read!=MHM_OK)return read;if(count!=t.count)return MHM_STALE;
            next.revision=(std::max)(next.revision,requested);
            for(uint32_t k=0;k<count;k++){
                auto& key=next.keys[next.keyCount++];key.target=t.info.handle;key.index=k;key.state=states[k];
                read=api.key_at(t.info.handle,k,&key.info);if(read!=MHM_OK)return read;
            }
            next.targetCount++;
        }
        // Target removal/replacement during enumeration must not prune a healthy contribution.
        for(uint32_t i=0;i<next.targetCount;i++){
            MhmTargetInfo again{};if(api.target_at(i,&again)!=MHM_OK||again.handle!=next.targets[i].info.handle)return MHM_STALE;
        }
        MhmTargetInfo extra{};
        if(api.target_at(next.targetCount,&extra)==MHM_OK)return MHM_STALE;
        for(auto it=owned.begin();it!=owned.end();){
            const bool present=std::any_of(next.keys.begin(),next.keys.begin()+next.keyCount,[&](const auto& k){return k.target==it->first.first&&k.index==it->first.second;});
            if(!present)it=owned.erase(it);else ++it;
        }
        if(owned.empty()&&source)Release();
        for(uint32_t i=0;i<next.keyCount;i++){
            auto& key=next.keys[i];const auto found=owned.find({key.target,key.index});
            if(found!=owned.end()){key.owned=1;key.contribution=found->second;}
        }
        next.source=source;output=next;return MHM_OK;
    }
    MhmResult Change(const Request& request){
        if(request.action==Action::Release)return Release();
        if(request.action==Action::Poll)return MHM_OK;
        MhmKeyInfo key{};auto result=api.key_at(request.target,request.key,&key);if(result!=MHM_OK)return result;
        if(request.action==Action::Set&&(!std::isfinite(request.weight)||request.weight<key.minimum||request.weight>key.maximum))return MHM_INVALID;
        auto candidate=owned;const Identity id{request.target,request.key};
        if(request.action==Action::Set)candidate[id]=request.weight;else candidate.erase(id);
        if(candidate.empty())return Release();
        if(candidate.size()>MaxKeys)return MHM_CAPACITY;
        bool opened=false;
        if(!source){result=api.open_source("MHWI-EquipmentMorph-ControlPanel",220,2000,&source);if(result!=MHM_OK)return result;opened=true;}
        std::vector<MhmUpdate> updates;updates.reserve(candidate.size());
        for(const auto& [identity,weight]:candidate)updates.push_back({identity.first,identity.second,weight});
        result=api.submit(source,updates.data(),static_cast<uint32_t>(updates.size()),&revision);
        if(result==MHM_OK)owned.swap(candidate);
        else if(opened)Release();
        return result;
    }
};
}
