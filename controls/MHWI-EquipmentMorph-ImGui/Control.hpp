#pragma once
#include "../../runtime/EquipmentMorphControlBridge/Session.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <deque>
#include <optional>
#include "PresetManager.hpp"

namespace morphgui {
struct ControlSnapshot {
    mhmcontrol::Response data;
    bool connected=false,ready=false;
    MhmResult operation=MHM_OK,scan=MHM_NOT_READY;
    uint64_t completed=0;
    morphpreset::State presets;
};
// Only the ordinary plugin worker calls the API. Render/input threads exchange
// immutable snapshots and coalesced commands; target handles prevent replay.
class Control {
    std::unique_ptr<mhmcontrol::Session> session;
    morphpreset::Manager presets;
    ControlSnapshot current;
    std::atomic<std::shared_ptr<const ControlSnapshot>> published{std::make_shared<ControlSnapshot>()};
    using Identity=std::pair<uint64_t,uint32_t>;
    std::mutex commands;
    std::map<Identity,mhmcontrol::Request> pending;
    bool releasePending=false;
    struct Work {std::map<Identity,mhmcontrol::Request> batch;bool release=false;uint64_t ticket=0;std::optional<morphpreset::Request> preset;};
    std::deque<Work> queued;
    std::atomic_uint64_t canceledThrough=0;
    uint64_t sequence=0,nextScan=0,nextWeights=0,nextHeartbeat=0;
    void Publish(){published.store(std::make_shared<ControlSnapshot>(current));}
    void Refresh(){
        if(!session){current.ready=false;current.scan=MHM_NOT_READY;presets.Update(current.data,false);current.presets=presets.Snapshot();return;}
        mhmcontrol::Response next;current.scan=session->Snapshot(next);
        current.ready=current.scan==MHM_OK&&mhmcontrol::Valid(next,0,0);
        if(current.ready)current.data=next;
        else if(current.scan==MHM_OK)current.scan=MHM_INVALID;
        presets.Update(current.data,current.ready);current.presets=presets.Snapshot();
    }
    void RefreshWeights(){
        if(!current.ready){Refresh();return;}
        auto next=current.data;current.scan=session->Weights(next);
        if(current.scan==MHM_OK&&mhmcontrol::Valid(next,0,0)){current.data=next;current.ready=true;}
        else Refresh(); // Retired handles require fresh discovery, never replay.
    }
    void Automatic(){
        if(presets.Automatic(session.get(),current.data,current.ready))Refresh();
        else{presets.Update(current.data,current.ready);current.presets=presets.Snapshot();}
    }
public:
    std::shared_ptr<const ControlSnapshot> Snapshot()const{return published.load();}
    bool Connected()const{return current.connected;} // Worker only.
    void ConfigurePresets(const std::filesystem::path& directory){presets.Configure(directory);presets.Update(current.data,current.ready);current.presets=presets.Snapshot();Publish();}
    MhmResult Connect(const MhmApi& api,morphpreset::IdentityQuery identity=nullptr){
        constexpr auto required=MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE|MHM_CAP_PART_LIFECYCLE;
        if(api.version!=MHM_API_VERSION||api.bytes!=sizeof(api)||(api.capabilities&required)!=required
           ||!api.limits||!api.target_at||!api.key_at||!api.open_source||!api.close_source||!api.heartbeat||!api.submit||!api.read_weights){current.scan=MHM_VERSION;Publish();return MHM_VERSION;}
        MhmLimits limits{};const auto result=api.limits(&limits);
        if(result!=MHM_OK)return result;
        if(!limits.targets||limits.targets>mhmcontrol::MaxTargets||!limits.keys_per_target||limits.keys_per_target>64||limits.batch_updates<mhmcontrol::MaxKeys){current.scan=MHM_CAPACITY;Publish();return MHM_CAPACITY;}
        session=std::make_unique<mhmcontrol::Session>(api);presets.SourceExpired();presets.SetQuery(identity);current={};current.connected=true;Refresh();Publish();return current.scan;
    }
    uint64_t Post(mhmcontrol::Action action,uint64_t target=0,uint32_t key=0,float weight=0){
        std::scoped_lock lock(commands);const auto ticket=++sequence;
        if(action==mhmcontrol::Action::Release){pending.clear();queued.clear();canceledThrough=ticket;releasePending=true;}
        else{
            const Identity id{target,key};
            if(pending.size()<mhmcontrol::MaxKeys||pending.contains(id)){
                mhmcontrol::Request request;request.action=action;request.target=target;request.key=key;request.weight=weight;pending[id]=request;
            }
        }return ticket;
    }
    uint64_t PostPreset(morphpreset::Request request){
        std::scoped_lock lock(commands);if(queued.size()>=16)return 0;
        Work work;work.batch.swap(pending);work.release=std::exchange(releasePending,false);work.ticket=++sequence;work.preset=std::move(request);queued.push_back(std::move(work));return sequence;
    }
    // Lease renewal continues while the window is hidden. An expired source
    // loses old drafts; registered defaults are matched again from saved files.
    void Service(uint64_t now,bool visible=false){
        std::deque<Work> work;
        {std::scoped_lock lock(commands);work.swap(queued);if(releasePending||!pending.empty()){Work tail;tail.batch.swap(pending);tail.release=std::exchange(releasePending,false);tail.ticket=sequence;work.push_back(std::move(tail));}}
        const bool changed=!work.empty();
        bool expired=false;
        if(session&&now>=nextHeartbeat){const auto result=session->Heartbeat();nextHeartbeat=now+500;
            if(result!=MHM_OK){current.operation=result;expired=true;presets.SourceExpired();}}
        for(const auto& item:work){
            if(item.ticket<canceledThrough.load()){current.completed=item.ticket;continue;}
            if(item.release){current.operation=session?session->Release():MHM_NOT_READY;presets.Pause();}
            bool accepted=session&&!expired;
            if(!session&&!item.batch.empty())current.operation=MHM_NOT_READY;
            if(session&&!expired)for(const auto& [id,request]:item.batch){
                (void)id;Refresh();
                if(!current.presets.enabled){current.operation=MHM_NOT_READY;accepted=false;continue;}
                if(!current.ready){current.operation=current.scan;accepted=false;continue;}
                const auto end=current.data.keys.begin()+current.data.keyCount;
                const auto found=std::find_if(current.data.keys.begin(),end,[&](const auto& k){return k.target==request.target&&k.index==request.key;});
                current.operation=found==end?MHM_STALE:session->Change(request);if(current.operation!=MHM_OK)accepted=false;else presets.Edited(request.target,current.data);
            }
            if(item.preset){
                Refresh();
                if(!accepted&&(!morphpreset::Management(item.preset->action)||((item.preset->action==morphpreset::Action::EndPreview||item.preset->action==morphpreset::Action::DisableMorph)&&!item.batch.empty())))presets.Fail(item.ticket,"编辑未被接受或控制来源已失效，请重新调节后操作预设");
                else{const auto result=presets.Process(*item.preset,item.ticket,session.get(),current.data,current.ready);if(!morphpreset::Management(item.preset->action)||(result==MHM_OK&&(item.preset->action==morphpreset::Action::EnableMorph||item.preset->action==morphpreset::Action::DisableMorph)))current.operation=result;}
                if(const auto result=presets.ConsumeLeaseFailure();result!=MHM_OK){current.operation=result;expired=true;}
            }
            current.completed=item.ticket;
        }
        if(changed||expired||now>=nextScan){Refresh();Automatic();nextScan=now+250;nextWeights=now+16;Publish();}
        else if(visible&&now>=nextWeights){RefreshWeights();nextWeights=now+16;Publish();}
    }
};
}
