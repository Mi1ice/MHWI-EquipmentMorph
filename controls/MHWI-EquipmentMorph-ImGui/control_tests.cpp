#define NOMINMAX
#include "Control.hpp"
#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include <chrono>

int main(){try{
    unsigned checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);checks++;};
    MhmApi api{};MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api);
    auto attach=[](uint32_t part,const char* name,bool exclusive=false){
        mhm::Asset asset;asset.part=part;asset.model=name;asset.basis.resize(64);
        for(const char* id:{"Example_A","Example_B"}){mhm::Key key;strcpy_s(key.info.id,id);key.info.maximum=1;key.info.exclusive_group=exclusive?1u:0u;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);}
        uint64_t handle=0;if(Core().Attach(std::move(asset),handle)!=MHM_OK)throw std::runtime_error("synthetic attach");return handle;
    };
    const auto body=attach(MHM_BODY,"synthetic/body");auto arm=attach(MHM_ARMS,"synthetic/arms",true);
    uint64_t underlying=0,revision=0;api.open_source("other-plugin",100,60000,&underlying);
    const MhmUpdate base[]{{body,0,.2f},{body,1,.8f},{arm,0,.3f}};api.submit(underlying,base,3,&revision);
    morphgui::Control control;auto invalid=api;invalid.submit=nullptr;
    check(control.Connect(invalid)==MHM_VERSION&&!control.Snapshot()->connected,"incomplete API rejected");
    check(control.Connect(api)==MHM_OK&&control.Snapshot()->data.targetCount==2&&!control.Snapshot()->data.source,"enumeration never takes ownership");
    using mhmcontrol::Action;
    auto key=[&](uint64_t target,uint32_t index){const auto snapshot=control.Snapshot();for(uint32_t i=0;i<snapshot->data.keyCount;i++)if(snapshot->data.keys[i].target==target&&snapshot->data.keys[i].index==index)return snapshot->data.keys[i];throw std::runtime_error("key missing");};
    control.Post(Action::Set,body,0,.4f);const auto last=control.Post(Action::Set,body,0,.7f);control.Service(100);
    check(control.Snapshot()->completed==last&&key(body,0).owned&&key(body,0).contribution==.7f,"coalesced slider command acknowledged");
    check(key(body,1).state.requested==.8f&&!key(body,1).owned,"unmodified key preserves another source");
    check(key(body,0).state.requested==.7f&&key(body,0).state.applied==0,"applied weight is not replaced by the edit value");
    uint64_t frame=0;Core().Capture(frame,revision);Core().Acknowledge(frame);Core().ReleaseFrame(frame);control.Service(400);
    check(key(body,0).state.applied==.7f,"published weight read from actual API snapshot");
    control.Post(Action::Set,arm,0,.6f);control.Service(450);
    check(key(body,0).contribution==.7f&&key(arm,0).contribution==.6f,"equipment selection retains other part contribution");
    control.Post(Action::Set,arm,1,.5f);control.Service(460);
    check(control.Snapshot()->operation==MHM_CONFLICT&&key(arm,0).contribution==.6f&&!key(arm,1).owned,"exclusive conflict preserves prior contribution");
    control.Post(Action::Set,body,0,std::numeric_limits<float>::quiet_NaN());control.Service(470);
    check(control.Snapshot()->operation==MHM_INVALID&&key(body,0).contribution==.7f,"non-finite input rejected without changing weights");
    control.Post(Action::Set,body,0,1.1f);control.Service(480);
    check(control.Snapshot()->operation==MHM_INVALID&&key(body,0).contribution==.7f,"range error preserves weights");
    control.Post(Action::Drop,body,0);control.Service(490);
    check(!key(body,0).owned&&key(body,0).state.requested==.2f&&key(arm,0).owned,"one-key release preserves other equipment and underlying source");
    control.Post(Action::Set,body,0,0);control.Service(500);
    check(key(body,0).owned&&key(body,0).state.requested==0,"explicit zero remains owned");
    Core().Tick(1000);control.Service(1000);const auto source=control.Snapshot()->data.source;
    Core().Tick(2001);control.Service(2001);
    check(control.Snapshot()->data.source==source&&key(body,0).owned,"hidden service keeps lease alive without UI polling");
    control.Post(Action::Set,body,0,.55f);control.Service(2020);
    const auto oldArm=arm;control.Post(Action::Set,oldArm,0,.9f);Core().Detach(oldArm);arm=attach(MHM_ARMS,"synthetic/arms",true);control.Service(2030);
    check(control.Snapshot()->operation==MHM_STALE&&!key(arm,0).owned&&key(arm,0).state.requested==0,"stale queued edit never replays onto same-model replacement");
    check(key(body,0).owned&&key(body,0).contribution==.55f,"replacement retains healthy part ownership");
    control.Post(Action::Set,body,0,.9f);const auto released=control.Post(Action::Release);control.Service(2040);
    check(control.Snapshot()->completed==released&&!control.Snapshot()->data.source&&key(body,0).state.requested==.2f,"close/release cancels queued changes and releases only own source");
    control.Service(2300);check(!key(body,0).owned,"closed source is not replayed by later refresh");
    control.Post(Action::Set,body,0,.45f);control.Service(2400);control.Post(Action::Set,body,0,.99f);Core().Tick(5000);control.Service(5000);
    check(control.Snapshot()->operation==MHM_STALE&&!control.Snapshot()->data.source&&key(body,0).state.requested==.2f,"expired source clears ownership and discards old queued edits without replay");
    control.Post(Action::Set,body,0,.33f);control.Service(5010);const auto previous=control.Snapshot();
    control.Post(Action::Set,body,0,.66f);control.Service(5020);
    check(previous->data.keys[0].contribution==.33f&&key(body,0).contribution==.66f,"published snapshots remain immutable while worker updates");
    uint64_t higher=0;api.open_source("higher-priority-plugin",250,60000,&higher);const MhmUpdate mask{body,0,.1f};api.submit(higher,&mask,1,&revision);control.Service(5300);
    check(key(body,0).owned&&key(body,0).contribution==.66f&&key(body,0).state.source==higher,"higher priority source remains distinct from panel contribution");
    control.Post(Action::Release);control.Service(5310);
    check(key(body,0).state.source==higher&&key(body,0).state.requested==.1f,"release leaves a higher priority source intact");api.close_source(higher);
    std::atomic_bool stop=false;std::thread worker([&]{uint64_t tick=5400;while(!stop){control.Service(tick++);std::this_thread::yield();}});
    for(unsigned i=0;i<1000;i++){control.Post(Action::Set,body,0,float(i%100)/100.f);const auto read=control.Snapshot();(void)read;}
    const float precise=.123456789f;const auto finalTicket=control.Post(Action::Set,body,0,precise);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(control.Snapshot()->completed<finalTicket&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    stop=true;worker.join();
    check(control.Snapshot()->completed>=finalTicket&&key(body,0).contribution==precise,"concurrent command posting and immutable snapshot reads keep latest precise weight");
    control.Post(Action::Release);control.Service(1000000);
    std::printf("PASS: %u ImGui control checks; no windows, input automation or game process\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
