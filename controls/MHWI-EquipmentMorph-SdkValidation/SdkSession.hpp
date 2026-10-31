#pragma once
#include "Catalog.hpp"
#include "../../sdk/MHWI-EquipmentMorph-0.4.0/include/equipment_morph_key_client.hpp"
#include "../../sdk/MHWI-EquipmentMorph-0.4.0/include/mhw_morph_identity.h"

namespace mhmvalidation {
using IdentityQuery=decltype(&MhwMorphGetAssetIdentity);
struct Observation {
    MhmResult result=MHM_NOT_READY;uint64_t epoch=0,target=0,source=0,requestedRevision=0,appliedRevision=0;
    MhmAssetIdentity identity{};std::string mode;std::vector<std::string> liveIds;std::vector<MhmWeightState> weights;
};
class SdkSession {
    MhmApi api{};IdentityQuery query=nullptr;mhwi::KeyClient client{"sdk-visual-validation",200,2000};
    Catalog catalog;uint64_t epoch=0,target=0;MhmAssetIdentity identity{};std::string mode;
    std::vector<std::string> ids;bool connected=false,valid=false,forbidOpen=false;
    inline static thread_local SdkSession* submitting=nullptr;
    static bool Same(const MhmAssetIdentity& a,const MhmAssetIdentity& b){return a.handle==b.handle&&std::memcmp(a.model_sha256,b.model_sha256,65)==0&&std::memcmp(a.pack_sha256,b.pack_sha256,65)==0;}
    static bool ValidIdentity(const MhmAssetIdentity& value,uint64_t h){return value.handle==h&&value.model_sha256[64]==0&&value.pack_sha256[64]==0&&morphpreset::Hex(std::string_view(value.model_sha256,64),64)&&morphpreset::Hex(std::string_view(value.pack_sha256,64),64);}
    void Invalidate(){if(valid||client.Source())++epoch;client.Release();valid=false;target=0;identity={};mode.clear();ids.clear();}
    MhmResult Identity(uint64_t h,MhmAssetIdentity& value)const {
        if(!query)return MHM_VERSION;auto status=query(MHM_IDENTITY_VERSION,sizeof(value),h,&value);
        return status==MHM_OK&&!ValidIdentity(value,h)?MHM_INVALID:status;
    }
    MhmResult Inspect(const Catalog& c,uint64_t& h,MhmAssetIdentity& value,std::vector<std::string>& names,std::string& selected){
        auto status=client.Refresh();if(status!=MHM_OK)return status;
        for(const auto& k:client.Keys())if(k.part==c.part){
            if(k.model!=c.modelId||k.info.minimum!=0||k.info.maximum!=1)return MHM_NOT_READY;
            if(!h)h=k.target;else if(h!=k.target)return MHM_STALE;
            if(k.index!=names.size())return MHM_INVALID;names.emplace_back(k.info.id);
        }
        if(!h||names.size()!=c.expectedKeyCount)return MHM_NOT_READY;
        for(const auto& [id,label]:c.labels){(void)label;if(std::find(names.begin(),names.end(),id)==names.end())return MHM_NOT_READY;}
        status=Identity(h,value);if(status!=MHM_OK)return status;
        for(const auto& m:c.modes)if(m.modelSha==value.model_sha256&&m.packSha==value.pack_sha256){selected=m.name;break;}
        return selected.empty()?MHM_NOT_READY:MHM_OK;
    }
    static MhmResult MHM_CALL GuardOpen(const char* name,uint32_t priority,uint32_t lease,uint64_t* source){
        if(!submitting||submitting->forbidOpen)return MHM_STALE;return submitting->api.open_source(name,priority,lease,source);
    }
    static MhmResult MHM_CALL GuardSubmit(uint64_t source,const MhmUpdate* values,uint32_t count,uint64_t* revision){
        auto* self=submitting;if(!self||!self->valid||count!=self->ids.size()||!values)return MHM_STALE;
        MhmAssetIdentity current{};auto status=self->Identity(self->target,current);if(status!=MHM_OK||!Same(current,self->identity))return MHM_STALE;
        for(uint32_t i=0;i<count;++i)if(values[i].target!=self->target||values[i].key!=i)return MHM_STALE;
        return self->api.submit(source,values,count,revision);
    }
    Observation Base(MhmResult result)const {Observation o;o.result=result;o.epoch=epoch;o.target=target;o.source=client.Source();o.identity=identity;o.mode=mode;o.liveIds=ids;return o;}
public:
    uint64_t Source()const{return client.Source();}
    uint64_t Epoch()const{return epoch;}
    MhmResult Connect(const MhmApi& table,IdentityQuery extension){
        Invalidate();connected=false;if(!extension||!table.open_source||!table.submit)return MHM_VERSION;
        auto guarded=table;guarded.open_source=GuardOpen;guarded.submit=GuardSubmit;
        const auto result=client.Connect(guarded);if(result!=MHM_OK)return result;api=table;query=extension;connected=true;return MHM_OK;
    }
    MhmResult Discover(const Catalog& c){
        if(!connected)return MHM_NOT_READY;
        uint64_t h=0;MhmAssetIdentity value{};std::vector<std::string> names;std::string selected;
        const auto result=Inspect(c,h,value,names,selected);
        if(result!=MHM_OK){Invalidate();return result;}
        if(valid&&target==h&&Same(identity,value)&&ids==names)return MHM_OK;
        Invalidate();catalog=c;target=h;identity=value;ids=std::move(names);mode=std::move(selected);valid=true;++epoch;return MHM_OK;
    }
    Observation Read(){
        if(!valid)return Base(MHM_NOT_READY);
        uint64_t h=0;MhmAssetIdentity before{},after{};std::vector<std::string> names;std::string selected;
        auto result=Inspect(catalog,h,before,names,selected);
        if(result!=MHM_OK||h!=target||!Same(before,identity)||names!=ids){Invalidate();return Base(result==MHM_OK?MHM_STALE:result);}
        auto o=Base(MHM_OK);result=client.Read(target,o.weights,o.requestedRevision,o.appliedRevision);
        if(result==MHM_OK)result=Identity(target,after);
        if(result!=MHM_OK||!Same(before,after)||o.weights.size()!=ids.size()){Invalidate();return Base(result==MHM_OK?MHM_STALE:result);}
        for(const auto& w:o.weights)if(!std::isfinite(w.requested)||!std::isfinite(w.applied)||w.requested<0||w.requested>1||w.applied<0||w.applied>1){Invalidate();return Base(MHM_INVALID);}
        return o;
    }
    MhmResult Submit(const std::vector<float>& weights,uint64_t& revision){
        if(!valid)return MHM_NOT_READY;
        if(weights.size()!=ids.size()||std::any_of(weights.begin(),weights.end(),[](float w){return !std::isfinite(w)||w<0||w>1;}))return MHM_INVALID;
        const auto o=Read();if(o.result!=MHM_OK)return o.result;
        if(client.Source()){const auto alive=Heartbeat();if(alive!=MHM_OK)return alive;}
        if(submitting)return MHM_INTERNAL;
        std::vector<mhwi::NamedWeight> values;for(size_t i=0;i<ids.size();++i)values.push_back({catalog.part,catalog.modelId,ids[i],weights[i]});
        struct Scope {Scope(SdkSession* self){submitting=self;}~Scope(){submitting=nullptr;}} scope(this);
        forbidOpen=client.Source()!=0;const auto result=client.Set(values,revision);
        if(result==MHM_STALE||result==MHM_NOT_READY||result==MHM_VERSION)Invalidate();return result;
    }
    bool Confirmed(const Observation& o,const std::vector<float>& expected,uint64_t revision,uint64_t expectedEpoch)const {
        if(!valid||!revision||!Source()||o.result!=MHM_OK||o.epoch!=expectedEpoch||o.epoch!=epoch||o.target!=target||o.source!=Source()||!Same(o.identity,identity)||o.liveIds!=ids||o.weights.size()!=expected.size()||o.weights.size()!=ids.size()||o.requestedRevision<revision||o.appliedRevision<revision)return false;
        for(size_t i=0;i<expected.size();++i)if(o.weights[i].requested!=expected[i]||o.weights[i].applied!=expected[i]||o.weights[i].source!=Source())return false;
        return true;
    }
    MhmResult Heartbeat(){const auto result=client.Heartbeat();if(result!=MHM_OK)Invalidate();return result;}
    MhmResult Release(){return client.Release();}
};
}
