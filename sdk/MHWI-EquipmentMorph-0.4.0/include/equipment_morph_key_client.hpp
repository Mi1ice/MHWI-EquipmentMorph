#pragma once
#include "mhw_morph_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mhwi {
struct NamedWeight {uint32_t part;std::string model,key;float weight;};
struct NamedKey {uint64_t target;uint32_t part,index;std::string model;MhmKeyInfo info;};
// API calls are thread-safe; a client instance belongs to one ordinary caller thread.
class KeyClient {
    MhmApi api{};MhmLimits limits{};uint64_t source=0;
    std::vector<NamedKey> keys;
    using Identity=std::pair<uint64_t,uint32_t>;
    std::map<Identity,float> owned;
    std::string name;uint32_t priority,lease;
    const NamedKey* Find(uint32_t part,const std::string& model,const std::string& id)const{
        const auto found=std::find_if(keys.begin(),keys.end(),[&](const auto& k){return k.part==part&&k.model==model&&id==k.info.id;});
        return found==keys.end()?nullptr:&*found;
    }
    MhmResult Publish(std::map<Identity,float> candidate,uint64_t& revision){
        if(candidate.empty())return Release();if(candidate.size()>limits.batch_updates)return MHM_CAPACITY;
        bool opened=false;
        if(!source){const auto result=api.open_source(name.c_str(),priority,lease,&source);if(result!=MHM_OK)return result;opened=true;}
        std::vector<MhmUpdate> batch;for(const auto& [id,weight]:candidate)batch.push_back({id.first,id.second,weight});
        const auto result=api.submit(source,batch.data(),static_cast<uint32_t>(batch.size()),&revision);
        if(result==MHM_OK)owned.swap(candidate);else if(opened)Release();
        return result;
    }
public:
    explicit KeyClient(std::string sourceName="equipment-morph-key-client",uint32_t sourcePriority=200,uint32_t leaseMs=2000)
        :name(std::move(sourceName)),priority(sourcePriority),lease(leaseMs){}
    KeyClient(const KeyClient&)=delete;KeyClient& operator=(const KeyClient&)=delete;
    ~KeyClient(){Release();}
    uint64_t Source()const{return source;}
    const std::vector<NamedKey>& Keys()const{return keys;}
    MhmResult Release(){const auto result=source?api.close_source(source):MHM_OK;source=0;owned.clear();return result==MHM_STALE?MHM_OK:result;}
    MhmResult Connect(const MhmApi& table){
        constexpr auto required=MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE|MHM_CAP_PART_LIFECYCLE;
        if(table.version!=MHM_API_VERSION||table.bytes!=sizeof(table)||(table.capabilities&required)!=required
           ||!table.limits||!table.target_at||!table.key_at||!table.open_source||!table.close_source||!table.submit||!table.heartbeat||!table.read_weights)return MHM_VERSION;
        MhmLimits bounds{};const auto result=table.limits(&bounds);if(result!=MHM_OK)return result;
        if(!bounds.targets||bounds.targets>5||!bounds.keys_per_target||!bounds.batch_updates
           ||bounds.keys_per_target>std::vector<MhmWeightState>().max_size()
           ||bounds.batch_updates>std::vector<MhmUpdate>().max_size())return MHM_CAPACITY;
        Release();keys.clear();api=table;limits=bounds;return MHM_OK;
    }
    MhmResult Heartbeat(){const auto result=source?api.heartbeat(source):MHM_OK;if(result==MHM_STALE){source=0;owned.clear();}return result;}
    MhmResult Refresh(){
        if(!api.target_at)return MHM_NOT_READY;
        std::vector<NamedKey> discovered;std::vector<MhmTargetInfo> targets;std::array<bool,5> seen{};
        for(uint32_t i=0;i<limits.targets;i++){
            MhmTargetInfo t{};const auto result=api.target_at(i,&t);if(result==MHM_NOT_READY)break;if(result!=MHM_OK)return result;
            if(!t.handle||t.part>4||seen[t.part]||!t.key_count||t.key_count>limits.keys_per_target||!std::memchr(t.model_id,0,128))return MHM_INVALID;
            seen[t.part]=true;targets.push_back(t);std::vector<std::string> ids;
            for(uint32_t k=0;k<t.key_count;k++){
                MhmKeyInfo info{};const auto read=api.key_at(t.handle,k,&info);if(read!=MHM_OK)return read;
                if(!std::memchr(info.id,0,64)||!info.id[0]||!std::isfinite(info.minimum)||!std::isfinite(info.maximum)||info.minimum>info.maximum||std::find(ids.begin(),ids.end(),info.id)!=ids.end())return MHM_INVALID;
                ids.emplace_back(info.id);discovered.push_back({t.handle,t.part,k,t.model_id,info});
            }
        }
        for(uint32_t i=0;i<targets.size();i++){
            MhmTargetInfo t{};if(api.target_at(i,&t)!=MHM_OK||t.handle!=targets[i].handle||t.part!=targets[i].part||t.key_count!=targets[i].key_count||std::memcmp(t.model_id,targets[i].model_id,128))return MHM_STALE;
        }
        MhmTargetInfo extra{};const auto end=api.target_at(static_cast<uint32_t>(targets.size()),&extra);if(end!=MHM_NOT_READY)return end==MHM_OK?MHM_STALE:end;
        std::erase_if(owned,[&](const auto& pair){return std::none_of(discovered.begin(),discovered.end(),[&](const auto& k){return pair.first==Identity{k.target,k.index};});});
        keys.swap(discovered);if(owned.empty()&&source)Release();return MHM_OK;
    }
    // Changes affect only named keys. All other contributions owned by this source persist.
    MhmResult Set(const std::vector<NamedWeight>& changes,uint64_t& revision){
        if(changes.empty())return MHM_INVALID;auto result=Refresh();if(result!=MHM_OK)return result;
        result=Heartbeat();if(result!=MHM_OK&&result!=MHM_STALE)return result;
        auto candidate=owned;std::map<Identity,bool> touched;
        for(const auto& value:changes){
            const auto* key=Find(value.part,value.model,value.key);if(!key)return MHM_NOT_READY;
            const Identity id{key->target,key->index};if(touched.contains(id)||!std::isfinite(value.weight)||value.weight<key->info.minimum||value.weight>key->info.maximum)return MHM_INVALID;
            touched[id]=true;candidate[id]=value.weight;
        }return Publish(std::move(candidate),revision);
    }
    MhmResult ReleaseKey(uint32_t part,const std::string& model,const std::string& id,uint64_t& revision){
        auto result=Refresh();if(result!=MHM_OK)return result;result=Heartbeat();if(result!=MHM_OK&&result!=MHM_STALE)return result;
        const auto* key=Find(part,model,id);if(!key)return MHM_NOT_READY;
        auto candidate=owned;candidate.erase({key->target,key->index});return Publish(std::move(candidate),revision);
    }
    MhmResult Read(uint64_t target,std::vector<MhmWeightState>& output,uint64_t& requestedRevision,uint64_t& appliedRevision)const{
        if(!api.read_weights)return MHM_NOT_READY;
        uint32_t count=0;uint64_t requested=0,applied=0;
        const auto probe=api.read_weights(target,nullptr,0,&count,&requested,&applied);
        if(probe!=MHM_BUFFER_SMALL)return probe==MHM_OK?MHM_INVALID:probe;
        std::vector<MhmWeightState> values;
        if(!count||count>limits.keys_per_target||count>values.max_size())return MHM_INVALID;
        values.resize(count);
        const auto result=api.read_weights(target,values.data(),count,&count,&requested,&applied);
        if(result!=MHM_OK)return result;if(count!=values.size())return MHM_INVALID;
        output.swap(values);requestedRevision=requested;appliedRevision=applied;return MHM_OK;
    }
};
}
