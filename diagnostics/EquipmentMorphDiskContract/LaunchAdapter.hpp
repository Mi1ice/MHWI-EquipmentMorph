#pragma once
#include "Pool.hpp"
#include <algorithm>
#include <charconv>
#include <string_view>

namespace facecontinuous {
struct Identity {
    std::uintptr_t owner=0,model=0,source=0,player=0;
    bool operator==(const Identity&) const = default;
    bool Valid() const {return owner>0xFFFF&&model>0xFFFF&&source>0xFFFF&&player>0xFFFF;}
};
struct Frame {
    std::uintptr_t engine=0,renderer=0,fence=0;
    std::uint64_t before=0,after=0;
    unsigned workers=0,mode=0,thread=0;
    bool Stable() const {return engine>0xFFFF&&renderer>0xFFFF&&fence>0xFFFF&&before==after&&before!=UINT64_MAX&&workers>0&&workers<=8&&thread!=0;}
};
inline bool ParseWeight(std::string_view text,float& weight) {
    while(!text.empty()&&(text.front()==' '||text.front()=='\t'||text.front()=='\r'||text.front()=='\n'))text.remove_prefix(1);
    while(!text.empty()&&(text.back()==' '||text.back()=='\t'||text.back()=='\r'||text.back()=='\n'))text.remove_suffix(1);
    float candidate=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),candidate);
    if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(candidate)||candidate<0||candidate>1)return false;
    weight=candidate==0?0:candidate;return true;
}
// Runtime gate serializes these methods; no native calls while held.
struct InputCycleRejection {
    bool open=false,syncSeen=false,dispatch=false;
    unsigned kind=0,launches=0,waits=0,compute=0,draw=0,dispatches=0;
    std::uint64_t before=0,after=0,counter=0,lastSignal=0;
};
class LaunchAdapter {
    std::uint64_t bindingEpoch=1;
    launchpool::Pool pool;
    std::array<std::uintptr_t,3> handles{};
    launchpool::MorphVersion latest{};
    unsigned cycleCompute=0,cycleDraw=0,cycleDispatch=0;
    Identity target{};
    Frame fixed{},batch{};
    std::array<std::uintptr_t,8> launchEvents{},waitEvents{};
    bool armed=false,ready=false,started=false,failed=false,open=false,syncSeen=false,originalCycle=false;
    unsigned launches=0,nextWait=0;
    std::uint64_t counter=0,lastSignal=0,boundaries=0,signals=0,inputs=0,dispatches=0,uploadDone=0,drawDone=0,originalBypasses=0;
    const char* reason="none";
    std::optional<InputCycleRejection> inputRejection;
    bool Bad(const char* why){if(failed)return false;failed=true;ready=false;open=false;reason=why;pool.Invalidate();return false;}
    bool Same(Frame f)const{return f.engine==fixed.engine&&f.renderer==fixed.renderer&&f.fence==fixed.fence&&f.workers==fixed.workers&&f.thread==fixed.thread;}
    bool Batch(Frame f)const{return Same(f)&&f.mode==batch.mode&&f.before==counter;}
public:
    explicit LaunchAdapter(std::uint64_t epoch=1):bindingEpoch(epoch){}
    bool Arm(){if(armed||failed)return false;armed=true;return true;}
    bool Capture(Identity id,std::uintptr_t passed){if(!Live()||!id.Valid()||passed!=id.source)return false;if(!target.Valid())target=id;return id==target;}
    Identity Target()const{return target;}
    bool Live()const{return armed&&!failed;}
    bool Ready()const{return ready&&!failed;}
    bool Started()const{return started&&!failed;}
    bool HasLaunched()const{return started;}
    const char* Reason()const{return reason;}
    std::optional<InputCycleRejection> InputRejection()const{return inputRejection;}
    Frame Fixed()const{return fixed;}
    unsigned PendingWaits()const{return nextWait;}
    std::uint64_t Boundaries()const{return boundaries;}
    std::uint64_t Signals()const{return signals;}
    std::uint64_t Inputs()const{return inputs;}
    std::uint64_t Dispatches()const{return dispatches;}
    std::uint64_t OriginalBypasses()const{return originalBypasses;}
    bool PassOriginalInput(){
        if(!Started()||!open||!originalCycle)return false;
        originalBypasses++;return true;
    }
    launchpool::View View(){return pool.Get();}
    bool PublishResources(std::array<std::uintptr_t,3> values){
        if(!Live()||!target.Valid()||ready)return false;
        for(unsigned i=0;i<3;i++){if(values[i]<=0xFFFF)return Bad("private_resources_invalid");for(unsigned j=0;j<i;j++)if(values[i]==values[j])return Bad("private_resources_alias");}
        handles=values;ready=true;return true;
    }
    bool Request(launchpool::MorphVersion weight){
        if(!Ready()||!weight.Valid())return false;
        latest=weight;
        return !started||pool.Request(latest);
    }
    std::optional<launchpool::Token> Prepare(){return Started()?pool.Prepare():std::nullopt;}
    bool BeginUpload(launchpool::Token token){return Started()&&pool.BeginUpload(token);}
    bool QueueUpload(launchpool::Token token,std::uint64_t stamp){
        if(!Started())return false;
        if(!pool.UploadQueued(token,stamp))return Bad("upload_token_rejected");return true;
    }
    bool Launch(Frame f,unsigned index,std::uintptr_t event){
        if(!Ready())return false;
        if(!started&&index!=0)return false;
        if(!f.Stable()||index>=f.workers||!event)return Bad("launch_contract");
        if(index==0){
            if(open||launches||nextWait)return Bad("launch_before_previous_signal");
            if(!started){fixed=f;lastSignal=f.before;if(!pool.Start(f.before,bindingEpoch)||!pool.Request(latest))return Bad("pool_start_failed");started=true;}
            if(!Same(f)||f.before!=lastSignal)return Bad("launch_identity_or_counter");
            if(!pool.Launch(f.before))return Bad("pool_launch_rejected");
            const auto view=pool.Get();
            // Freeze this route until Signal. Requests and upload completions
            // cannot publish a private input in the middle of this cycle.
            originalCycle=view.live&&view.frameOpen&&view.active<0&&view.requested.IsZero()&&view.applied.IsZero()
                &&std::all_of(view.slots.begin(),view.slots.end(),[](const auto& slot){return slot.phase==launchpool::Phase::Free;});
            batch=f;counter=f.before;open=true;syncSeen=false;
            cycleCompute=cycleDraw=cycleDispatch=0;
        }
        if(!open||index!=launches||!Batch(f))return Bad("mixed_launch_batch");
        if(launchEvents[index]&&launchEvents[index]!=event)return Bad("launch_event_changed");
        launchEvents[index]=event;launches++;return true;
    }
    bool Wait(Frame f,unsigned index,std::uintptr_t event,unsigned result){
        if(!Started())return false;
        if(!open||launches!=f.workers||!f.Stable()||!Batch(f)||index!=nextWait||index>=f.workers||!event||result!=0)return Bad("worker_wait_contract");
        if(waitEvents[index]&&waitEvents[index]!=event)return Bad("worker_event_changed");
        waitEvents[index]=event;nextWait++;return true;
    }
    bool Boundary(Frame f){
        if(!Started())return false;
        if(!open||!f.Stable()||!Batch(f)||launches!=f.workers||nextWait!=f.workers||syncSeen)return Bad("incomplete_sync_boundary");
        if(!pool.Join(f.before))return Bad("pool_join_rejected");
        nextWait=0;syncSeen=true;boundaries++;return true; // Publication already occurred at first launch.
    }
    bool Completed(std::uint64_t upload,std::uint64_t drawing){
        if(!Started())return false;
        if(upload==UINT64_MAX||drawing==UINT64_MAX||upload<uploadDone||drawing<drawDone)return Bad("completion_rejected");
        if(!pool.Completed(upload,drawing))return Bad("pool_completion_rejected");
        uploadDone=upload;drawDone=drawing;return true;
    }
    std::uintptr_t Input(Identity seen,std::uintptr_t passed,std::uint64_t before,std::uint64_t after,bool dispatch=false,unsigned kind=0){
        if(!Started()||seen.owner!=target.owner)return passed;
        const auto actual=target;
        if(seen!=actual){Bad("target_identity_changed");return passed;}
        if(!open||before!=after||before!=counter){
            // Retain the first rejected proof before Bad closes the cycle.
            // No formatting, allocation or logging on the native callback.
            inputRejection=InputCycleRejection{open,syncSeen,dispatch,kind,launches,nextWait,cycleCompute,cycleDraw,cycleDispatch,before,after,counter,lastSignal};
            Bad("input_outside_launch_cycle");return passed;
        }
        const auto slot=pool.Input(before,after);
        if(!pool.Get().live){Bad("pool_input_rejected");return passed;}
        const auto expected=slot?handles[*slot]:actual.source;
        if((!dispatch&&passed!=actual.source)||(dispatch&&passed!=expected)){Bad("input_or_dispatch_contract");return passed;}
        if(dispatch){cycleDispatch++;dispatches++;return passed;}
        if(kind==1)cycleCompute++;else if(kind==2)cycleDraw++;
        else {Bad("unknown_input_kind");return passed;}
        inputs++;return expected;
    }
    bool Signal(std::uintptr_t renderer,std::uintptr_t fence,std::uint64_t before,std::uint64_t after,std::uint64_t value,bool success){
        if(!Started())return false;
        if(!open||!syncSeen||launches!=fixed.workers||nextWait||renderer!=fixed.renderer||fence!=fixed.fence||!success||before!=after||before!=value||counter==UINT64_MAX||value==UINT64_MAX||value!=counter+1)return Bad("signal_contract");
        if(cycleDispatch!=cycleCompute)return Bad("cycle_input_coverage");
        if(!pool.Signal(value,success,true))return Bad("pool_signal_rejected");
        lastSignal=value;open=false;launches=0;syncSeen=false;signals++;return true;
    }
    void Invalidate(std::uintptr_t owner,const char* why){if(!owner||owner==target.owner)Bad(why);}
};
}
