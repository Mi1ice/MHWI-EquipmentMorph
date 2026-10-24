#pragma once
#include "NativeExpressionApi.hpp"
#include "Preset.hpp"

namespace morphexpression {
enum class Action {Enable,Map,Clear};
struct Request {Action action=Action::Enable;bool enabled=false;uint64_t target=0;uint32_t key=0;};
struct Snapshot {
    State state;
    bool connected=false,busy=false;
    MhmResult result=MHM_NOT_READY;
};
class Client {
    Read read=nullptr;Configure configure=nullptr;
    decltype(&MhwMorphGetAssetIdentity) identity=nullptr;
    Snapshot current;
    uint64_t nextPoll=0,waiting=0;
    bool hasOperation=false;
public:
    void SetIdentity(decltype(identity) query){identity=query;}
    void SetApi(Read get,Configure post){read=get;configure=post;nextPoll=waiting=0;hasOperation=false;current={};}
    const Snapshot& Get()const{return current;}
    void Service(uint64_t now,bool force=false){
        if(!force&&now<nextPoll)return;nextPoll=now+100;
        if(!read||!configure){
            HMODULE module=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"MHWEquipmentMorphNativeBlink.dll",&module))return;
            auto address=GetProcAddress(module,"MhwNativeBlinkRead");std::memcpy(&read,&address,sizeof(read));
            address=GetProcAddress(module,"MhwNativeBlinkConfigure");std::memcpy(&configure,&address,sizeof(configure));
            if(!read||!configure){current.result=MHM_VERSION;return;}
        }
        State state;const auto result=read(Version,sizeof(state),&state);
        if(result!=MHM_OK||state.version!=Version||state.bytes!=sizeof(state)||!Valid(state.config)||
            state.ready>1||static_cast<uint32_t>(state.status)>static_cast<uint32_t>(Status::ApiError)||
            !std::isfinite(state.closure)||!std::isfinite(state.applied)||!std::memchr(state.detail,0,sizeof(state.detail))){
            current.connected=false;current.result=result==MHM_OK?MHM_VERSION:result;return;
        }
        current.connected=true;current.state=state;
        if(waiting&&state.completed>=waiting){waiting=0;current.result=state.configurationResult;}
        else if(!waiting&&!hasOperation)current.result=state.configurationResult;
        current.busy=waiting!=0;
        if(state.tick&&now>state.tick&&now-state.tick>1500){current.connected=false;current.result=MHM_NOT_READY;}
    }
    MhmResult Change(const Request& request,const mhmcontrol::Response& data,bool ready){
        hasOperation=true;
        if(!current.connected||!current.state.ready)return current.result=MHM_NOT_READY;
        if(waiting)return current.result=MHM_CAPACITY;
        auto config=current.state.config;
        if(request.action==Action::Enable)config.enabled=request.enabled?1u:0u;
        else if(request.action==Action::Clear){config={};config.enabled=current.state.config.enabled;}
        else if(request.action==Action::Map){
            if(!ready||!identity)return current.result=MHM_NOT_READY;
            const mhmcontrol::Target* target=nullptr;
            for(uint32_t i=0;i<data.targetCount;++i)if(data.targets[i].info.handle==request.target)target=&data.targets[i];
            if(!target||request.key>=target->count)return current.result=MHM_STALE;
            const auto& key=data.keys[target->first+request.key].info;
            if(!std::isfinite(key.minimum)||!std::isfinite(key.maximum)||key.minimum>0||key.maximum<1)return current.result=MHM_INVALID;
            MhmAssetIdentity asset{};
            if(identity(MHM_IDENTITY_VERSION,sizeof(asset),request.target,&asset)!=MHM_OK||!morphpreset::ValidIdentity(asset,request.target))return current.result=MHM_STALE;
            config={};config.enabled=current.state.config.enabled;config.mapped=1;config.part=target->info.part;
            strcpy_s(config.model,target->info.model_id);strcpy_s(config.modelSha,asset.model_sha256);strcpy_s(config.packSha,asset.pack_sha256);strcpy_s(config.key,key.id);
        }else return current.result=MHM_INVALID;
        if(!Valid(config))return current.result=MHM_INVALID;
        uint64_t ticket=0;current.result=configure(Version,sizeof(config),&config,&ticket);
        if(current.result==MHM_OK){if(!ticket)return current.result=MHM_INVALID;waiting=ticket;current.busy=true;}
        return current.result;
    }
};
}
