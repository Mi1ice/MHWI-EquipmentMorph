#include <Windows.h>
#include <cstdio>
#include <stdexcept>
#if __has_include("C07LoadDriver.hpp")
#include "C07LoadDriver.hpp"
namespace fakeapi {
inline uint64_t source=0,revision=100,applied=0;inline unsigned submits=0,closes=0,hearts=0;inline bool stale=false,expired=false,failSubmit=false;
inline std::array<MhmUpdate,75> current{};
inline MhmResult Limits(MhmLimits* p){*p={5,1024,16,5120,64};return MHM_OK;}
inline MhmResult Open(const char*,uint32_t priority,uint32_t lease,uint64_t* id){if(priority!=200||lease!=2000)return MHM_INVALID;*id=source=7;return MHM_OK;}
inline MhmResult Close(uint64_t){++closes;source=0;return MHM_OK;}
inline MhmResult Heart(uint64_t){++hearts;return expired?MHM_STALE:MHM_OK;}
inline MhmResult Submit(uint64_t,const MhmUpdate* v,uint32_t n,uint64_t* r){if(n!=75||failSubmit)return MHM_INVALID;std::copy_n(v,75,current.begin());*r=++revision;++submits;return MHM_OK;}
inline MhmResult Read(uint64_t h,MhmWeightState* out,uint32_t,uint32_t* needed,uint64_t* req,uint64_t* app){if(stale)return MHM_STALE;*needed=15;*req=revision;*app=applied;for(unsigned k=0;k<15;k++)out[k]={current[(h-1)*15+k].weight,0,source};return MHM_OK;}
inline void Reset(){source=0;revision=100;applied=0;submits=closes=hearts=0;stale=expired=failSubmit=false;}
}
#endif
int main(){try{
#if !__has_include("C07LoadDriver.hpp")
    volatile bool missing=true;if(missing)throw std::runtime_error("C07 SDK driver unavailable");
#else
    unsigned checks=0;auto check=[&](bool b,const char* n){if(!b)throw std::runtime_error(n);++checks;};
    using namespace c07driver;
    Frozen f[5]{};for(unsigned p=0;p<5;p++){f[p].model="model";f[p].modelSha="modelhash";f[p].packSha="packhash";for(unsigned i=0;i<15;i++)f[p].keys[i]="key";for(unsigned i=0;i<6;i++)f[p].selected[i]=i;}
    std::array<uint64_t,5> handles={1,2,3,4,5};
    auto z=Batch('Z',0,f,handles);check(z.size()==75&&std::all_of(z.begin(),z.end(),[](auto& v){return v.weight==0;}),"Z complete zero batch");
    auto s=Batch('S',0,f,handles);check(std::count_if(s.begin(),s.end(),[](auto& v){return v.weight==.1f;})==30,"S thirty weights");
    for(auto [ms,w]:std::array<std::pair<uint64_t,float>,5>{{{0,.05f},{500,.1f},{1000,.15f},{1500,.1f},{2000,.05f}}}){
        auto d=Batch('D',ms,f,handles);check(std::count_if(d.begin(),d.end(),[&](auto& v){return std::abs(v.weight-w)<.00001f;})==(w==.1f?30:5),"triangle endpoints/interior");
        check(std::count_if(d.begin(),d.end(),[](auto& v){return v.weight!=0;})==30,"D thirty nonzero");
    }
    // Fake API replaces only the process-bound public API; the scheduling,
    // identity validation, source lifecycle and weights below are real driver.
    struct Fake {
        static MhmResult Target(uint32_t i,MhmTargetInfo* out){if(i>=5)return MHM_NOT_READY;*out={uint64_t(i+1),i,15,{}};strcpy_s(out->model_id,"model");return MHM_OK;}
        static MhmResult Key(uint64_t,uint32_t,MhmKeyInfo* out){*out={};strcpy_s(out->id,"key");return MHM_OK;}
        static MhmResult Identity(uint32_t,uint32_t,uint64_t h,MhmAssetIdentity* out){*out={};out->handle=h;strcpy_s(out->model_sha256,"modelhash");strcpy_s(out->pack_sha256,"packhash");return MHM_OK;}
    };
    MhmApi api{};api.target_at=Fake::Target;api.key_at=Fake::Key;
    auto found=Inspect(api,Fake::Identity,f);check(found==handles,"exact five identities");
    f[2].packSha="changed";bool rejected=false;try{Inspect(api,Fake::Identity,f);}catch(...){rejected=true;}check(rejected,"identity mismatch rejects");f[2].packSha="packhash";
    f[1].keys[14]="wrong";rejected=false;try{Inspect(api,Fake::Identity,f);}catch(...){rejected=true;}check(rejected,"key mismatch rejects");f[1].keys[14]="key";
    Schedule staticPlan('S',0,1000);check(staticPlan.SubmitDue(0)&&!staticPlan.SubmitDue(34)&&!staticPlan.SubmitDue(1000),"S only one submission");
    Schedule dynamic('D',0,1000);check(dynamic.SubmitDue(0)&&!dynamic.SubmitDue(10)&&dynamic.SubmitDue(34)&&dynamic.SubmitDue(1000)&&!dynamic.SubmitDue(1001),"D skips catchup bursts");
    Schedule heart('Z',0,1000);check(!heart.HeartbeatDue(499)&&heart.HeartbeatDue(500)&&!heart.HeartbeatDue(501),"500ms heartbeat");
    check(!heart.Expired(1999)&&heart.Expired(2500),"lease gap stops");
    Schedule duration('D',0,1000);check(duration.Expired(90000)&&duration.Expired(0-uint64_t(1)),"90 second and backward/overflow stop");
    auto probe=Probe(49,f,handles);check(probe.size()==75&&probe[4*15+5].weight==0,"50th step is independent reset");
    Pending pending;pending.Accept(0,10,100);pending.Accept(0,11,110);check(!pending.Observe(0,9,120).has_value(),"older applied revision not falsely attributed");
    check(pending.Observe(0,11,150)==40,"exact revision observed");check(!pending.Observe(0,11,160).has_value(),"duplicate observation ignored");check(pending.Unobserved()==1,"superseded revision stays unobserved");
    Pending absent;absent.Accept(1,20,100);check(absent.Unobserved()==1,"missing application is not zero");
    api.limits=fakeapi::Limits;api.open_source=fakeapi::Open;api.close_source=fakeapi::Close;api.heartbeat=fakeapi::Heart;api.submit=fakeapi::Submit;api.read_weights=fakeapi::Read;
    const auto noLog=[](const char*,unsigned,uint64_t,uint64_t,uint64_t){};
    fakeapi::Reset();{Controller c(api,Fake::Identity,f,'S',1000,1000);for(uint64_t now=1000;now<2000;now+=10)c.Service([=]{return now;},noLog);check(fakeapi::submits==1&&fakeapi::hearts==1,"static session does not resubmit");}check(fakeapi::closes==1&&!fakeapi::source,"stop releases source once");
    fakeapi::Reset();{Controller c(api,Fake::Identity,f,'D',1000,1000);fakeapi::failSubmit=true;bool failed=false;try{c.Service([]{return uint64_t(1000);},noLog);}catch(...){failed=true;}check(failed&&fakeapi::closes==1&&!fakeapi::source,"submit error closes immediately");}
    fakeapi::Reset();{Controller c(api,Fake::Identity,f,'Z',1000,1000);fakeapi::stale=true;bool failed=false;try{c.Service([]{return uint64_t(1000);},noLog);}catch(...){failed=true;}check(failed&&!fakeapi::source,"stale target stops without replay");}
    fakeapi::Reset();{Controller c(api,Fake::Identity,f,'Z',1000,1000);fakeapi::expired=true;bool failed=false;try{c.Service([]{return uint64_t(1500);},noLog);}catch(...){failed=true;}check(failed&&!fakeapi::source,"expired lease closes without reconnect");}
    fakeapi::Reset();{Controller c(api,Fake::Identity,f,'P',1000,1000);unsigned accepts=0;bool done=false;for(uint64_t now=1000;now<=26000;now+=5)done=c.Service([=]{return now;},[&](const char* kind,unsigned,uint64_t,uint64_t,uint64_t){if(!std::strcmp(kind,"accepted"))++accepts;});check(done&&fakeapi::submits==50&&accepts==50,"50 probes tracked once for selected part");}
    std::printf("PASS %u C07 driver checks\n",checks);
#endif
    return 0;
}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}}
