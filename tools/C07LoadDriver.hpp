#pragma once
#include <mhw_morph_api.h>
#include <mhw_morph_identity.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace c07driver {
using IdentityQuery=MhmResult(MHM_CALL*)(uint32_t,uint32_t,uint64_t,MhmAssetIdentity*);
struct Frozen {const char* model;const char* modelSha;const char* packSha;const char* keys[15];unsigned selected[6];};
inline void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<size_t N> bool Text(const char(&actual)[N],const char* expected){return std::memchr(actual,0,N)&&std::strcmp(actual,expected)==0;}
inline std::array<uint64_t,5> Inspect(const MhmApi& api,IdentityQuery identity,const Frozen* frozen){
    Require(api.target_at&&api.key_at&&identity,"missing SDK identity/target API");
    std::array<uint64_t,5> found{};
    for(unsigned i=0;i<5;i++){
        MhmTargetInfo t{};Require(api.target_at(i,&t)==MHM_OK&&t.part<5&&t.handle&&t.key_count==15&&!found[t.part],"expected exactly five 15-key targets");
        const auto& f=frozen[t.part];Require(Text(t.model_id,f.model),"frozen model mismatch");
        MhmAssetIdentity id{};Require(identity(MHM_IDENTITY_VERSION,sizeof(id),t.handle,&id)==MHM_OK&&id.handle==t.handle&&Text(id.model_sha256,f.modelSha)&&Text(id.pack_sha256,f.packSha),"frozen asset digest mismatch");
        for(unsigned k=0;k<15;k++){MhmKeyInfo info{};Require(api.key_at(t.handle,k,&info)==MHM_OK&&Text(info.id,f.keys[k]),"frozen key order mismatch");}
        for(unsigned k=0;k<6;k++){Require(f.selected[k]<15,"selected key bound");for(unsigned j=0;j<k;j++)Require(f.selected[k]!=f.selected[j],"duplicate selected key");}
        found[t.part]=t.handle;
    }
    MhmTargetInfo extra{};Require(api.target_at(5,&extra)==MHM_NOT_READY,"unexpected target count");return found;
}
inline std::array<MhmUpdate,75> Batch(char state,uint64_t elapsedMs,const Frozen* f,const std::array<uint64_t,5>& handles){
    Require(state=='Z'||state=='S'||state=='D',"unknown state");std::array<MhmUpdate,75> batch{};
    for(unsigned p=0;p<5;p++){
        for(unsigned k=0;k<15;k++)batch[p*15+k]={handles[p],k,0};
        if(state!='Z')for(unsigned k=0;k<6;k++)batch[p*15+f[p].selected[k]].weight=.1f;
        if(state=='D'){const auto phase=double(elapsedMs%2000)/1000.;const auto value=.05+.1*(phase<=1?phase:2-phase);batch[p*15+f[p].selected[5]].weight=float(value);}
    }return batch;
}
inline std::array<MhmUpdate,75> Probe(unsigned index,const Frozen* f,const std::array<uint64_t,5>& handles){
    Require(index<50,"probe count bound");auto batch=Batch('Z',0,f,handles);const auto part=index/10;
    batch[part*15+f[part].selected[5]].weight=index%2?0.f:.1f;return batch;
}
struct Schedule {
    char state;uint64_t begin,frequency,lastHeart,nextSubmit=0;bool submitted=false;
    Schedule(char s,uint64_t start,uint64_t freq):state(s),begin(start),frequency(freq),lastHeart(start){Require(freq>0&&freq<=10000000000ull,"invalid frequency");}
    bool Expired(uint64_t now)const{return now<begin||now<lastHeart||now-begin>=frequency*90||now-lastHeart>=frequency*2;}
    bool HeartbeatDue(uint64_t now){if(now-lastHeart<frequency/2)return false;lastHeart=now;return true;}
    bool SubmitDue(uint64_t now){if(submitted&&(state!='D'||now<nextSubmit))return false;submitted=true;nextSubmit=begin+((now-begin)*30/frequency+1)*frequency/30;return true;}
};
struct Pending {
    struct Entry {unsigned part;uint64_t revision,qpc;bool observed=false;std::array<float,15> weights{};};
    std::vector<Entry> entries;
    void Accept(unsigned part,uint64_t revision,uint64_t qpc,const std::array<MhmUpdate,75>* batch=nullptr){
        Require(entries.size()<15000,"bounded response ledger full");Entry e{part,revision,qpc};
        if(batch)for(unsigned k=0;k<15;k++)e.weights[k]=(*batch)[part*15+k].weight;entries.push_back(e);
    }
    std::optional<uint64_t> Observe(unsigned part,uint64_t revision,uint64_t qpc,const MhmWeightState* weights=nullptr){
        for(auto it=entries.rbegin();it!=entries.rend();++it)if(it->part==part&&it->revision==revision){
            if(it->observed)return {};Require(qpc>=it->qpc,"application QPC before acceptance");
            if(weights)for(unsigned k=0;k<15;k++)Require(std::abs(it->weights[k]-weights[k].applied)<1e-6f,"application weights do not match revision");
            it->observed=true;return qpc-it->qpc;
        }return {};
    }
    size_t Unobserved()const{return size_t(std::count_if(entries.begin(),entries.end(),[](const auto& e){return !e.observed;}));}
};
// One session owns one source. An exception, stop or deadline destroys it;
// there is no automatic reconnect or replay onto new target handles.
class Source {
    const MhmApi& api;
public:
    uint64_t id=0;
    explicit Source(const MhmApi& value):api(value){
        Require(api.open_source&&api.close_source&&api.heartbeat&&api.submit&&api.read_weights&&api.limits,"incomplete SDK table");
        MhmLimits limits{};Require(api.limits(&limits)==MHM_OK&&limits.targets>=5&&limits.keys_per_target>=15&&limits.batch_updates>=75,"SDK capacity mismatch");
        Require(api.open_source("c07-bounded-measurement",200,2000,&id)==MHM_OK&&id,"SDK source open failed");
    }
    Source(const Source&)=delete;Source& operator=(const Source&)=delete;
    MhmResult Close()noexcept{if(!id)return MHM_OK;const auto closing=id;id=0;return api.close_source(closing);}
    ~Source(){Close();}
};
class Controller {
    const MhmApi& api;IdentityQuery identity;const Frozen* frozen;
    char state;Schedule schedule;std::array<MhmUpdate,75> current{};
    uint64_t revision=0,nextProbe=0;unsigned probeIndex=0;
public:
    std::array<uint64_t,5> handles;std::unique_ptr<Source> source;Pending pending;
    Controller(const MhmApi& a,IdentityQuery i,const Frozen* f,char s,uint64_t now,uint64_t freq):api(a),identity(i),frozen(f),state(s),schedule(s,now,freq),handles(Inspect(a,i,f)){
        Require(s=='Z'||s=='S'||s=='D'||s=='P',"unknown driver state");source=std::make_unique<Source>(api);
    }
    template<class Clock,class Log> bool Service(Clock&& clock,Log&& log){
        try{
            const auto now=clock();Require(!schedule.Expired(now),"90-second deadline, lease gap or clock failure");
            const auto elapsed=(now-schedule.begin)*1000/schedule.frequency;
            if(schedule.HeartbeatDue(now)){
                Require(Inspect(api,identity,frozen)==handles,"target handle/epoch changed");
                Require(api.heartbeat(source->id)==MHM_OK,"SDK heartbeat failed");log("heartbeat",5,0,now,0);
            }
            const bool probeDue=state=='P'&&probeIndex<50&&now>=nextProbe;
            if(probeDue||(state!='P'&&schedule.SubmitDue(now))){
                current=state=='P'?Probe(probeIndex++,frozen,handles):Batch(state,elapsed,frozen,handles);
                if(state=='P')nextProbe=now+schedule.frequency/2;
                const auto before=clock();Require(api.submit(source->id,current.data(),75,&revision)==MHM_OK,"SDK submission failed");const auto accepted=clock();
                for(unsigned p=0;p<5;p++)if(state!='P'||p==(probeIndex-1)/10){pending.Accept(p,revision,accepted,&current);log("accepted",p,revision,accepted,before);}
            }
            for(unsigned p=0;p<5;p++){
                std::array<MhmWeightState,15> values{};uint32_t needed=0;uint64_t requested=0,applied=0;
                Require(api.read_weights(handles[p],values.data(),15,&needed,&requested,&applied)==MHM_OK&&needed==15,"SDK target/read changed");
                if(revision){Require(requested==revision,"competing request revision");for(unsigned k=0;k<15;k++)Require(values[k].source==source->id&&std::abs(values[k].requested-current[p*15+k].weight)<1e-6f,"competing source or requested weights");}
                const auto observed=clock();if(const auto latency=pending.Observe(p,applied,observed,values.data()))log("applied_observed",p,applied,observed,*latency);
            }
            return state=='P'&&probeIndex==50&&now>=nextProbe;
        }catch(...){source->Close();throw;}
    }
};
}
