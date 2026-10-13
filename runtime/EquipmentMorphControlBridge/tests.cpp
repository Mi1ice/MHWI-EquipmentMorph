#define NOMINMAX
#include "Session.hpp"
#include "../EquipmentMorphPerKeyCore/api.cpp"
#include <cstdio>
#include <stdexcept>
#include <limits>
int main(){try {
    unsigned checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);checks++;};
    MhmApi api{};check(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK,"public API");
    auto attach=[](uint32_t part,const char* name){mhm::Asset asset;asset.part=part;asset.model=name;asset.basis.resize(64);
        for(const char* id:{"Example_A","Example_B"}){mhm::Key key;strcpy_s(key.info.id,id);key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);}
        uint64_t handle=0;if(Core().Attach(std::move(asset),handle)!=MHM_OK)throw std::runtime_error("synthetic attach");return handle;};
    const auto body=attach(MHM_BODY,"synthetic/body");auto arm=attach(MHM_ARMS,"synthetic/arms");
    uint64_t other=0,revision=0;api.open_source("other",100,60000,&other);
    const MhmUpdate underlying[]{{body,0,.2f},{body,1,.8f},{arm,0,.3f}};api.submit(other,underlying,3,&revision);
    mhmcontrol::Response snapshot;using mhmcontrol::Action;
    auto request=[](Action action,uint64_t target,uint32_t key,float weight){mhmcontrol::Request r;r.sequence=1;r.action=action;r.target=target;r.key=key;r.weight=weight;return r;};
    {
        mhmcontrol::Session session(api);
        check(session.Snapshot(snapshot)==MHM_OK&&snapshot.targetCount==2&&snapshot.keyCount==4,"generic discovery");
        check(session.Change(request(Action::Set,body,0,.6f))==MHM_OK&&session.Snapshot(snapshot)==MHM_OK&&snapshot.keys[0].owned&&snapshot.keys[0].state.requested==.6f&&snapshot.keys[1].state.requested==.8f,"per-key takeover preserves unowned key");
        check(session.Change(request(Action::Set,arm,0,.7f))==MHM_OK&&session.Snapshot(snapshot)==MHM_OK&&snapshot.keys[0].state.requested==.6f&&snapshot.keys[2].state.requested==.7f,"cross-part ownership retained");
        check(session.Change(request(Action::Drop,body,0,0))==MHM_OK&&session.Snapshot(snapshot)==MHM_OK&&!snapshot.keys[0].owned&&snapshot.keys[0].state.requested==.2f&&snapshot.keys[2].owned,"one-key release reveals underlying source");
        check(session.Change(request(Action::Set,body,0,0))==MHM_OK&&session.Snapshot(snapshot)==MHM_OK&&snapshot.keys[0].state.requested==0&&snapshot.keys[0].owned,"explicit zero retains ownership");
        check(session.Change(request(Action::Set,body,0,std::numeric_limits<float>::quiet_NaN()))==MHM_INVALID&&session.Change(request(Action::Set,body,0,1.1f))==MHM_INVALID&&session.Snapshot(snapshot)==MHM_OK&&snapshot.keys[0].contribution==0,"invalid values leave contribution intact");
        Core().Detach(arm);arm=attach(MHM_ARMS,"synthetic/new_arms");
        check(session.Snapshot(snapshot)==MHM_OK&&snapshot.keys[0].owned&&!snapshot.keys[2].owned&&snapshot.keys[2].state.requested==0,"replacement has no replay, healthy part remains owned");
        check(session.Release()==MHM_OK&&session.Snapshot(snapshot)==MHM_OK&&!session.Source()&&snapshot.keys[0].state.requested==.2f,"release only own source");
        session.Change(request(Action::Set,body,0,.5f));Core().Tick(2001);
        check(session.Heartbeat()==MHM_STALE&&session.Snapshot(snapshot)==MHM_OK&&!snapshot.keys[0].owned&&snapshot.keys[0].state.requested==.2f,"expired source has no automatic replay");
        session.Change(request(Action::Set,body,0,.9f));
    }
    MhmWeightState weights[2]{};uint32_t count=0;uint64_t applied=0;api.read_weights(body,weights,2,&count,&revision,&applied);
    check(weights[0].requested==.2f,"session destruction releases own contribution");
    auto valid=request(Action::Set,body,0,.5f);
    check(mhmcontrol::Valid(valid,0)&&!mhmcontrol::Valid(valid,1),"sequence anti-replay");valid.bytes=0;
    check(!mhmcontrol::Valid(valid,0),"malformed packet rejected");
    snapshot.pid=123;snapshot.sequence=4;check(mhmcontrol::Valid(snapshot,4,123)&&!mhmcontrol::Valid(snapshot,4,124),"bounded response and server identity");snapshot.keyCount=321;
    check(!mhmcontrol::Valid(snapshot,4,123),"oversized response rejected");
    std::printf("PASS: %u control-session checks; synthetic assets only\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
