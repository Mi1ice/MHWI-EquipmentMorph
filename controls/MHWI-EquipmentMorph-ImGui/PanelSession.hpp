#pragma once
#include "PanelData.hpp"
#include <map>
#include <span>
namespace morphpanel {
class Session {
    MhmApi api{};MhmLimits limits{};uint64_t source=0,revision=0;
    using Identity=std::pair<uint64_t,uint32_t>;
    std::map<Identity,float> owned;
    struct Metadata {MhmTargetInfo target{};std::vector<MhmKeyInfo> keys;};
    std::map<uint64_t,Metadata> cache;
    MhmResult Read(Snapshot& next){
        next.revision=0;
        for(auto& t:next.targets){
            std::vector<MhmWeightState> states(t.count);uint32_t count=0;uint64_t requested=0;
            const auto result=api.read_weights(t.info.handle,states.data(),t.count,&count,&requested,&t.appliedRevision);
            if(result!=MHM_OK)return result;if(count!=t.count)return MHM_STALE;
            next.revision=(std::max)(next.revision,requested);
            for(uint32_t k=0;k<count;++k)next.keys[t.first+k].state=states[k];
        }return Valid(next,limits)?MHM_OK:MHM_INVALID;
    }
public:
    Session()=default;~Session(){Release();}Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    MhmResult Connect(const MhmApi& table){
        constexpr auto required=MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE|MHM_CAP_PART_LIFECYCLE;
        if(table.version!=MHM_API_VERSION||table.bytes!=sizeof(table)||(table.capabilities&required)!=required||!table.limits||!table.target_at||!table.key_at||!table.open_source||!table.close_source||!table.heartbeat||!table.submit||!table.read_weights)return MHM_VERSION;
        MhmLimits next{};const auto result=table.limits(&next);if(result!=MHM_OK)return result;
        if(!next.targets||!next.keys_per_target||!next.batch_updates)return MHM_CAPACITY;
        Release();api=table;limits=next;cache.clear();return MHM_OK;
    }
    uint64_t Source()const{return source;}const MhmLimits& Limits()const{return limits;}
    MhmResult Release(){const auto result=source?api.close_source(source):MHM_OK;source=0;owned.clear();return result==MHM_STALE?MHM_OK:result;}
    MhmResult Heartbeat(){const auto result=source?api.heartbeat(source):MHM_OK;if(result==MHM_STALE){source=0;owned.clear();}return result;}
    MhmResult Weights(morphpanel::Snapshot& output){
        if(!api.read_weights)return MHM_NOT_READY;if(!Valid(output,limits))return MHM_INVALID;
        auto next=output;const auto result=Read(next);if(result!=MHM_OK)return result;output=std::move(next);return MHM_OK;
    }
    MhmResult Snapshot(morphpanel::Snapshot& output){
        if(!api.target_at)return MHM_NOT_READY;
        morphpanel::Snapshot next;next.capabilities=api.capabilities;std::map<uint64_t,Metadata> nextCache;
        for(uint32_t i=0;i<=MaxTargets;++i){
            MhmTargetInfo info{};const auto result=api.target_at(i,&info);if(result==MHM_NOT_READY)break;if(result!=MHM_OK)return result;
            if(i==MaxTargets||i>=limits.targets||!info.key_count||info.key_count>(std::min)(KeysPerTarget,limits.keys_per_target)||!Identifier(info.model_id))return MHM_CAPACITY;
            mhmcontrol::Target t;t.info=info;t.first=next.keyCount;t.count=info.key_count;
            Metadata meta;const auto found=cache.find(info.handle);
            if(found!=cache.end()){
                if(found->second.target.part!=info.part||found->second.target.key_count!=info.key_count||std::string(found->second.target.model_id)!=info.model_id)return MHM_STALE;
                meta=found->second;
            }else{meta.target=info;meta.keys.resize(info.key_count);for(uint32_t k=0;k<info.key_count;++k){const auto r=api.key_at(info.handle,k,&meta.keys[k]);if(r!=MHM_OK)return r;}}
            for(uint32_t k=0;k<info.key_count;++k){mhmcontrol::Key key;key.target=info.handle;key.index=k;key.info=meta.keys[k];const auto own=owned.find({key.target,k});if(own!=owned.end()){key.owned=1;key.contribution=own->second;}next.keys.push_back(key);}
            next.keyCount+=info.key_count;next.targets.push_back(t);++next.targetCount;nextCache.emplace(info.handle,std::move(meta));
        }
        const auto read=Read(next);if(read!=MHM_OK)return read;
        for(uint32_t i=0;i<next.targetCount;++i){MhmTargetInfo again{};if(api.target_at(i,&again)!=MHM_OK||again.handle!=next.targets[i].info.handle)return MHM_STALE;}
        MhmTargetInfo extra{};const auto extraResult=api.target_at(next.targetCount,&extra);if(extraResult==MHM_OK)return MHM_STALE;if(extraResult!=MHM_NOT_READY)return extraResult;
        for(auto it=owned.begin();it!=owned.end();){const auto t=nextCache.find(it->first.first);if(t==nextCache.end()||it->first.second>=t->second.keys.size())it=owned.erase(it);else ++it;}
        if(owned.empty()&&source)Release();next.source=source;cache.swap(nextCache);output=std::move(next);return MHM_OK;
    }
    MhmResult Replace(std::span<const MhmUpdate> updates){
        if(!api.submit)return MHM_NOT_READY;if(updates.size()>(std::min)(MaxKeys,limits.batch_updates))return MHM_CAPACITY;
        std::map<Identity,float> candidate;for(const auto& u:updates){if(!std::isfinite(u.weight)||u.weight<0||u.weight>1||!candidate.emplace(Identity{u.target,u.key},u.weight).second)return MHM_INVALID;}
        if(candidate.empty())return Release();bool opened=false;
        if(!source){const auto r=api.open_source("MHWI-EquipmentMorph-ControlPanel",220,2000,&source);if(r!=MHM_OK)return r;opened=true;}
        const auto result=api.submit(source,updates.data(),static_cast<uint32_t>(updates.size()),&revision);if(result==MHM_OK)owned.swap(candidate);else if(opened)Release();return result;
    }
    MhmResult ChangeMany(std::span<const mhmcontrol::Request> requests){
        auto candidate=owned;std::set<Identity> seen;
        for(const auto& r:requests){if(r.action!=mhmcontrol::Action::Set&&r.action!=mhmcontrol::Action::Drop)return MHM_INVALID;
            const Identity id{r.target,r.key};if(!seen.insert(id).second)return MHM_INVALID;
            // Revalidate the generation at the API boundary; stable metadata need not be re-enumerated.
            MhmKeyInfo key{};const auto result=api.key_at(r.target,r.key,&key);if(result!=MHM_OK)return result;
            if(r.action==mhmcontrol::Action::Set){if(!std::isfinite(r.weight)||r.weight<0||r.weight>1||r.weight<key.minimum||r.weight>key.maximum)return MHM_INVALID;candidate[id]=r.weight;}else candidate.erase(id);
        }
        std::vector<MhmUpdate> updates;updates.reserve(candidate.size());for(const auto& [id,value]:candidate)updates.push_back({id.first,id.second,value});return Replace(updates);
    }
};
}
