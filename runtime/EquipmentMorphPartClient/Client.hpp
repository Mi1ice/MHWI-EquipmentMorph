#pragma once
#include "../EquipmentMorphPartCore/include/mhw_morph_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace morphpartclient {
struct TargetSpec {std::string model;uint32_t part=0;std::vector<std::string> ids;};
struct PartValues {uint32_t part=0;std::vector<float> weights;};
struct PartState {uint32_t part=0;uint64_t target=0,appliedRevision=0;std::vector<MhmWeightState> weights;};
// Source contribution is still an atomic full replacement. Cache ONLY weights
// explicitly submitted for current handles. A new handle inherits no old entry.
class Client {
    struct Member {MhmTargetInfo info{};std::vector<uint32_t> keys;};
    MhmApi api{};MhmLimits limits{};uint64_t source=0;
    std::vector<TargetSpec> specs;std::map<uint32_t,Member> members;
    std::map<uint64_t,std::vector<float>> owned;
    static bool Identifier(const std::string& value,size_t bound) {
        return !value.empty()&&value.size()<bound&&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<=126;});
    }
    MhmResult Enumerate(std::vector<MhmTargetInfo>& output) const {
        std::vector<MhmTargetInfo> result;std::array<bool,5> seen{};
        for(uint32_t i=0;i<limits.targets;i++) {
            MhmTargetInfo info{};const auto status=api.target_at(i,&info);
            if(status==MHM_NOT_READY)break;if(status!=MHM_OK)return status;
            if(!info.handle||info.part>MHM_LEGS||seen[info.part]||!info.key_count
                    ||info.key_count>limits.keys_per_target||!std::memchr(info.model_id,0,sizeof(info.model_id)))return MHM_INVALID;
            seen[info.part]=true;result.push_back(info);
        }
        output.swap(result);return MHM_OK;
    }
