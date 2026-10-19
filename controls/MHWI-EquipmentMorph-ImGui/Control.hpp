#pragma once
#include "../../runtime/EquipmentMorphControlBridge/Session.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace morphgui {
struct ControlSnapshot {
    mhmcontrol::Response data;
    bool connected=false,ready=false;
    MhmResult operation=MHM_OK,scan=MHM_NOT_READY;
    uint64_t completed=0;
};
// Only the ordinary plugin worker calls the API. Render/input threads exchange
// immutable snapshots and coalesced commands; target handles prevent replay.
class Control {
    std::unique_ptr<mhmcontrol::Session> session;
    ControlSnapshot current;
    std::atomic<std::shared_ptr<const ControlSnapshot>> published{std::make_shared<ControlSnapshot>()};
    using Identity=std::pair<uint64_t,uint32_t>;
    std::mutex commands;
    std::map<Identity,mhmcontrol::Request> pending;
    bool releasePending=false;
    uint64_t sequence=0,nextScan=0,nextHeartbeat=0;
    void Publish(){published.store(std::make_shared<ControlSnapshot>(current));}
    void Refresh(){
        mhmcontrol::Response next;current.scan=session->Snapshot(next);
        current.ready=current.scan==MHM_OK&&mhmcontrol::Valid(next,0,0);
        if(current.ready)current.data=next;
        else if(current.scan==MHM_OK)current.scan=MHM_INVALID;
    }
public:
    std::shared_ptr<const ControlSnapshot> Snapshot()const{return published.load();}
    bool Connected()const{return current.connected;} // Worker only.
    MhmResult Connect(const MhmApi& api){
        constexpr auto required=MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE|MHM_CAP_PART_LIFECYCLE;
        if(api.version!=MHM_API_VERSION||api.bytes!=sizeof(api)||(api.capabilities&required)!=required
           ||!api.limits||!api.target_at||!api.key_at||!api.open_source||!api.close_source||!api.heartbeat||!api.submit||!api.read_weights){current.scan=MHM_VERSION;Publish();return MHM_VERSION;}
        MhmLimits limits{};const auto result=api.limits(&limits);
        if(result!=MHM_OK)return result;
        if(!limits.targets||limits.targets>mhmcontrol::MaxTargets||!limits.keys_per_target||limits.keys_per_target>64||limits.batch_updates<mhmcontrol::MaxKeys){current.scan=MHM_CAPACITY;Publish();return MHM_CAPACITY;}
        session=std::make_unique<mhmcontrol::Session>(api);current={};current.connected=true;Refresh();Publish();return current.scan;
    }
    uint64_t Post(mhmcontrol::Action action,uint64_t target=0,uint32_t key=0,float weight=0){
        std::scoped_lock lock(commands);const auto ticket=++sequence;
        if(action==mhmcontrol::Action::Release){pending.clear();releasePending=true;}
        else{
            const Identity id{target,key};
            if(pending.size()<mhmcontrol::MaxKeys||pending.contains(id)){
                mhmcontrol::Request request;request.action=action;request.target=target;request.key=key;request.weight=weight;pending[id]=request;
            }
        }return ticket;
    }
    // Lease renewal continues while the window is hidden. An expired source
    // loses ownership; no previous weights are automatically submitted again.
    void Service(uint64_t now){
        std::map<Identity,mhmcontrol::Request> batch;bool release=false;uint64_t completed=0;
        {std::scoped_lock lock(commands);batch.swap(pending);release=std::exchange(releasePending,false);completed=sequence;}
        const bool changed=release||!batch.empty();
        if(!session){if(changed){current.operation=MHM_NOT_READY;current.completed=completed;Publish();}return;}
        bool expired=false;
        if(now>=nextHeartbeat){const auto result=session->Heartbeat();nextHeartbeat=now+500;
            if(result!=MHM_OK){current.operation=result;expired=true;}}
        if(expired)batch.clear(); // Do not reopen an expired source for an old queued edit.
        if(release)current.operation=session->Release();
        for(const auto& [id,request]:batch){
            (void)id;Refresh();
            if(!current.ready){current.operation=current.scan;continue;}
            const auto end=current.data.keys.begin()+current.data.keyCount;
            const auto found=std::find_if(current.data.keys.begin(),end,[&](const auto& k){return k.target==request.target&&k.index==request.key;});
            current.operation=found==end?MHM_STALE:session->Change(request);
        }
        if(changed||expired||now>=nextScan){Refresh();nextScan=now+250;current.completed=completed;Publish();}
    }
};
}
