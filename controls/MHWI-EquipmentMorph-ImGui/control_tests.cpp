#define NOMINMAX
#include "Control.hpp"
#include "KeyFilter.hpp"
#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace {
MhmApi originalApi{};
std::atomic_uint targetReads=0,keyReads=0,weightReads=0;
uint64_t failWeightTarget=0;
MhmResult MHM_CALL CountTarget(uint32_t index,MhmTargetInfo* info){++targetReads;return originalApi.target_at(index,info);}
MhmResult MHM_CALL CountKey(uint64_t target,uint32_t index,MhmKeyInfo* info){++keyReads;return originalApi.key_at(target,index,info);}
MhmResult MHM_CALL CountWeights(uint64_t target,MhmWeightState* values,uint32_t capacity,uint32_t* count,uint64_t* requested,uint64_t* applied){
    ++weightReads;if(target==failWeightTarget)return MHM_NOT_READY;
    return originalApi.read_weights(target,values,capacity,count,requested,applied);
}
struct ExpressionMock {
    inline static morphexpression::State state;
    inline static morphexpression::Config posted;
    inline static MhmResult result=MHM_OK,identityResult=MHM_OK;
    inline static uint64_t ticket=0;
    inline static unsigned reads=0,posts=0;
    static MhmResult MHM_CALL Read(uint32_t version,uint32_t bytes,morphexpression::State* out){
        ++reads;if(version!=1||bytes!=sizeof(*out))return MHM_VERSION;*out=state;return MHM_OK;
    }
    static MhmResult MHM_CALL Post(uint32_t version,uint32_t bytes,const morphexpression::Config* value,uint64_t* out){
        ++posts;if(version!=1||bytes!=sizeof(*value))return MHM_VERSION;if(result!=MHM_OK)return result;posted=*value;*out=++ticket;return MHM_OK;
    }
    static MhmResult MHM_CALL Identity(uint32_t,uint32_t,uint64_t handle,MhmAssetIdentity* out){
        *out={};out->handle=handle;std::memset(out->model_sha256,'a',64);std::memset(out->pack_sha256,'b',64);return identityResult;
    }
    static void Complete(MhmResult value=MHM_OK){state.completed=ticket;state.configurationResult=value;if(value==MHM_OK)state.config=posted;}
};
}
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
    {
        originalApi=api;auto counted=api;counted.target_at=CountTarget;counted.key_at=CountKey;counted.read_weights=CountWeights;
        morphgui::Control polling;check(polling.Connect(counted)==MHM_OK,"weight polling connects without owning targets");
        polling.Post(Action::Set,body,0,1);polling.Service(1,true);
        const auto before=polling.Snapshot();Core().Capture(frame,revision);Core().Acknowledge(frame);Core().ReleaseFrame(frame);
        const auto targetsBefore=targetReads.load(),keysBefore=keyReads.load(),weightsBefore=weightReads.load();
        polling.Service(16,true);check(polling.Snapshot()==before&&weightReads==weightsBefore,"visible weight reads wait for sixteen millisecond interval");
        polling.Service(17,true);
        check(polling.Snapshot()->data.keys[0].state.applied==1&&polling.Snapshot()->data.keys[0].owned,"actual acknowledgement appears at next visible read without another edit");
        check(targetReads==targetsBefore&&keyReads==keysBefore&&weightReads==weightsBefore+2,"fast weight read avoids target and key enumeration");
        const auto visible=polling.Snapshot();polling.Service(33,false);
        check(polling.Snapshot()==visible,"hidden panel skips fast weight reads");
        const auto leg=attach(MHM_LEGS,"synthetic/legs");polling.Service(49,true);
        check(polling.Snapshot()->data.targetCount==2,"weight read reuses metadata between discovery scans");
        polling.Service(251,true);
        check(polling.Snapshot()->data.targetCount==3,"visible weight reads do not postpone equipment discovery");
        morphpanel::Session session;session.Connect(counted);morphpanel::Snapshot readback;
        check(session.Snapshot(readback)==MHM_OK,"read failure fixture discovers complete state");
        readback.keys[0].state.applied=.123f;const auto saved=readback;failWeightTarget=arm;
        check(session.Weights(readback)==MHM_NOT_READY&&readback.revision==saved.revision&&readback.keys[0].state.applied==saved.keys[0].state.applied,
            "failed weight read leaves caller snapshot unchanged");failWeightTarget=0;
        failWeightTarget=body;morphgui::Control unavailable;unavailable.Connect(counted);unavailable.Service(0,true);unavailable.Service(16,true);
        check(!unavailable.Snapshot()->ready&&unavailable.Snapshot()->scan==MHM_NOT_READY,"fast reads never turn a failed initial discovery into an empty ready snapshot");failWeightTarget=0;
        Core().Detach(arm);arm=attach(MHM_ARMS,"synthetic/replaced_arms",true);polling.Service(267,true);
        const auto replacement=polling.Snapshot();bool healthy=false,fresh=false;
        for(uint32_t i=0;i<replacement->data.keyCount;i++){
            const auto& entry=replacement->data.keys[i];
            if(entry.target==body&&entry.index==0)healthy=entry.owned&&entry.contribution==1;
            if(entry.target==arm&&entry.index==0)fresh=!entry.owned&&entry.state.requested==0;
        }
        check(replacement->ready&&healthy&&fresh,"retired handle during fast read triggers discovery without replaying weights");
        polling.Post(Action::Release);polling.Service(268,true);Core().Detach(leg);
    }
    {
        morphgui::KeyFilter filter;filter.Select(body);
        check(filter.Matches("Example_A",false,0),"default filter includes unowned zero keys");
        strcpy_s(filter.search,"  EXAMPLE_b \t");
        check(filter.Matches("Example_B",true,.5f)&&!filter.Matches("Example_A",true,.5f),"search trims whitespace and folds ASCII case");
        strcpy_s(filter.search,"胸部");check(filter.Matches("测试胸部形态",true,0),"search matches literal UTF-8 names");
        filter.Clear();filter.mode=1;
        check(filter.Matches("zero",true,0)&&!filter.Matches("other",false,1),"owned filter includes explicit zero and excludes other sources");
        filter.mode=2;check(filter.Matches("other",false,1)&&!filter.Matches("own",true,0),"unowned filter is panel-specific");
        filter.mode=3;const float tiny=std::numeric_limits<float>::denorm_min();
        check(filter.Matches("tiny",true,tiny)&&filter.Matches("negative",true,-.1f)&&!filter.Matches("zero",true,-0.f),"nonzero filter retains exact float values including tiny and negative contributions");
        check(!filter.Matches("external",false,1),"nonzero filter does not include an external source");
        strcpy_s(filter.search,"Example");check(!filter.Matches("different",true,1),"name and ownership filters intersect");
        check(filter.Visible("different",true,0,true,4,4),"active row remains visible after reaching zero or changing search");
        check(filter.Visible("different",false,0,false,5,4),"pending release remains visible until worker acknowledgement");
        check(!filter.Visible("different",false,0,false,5,5),"finished filtered row disappears after acknowledgement");
        filter.Select(body);check(filter.mode==3&&filter.search[0]!=0,"same target preserves filters across panel hide and reopen");
        filter.Select(arm);check(filter.mode==0&&!filter.search[0],"target replacement clears search and state filters");
        morphpanel::Session session;session.Connect(api);mhmcontrol::Request set;set.action=Action::Set;set.target=body;set.key=0;set.weight=0;
        check(session.ChangeMany({&set,1})==MHM_OK,"filter fixture owns an explicit zero");set.key=1;set.weight=tiny;
        check(session.ChangeMany({&set,1})==MHM_OK,"filter fixture owns a precise tiny weight");morphpanel::Snapshot data;
        check(session.Snapshot(data)==MHM_OK,"filter fixture reads full ownership");
        filter.mode=3;std::vector<const mhmcontrol::Key*> visible;
        for(uint32_t i=0;i<data.keyCount;i++){const auto& k=data.keys[i];if(filter.Matches(k.info.id,k.owned!=0,k.contribution))visible.push_back(&k);}
        check(visible.size()==1&&visible[0]->target==body&&visible[0]->index==1,"filtered first row preserves original target and key index");
        set.target=visible[0]->target;set.key=visible[0]->index;set.weight=.37f;check(session.ChangeMany({&set,1})==MHM_OK,"filtered key edit submits its original identity");
        session.Snapshot(data);unsigned owned=0;bool zero=false,edited=false;
        for(uint32_t i=0;i<data.keyCount;i++){const auto& k=data.keys[i];if(k.owned){owned++;zero=zero||(k.target==body&&k.index==0&&k.contribution==0);edited=edited||(k.target==body&&k.index==1&&k.contribution==.37f);}}
        check(owned==2&&zero&&edited,"hidden owned zero remains in full snapshot for preset capture and is not changed by visible edit");session.Release();
        morphgui::FeedbackTime feedback;
        check(feedback.Show(1,true,10)&&feedback.Show(1,true,12.9)&&!feedback.Show(1,true,13),"success feedback expires after three seconds");
        check(feedback.Show(2,true,20)&&feedback.Show(2,false,200),"new completion restarts success feedback and errors remain visible");
    }
    {
        using namespace morphexpression;
        using ExprAction=morphexpression::Action;
        Client client;client.SetApi(ExpressionMock::Read,ExpressionMock::Post);client.SetIdentity(ExpressionMock::Identity);
        ExpressionMock::state={};ExpressionMock::state.ready=1;client.Service(100);
        check(client.Get().connected&&!client.Get().busy,"optional expression worker discovered independently of morph control");
        client.Service(150);check(ExpressionMock::reads==1,"expression polling bounded outside render thread");
        morphpanel::Snapshot data;data.targetCount=1;data.keyCount=2;data.targets.resize(1);data.keys.resize(2);
        data.targets[0].info.handle=4321;data.targets[0].info.part=MHM_BODY;strcpy_s(data.targets[0].info.model_id,"synthetic/face");data.targets[0].first=0;data.targets[0].count=2;
        for(uint32_t i=0;i<2;++i){data.keys[i].target=4321;data.keys[i].index=i;strcpy_s(data.keys[i].info.id,i?"eyes.closed":"unrelated");data.keys[i].info.maximum=i?1.f:.5f;}
        check(client.Change({ExprAction::Map,false,4321,1},data,true)==MHM_OK&&client.Get().busy,"explicit expression mapping queued with acknowledgment");
        check(!std::strcmp(ExpressionMock::posted.key,"eyes.closed")&&Sha(ExpressionMock::posted.modelSha)&&Sha(ExpressionMock::posted.packSha),"mapping captures key ID and exact asset identity");
        check(client.Change({ExprAction::Enable,true},data,true)==MHM_CAPACITY,"pending expression settings cannot be overwritten");
        client.Service(200);check(client.Get().busy&&!client.Get().state.config.mapped,"state remains saved configuration until worker acknowledges");
        ExpressionMock::Complete();client.Service(300);check(!client.Get().busy&&client.Get().state.config.mapped,"expression mapping published after worker completion");
        check(client.Change({ExprAction::Map,false,4320,1},data,true)==MHM_STALE,"retired target handle never replays expression mapping");
        client.Service(400);check(client.Get().result==MHM_STALE,"local expression errors persist until another operation");
        const auto posts=ExpressionMock::posts;check(client.Change({ExprAction::Map,false,4321,0},data,true)==MHM_INVALID&&ExpressionMock::posts==posts,"incompatible weight range rejected before expression request");
        ExpressionMock::identityResult=MHM_STALE;check(client.Change({ExprAction::Map,false,4321,1},data,true)==MHM_STALE,"retired asset identity blocks mapping");ExpressionMock::identityResult=MHM_OK;
        check(client.Change({ExprAction::Enable,true},data,false)==MHM_OK,"independent expression switch works without current equipment");ExpressionMock::Complete();client.Service(500);
        check(client.Get().state.config.enabled==1,"expression enable acknowledgment retained");
        check(client.Change({ExprAction::Clear},data,false)==MHM_OK,"mapping can be cleared while equipment absent");ExpressionMock::Complete();client.Service(600);
        check(!client.Get().state.config.mapped&&client.Get().state.config.enabled,"clearing expression mapping preserves independent enabled switch");
        check(client.Change({ExprAction::Enable,false},data,false)==MHM_OK,"disable request queued");ExpressionMock::Complete(MHM_NOT_READY);client.Service(700);
        check(client.Get().result==MHM_NOT_READY&&!client.Get().busy&&client.Get().state.config.enabled,"save failure reports error and retains old expression switch");
        ExpressionMock::state.config.enabled=2;client.Service(800);check(!client.Get().connected&&client.Get().result==MHM_VERSION,"malformed optional plugin state rejected");
        ExpressionMock::state.config.enabled=1;ExpressionMock::state.tick=1;client.Service(2000);
        check(!client.Get().connected&&client.Get().result==MHM_NOT_READY,"stopped expression worker not displayed as live");
    }
    std::printf("PASS: %u ImGui control checks; no windows, input automation or game process\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