public:
    Client()=default;Client(const Client&)=delete;Client& operator=(const Client&)=delete;
    ~Client(){Release();}
    uint64_t Source()const{return source;}
    uint64_t Target(uint32_t part)const {const auto it=members.find(part);return it==members.end()?0:it->second.info.handle;}
    MhmResult Release() {
        const auto previous=source;source=0;owned.clear();
        return previous?api.close_source(previous):MHM_OK;
    }
    MhmResult Connect(const MhmApi& table,const std::vector<TargetSpec>& specifications) {
        constexpr auto capabilities=MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE|MHM_CAP_PART_LIFECYCLE;
        if(table.version!=MHM_API_VERSION||table.bytes!=sizeof(MhmApi)||(table.capabilities&capabilities)!=capabilities
                ||!table.limits||!table.target_at||!table.key_at||!table.read_weights||!table.open_source
                ||!table.close_source||!table.submit||!table.heartbeat)return MHM_VERSION;
        MhmLimits bounds{};const auto status=table.limits(&bounds);if(status!=MHM_OK)return status;
        if(!bounds.targets||bounds.targets>5||!bounds.keys_per_target||bounds.keys_per_target>64
                ||!bounds.batch_updates||bounds.batch_updates>320)return MHM_CAPACITY;
        if(specifications.empty()||specifications.size()>bounds.targets)return MHM_INVALID;
        std::array<bool,5> seen{};size_t count=0;
        for(const auto& spec:specifications) {
            if(spec.part>MHM_LEGS||seen[spec.part]||!Identifier(spec.model,128)
                    ||spec.ids.empty()||spec.ids.size()>bounds.keys_per_target)return MHM_INVALID;
            seen[spec.part]=true;count+=spec.ids.size();
            for(size_t i=0;i<spec.ids.size();i++) {
                if(!Identifier(spec.ids[i],64))return MHM_INVALID;
                for(size_t j=0;j<i;j++)if(spec.ids[i]==spec.ids[j])return MHM_INVALID;
            }
        }
        if(count>bounds.batch_updates)return MHM_CAPACITY;
        Release();members.clear();api=table;limits=bounds;specs=specifications;return MHM_OK;
    }
    MhmResult Refresh() {
        if(!api.target_at)return MHM_NOT_READY;
        std::vector<MhmTargetInfo> before,after;auto status=Enumerate(before);if(status!=MHM_OK)return status;
        std::map<uint32_t,Member> candidate;
        for(const auto& spec:specs)for(const auto& info:before) {
            if(info.part!=spec.part||spec.model!=info.model_id)continue;
            Member member;member.info=info;
            for(const auto& id:spec.ids) {
                bool found=false;
                for(uint32_t k=0;k<info.key_count;k++) {
                    MhmKeyInfo key{};status=api.key_at(info.handle,k,&key);
                    if(status==MHM_STALE)return MHM_NOT_READY;if(status!=MHM_OK)return status;
                    if(!std::memchr(key.id,0,sizeof(key.id))||key.minimum!=0||key.maximum!=1)return MHM_INVALID;
                    if(id==key.id){member.keys.push_back(k);found=true;break;}
                }
                if(!found)return MHM_NOT_READY;
            }
            candidate.emplace(spec.part,std::move(member));
        }
        status=Enumerate(after);if(status!=MHM_OK)return status;
        if(before.size()!=after.size())return MHM_NOT_READY;
        for(size_t i=0;i<before.size();i++)if(before[i].handle!=after[i].handle||before[i].part!=after[i].part
                ||before[i].key_count!=after[i].key_count||std::strcmp(before[i].model_id,after[i].model_id)!=0)return MHM_NOT_READY;
        auto retained=owned;
        std::erase_if(retained,[&](const auto& entry){return std::none_of(candidate.begin(),candidate.end(),[&](const auto& m){return m.second.info.handle==entry.first;});});
        members.swap(candidate);owned.swap(retained);return MHM_OK;
    }
    MhmResult Heartbeat() {
        if(!source)return MHM_OK;const auto status=api.heartbeat(source);
        if(status==MHM_STALE){source=0;owned.clear();}return status;
    }
    MhmResult Set(const std::vector<PartValues>& changes,uint64_t& revision) {
        if(changes.empty())return MHM_INVALID;
        auto candidate=owned;std::array<bool,5> seen{};
        for(const auto& change:changes) {
            if(change.part>MHM_LEGS||seen[change.part])return MHM_INVALID;seen[change.part]=true;
            const auto it=members.find(change.part);if(it==members.end())return MHM_NOT_READY;
            if(change.weights.size()!=it->second.keys.size())return MHM_INVALID;
            for(float weight:change.weights)if(!std::isfinite(weight)||weight<0||weight>1)return MHM_INVALID;
            candidate[it->second.info.handle]=change.weights;
        }
        std::vector<MhmUpdate> batch;
        for(const auto& [part,m]:members) {
            (void)part;const auto values=candidate.find(m.info.handle);if(values==candidate.end())continue;
            for(size_t i=0;i<m.keys.size();i++)batch.push_back({m.info.handle,m.keys[i],values->second[i]});
        }
        if(batch.size()>limits.batch_updates)return MHM_CAPACITY;
        if(!source){const auto status=api.open_source("public-part-client",200,2000,&source);if(status!=MHM_OK)return status;}
        const auto status=api.submit(source,batch.data(),static_cast<uint32_t>(batch.size()),&revision);
        if(status==MHM_OK)owned.swap(candidate);
        else if(status==MHM_STALE)Heartbeat(); // Never release healthy body for a stale arm handle.
        return status;
    }
    MhmResult Read(std::vector<PartState>& output,uint64_t& revision) const {
        if(members.empty())return MHM_NOT_READY;
        std::vector<PartState> result;uint64_t requested=0;
        for(unsigned pass=0;pass<2;pass++) {
            size_t index=0;
            for(const auto& [part,m]:members) {
                std::vector<MhmWeightState> values(m.info.key_count);uint32_t count=0;uint64_t req=0,app=0;
                const auto status=api.read_weights(m.info.handle,values.data(),m.info.key_count,&count,&req,&app);
                if(status!=MHM_OK)return status;if(count!=m.info.key_count)return MHM_INVALID;
                if(!pass&&!index)requested=req;else if(req!=requested)return MHM_NOT_READY;
                if(!pass){PartState state{part,m.info.handle,app,{}};for(auto k:m.keys)state.weights.push_back(values[k]);result.push_back(std::move(state));}
                else if(result[index].appliedRevision!=app)return MHM_NOT_READY;
                index++;
            }
        }
        output.swap(result);revision=requested;return MHM_OK;
    }
};
}
