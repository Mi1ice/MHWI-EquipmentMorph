#pragma once
#include "Trace.hpp"
#include <optional>

namespace mhmvalidation {
enum class Action {Begin,PreviousCase,NextCase,PreviousStep,NextStep,RepeatStep,ZeroAll,End};
enum class Phase {NotReady,NotStarted,WaitingApply,Holding,WaitingZero,Covered,Interrupted,Ending,Ended};
inline const char* PhaseName(Phase p){switch(p){case Phase::NotReady:return "not_ready";case Phase::NotStarted:return "not_started";case Phase::WaitingApply:return "waiting_apply";case Phase::Holding:return "holding";case Phase::WaitingZero:return "waiting_zero";case Phase::Covered:return "covered";case Phase::Interrupted:return "interrupted";case Phase::Ending:return "ending";case Phase::Ended:return "ended";}return "invalid";}
struct DisplayRow {std::string source,id;float weight=0;bool fixed=false;};
struct Snapshot {
    Phase phase=Phase::NotReady;size_t caseIndex=0,stepIndex=0,caseCount=0,stepCount=0;std::string caseId,caseTitle,error;
    bool pending=false,started=false,ending=false,finished=false,normalEnd=false,evidenceComplete=true,applicationConfirmed=false,zeroConfirmed=false;
    uint64_t acceptedRevision=0;Observation reference,observation;std::vector<float> targets;std::vector<DisplayRow> rows;
};
class Controller {
    SdkSession& session;const Catalog& catalog;TraceWriter& trace;Snapshot state;
    uint64_t now=0,pendingAt=0,nextRead=0,nextHeartbeat=0;Phase waiting=Phase::WaitingApply;
    std::optional<size_t> nextCase;
    bool Emit(const char* event){
        TraceRecord r;r.event=event;r.phase=PhaseName(state.phase);r.error=state.error;r.caseId=catalog.cases[state.caseIndex].id;r.step=state.stepIndex;r.nowMs=now;r.acceptedRevision=state.acceptedRevision;r.reference=state.reference;r.observation=state.observation;r.targets=state.targets;
        if(r.targets.empty())r.targets.assign(r.reference.liveIds.size(),0);r.accepted=state.acceptedRevision!=0;r.applicationConfirmed=state.applicationConfirmed;r.zeroConfirmed=state.zeroConfirmed;r.normalEnd=state.normalEnd;r.evidenceComplete=state.evidenceComplete;
        if(trace.Append(r))return true;state.evidenceComplete=false;state.error="操作记录不完整；停止新案例和非零步骤，仍可归零或结束。";return false;
    }
    void Fail(std::string message){
        state.error=std::move(message);state.phase=Phase::Interrupted;state.pending=false;state.started=false;state.applicationConfirmed=false;state.zeroConfirmed=false;state.normalEnd=false;state.finished=state.ending;nextCase.reset();session.Release();Emit("interrupted");
    }
    bool Request(std::vector<float> weights,Phase phase,uint64_t time,size_t step,std::optional<size_t> selected={}){
        now=time;const auto old=state;const auto observed=session.Read();state.observation=observed;
        if(observed.result!=MHM_OK){Fail("归零／应用未确认：目标或核心不可用。");return false;}
        state.reference=observed;state.targets=std::move(weights);state.stepIndex=step;state.phase=phase;state.pending=true;state.acceptedRevision=0;state.applicationConfirmed=false;state.zeroConfirmed=false;state.normalEnd=false;waiting=phase;pendingAt=now;nextCase=selected;
        const bool logged=Emit("operation_requested");
        if(!logged&&std::any_of(state.targets.begin(),state.targets.end(),[](float w){return w!=0;})){
            const auto error=state.error;state=old;state.evidenceComplete=false;state.error=error;nextCase.reset();return false;
        }
        const auto result=session.Submit(state.targets,state.acceptedRevision);state.reference.source=session.Source();
        if(result!=MHM_OK){Fail("提交未被接受，结果="+std::to_string(result)+"；未确认归零或应用。");return false;}
        Emit("submitted");return true;
    }
public:
    Controller(SdkSession& sdk,const Catalog& cases,TraceWriter& writer):session(sdk),catalog(cases),trace(writer){Require(!catalog.cases.empty()&&!catalog.steps.empty(),"Empty controller catalog");}
    Snapshot GetSnapshot()const {
        auto out=state;out.caseCount=catalog.cases.size();out.stepCount=catalog.steps.size();const auto& c=catalog.cases[out.caseIndex];out.caseId=c.id;out.caseTitle=c.title;
        for(const auto& k:c.fixed)out.rows.push_back({catalog.labels.at(k.id),k.id,k.weight,true});
        for(const auto& id:c.varying)out.rows.push_back({catalog.labels.at(id),id,catalog.steps[out.stepIndex],false});return out;
    }
    bool Dispatch(Action action,uint64_t time){
        if(state.finished)return false;
        if(action==Action::End){if(state.ending&&state.pending)return true;state.ending=true;nextCase.reset();return Request(std::vector<float>(catalog.expectedKeyCount,0),Phase::Ending,time,0);}
        if(action==Action::ZeroAll&&!state.ending)return Request(std::vector<float>(catalog.expectedKeyCount,0),Phase::WaitingZero,time,0);
        if(state.pending||state.ending||!state.evidenceComplete)return false;
        if(action==Action::PreviousCase||action==Action::NextCase){
            if(state.phase!=Phase::Holding&&state.phase!=Phase::NotStarted)return false;
            if((action==Action::PreviousCase&&state.caseIndex==0)||(action==Action::NextCase&&state.caseIndex+1>=catalog.cases.size()))return false;
            const auto selected=action==Action::PreviousCase?state.caseIndex-1:state.caseIndex+1;
            return Request(std::vector<float>(catalog.expectedKeyCount,0),Phase::WaitingZero,time,0,selected);
        }
        if(action==Action::Begin){if(state.phase!=Phase::NotStarted||state.started)return false;state.error.clear();if(!Request(BuildWeights(catalog,state.caseIndex,0,state.observation.liveIds),Phase::WaitingApply,time,0))return false;state.started=true;return true;}
        if(state.phase!=Phase::Holding||!state.started)return false;
        size_t step=state.stepIndex;if(action==Action::PreviousStep){if(step==0)return false;--step;}else if(action==Action::NextStep){if(step+1>=catalog.steps.size())return false;++step;}else if(action!=Action::RepeatStep)return false;
        return Request(BuildWeights(catalog,state.caseIndex,step,state.observation.liveIds),Phase::WaitingApply,time,step);
    }
    void Tick(uint64_t time){
        if(state.finished)return;now=time;
        if(now>=nextHeartbeat){nextHeartbeat=now+500;if(session.Source()&&session.Heartbeat()!=MHM_OK){Fail("来源租约失效；归零或应用未确认。");return;}}
        if(now<nextRead)return;nextRead=now+100;
        auto observed=session.Read();state.observation=observed;
        if(observed.result!=MHM_OK){
            if(state.pending||state.started){Fail("目标变化或已失效；当前案例中断，归零未确认。");return;}
            if(state.ending){Fail("结束时目标不可用；归零未确认。");return;}
            if(session.Discover(catalog)==MHM_OK){state.observation=session.Read();if(state.observation.result==MHM_OK){state.reference=state.observation;state.phase=Phase::NotStarted;state.stepIndex=0;state.acceptedRevision=0;state.applicationConfirmed=false;state.zeroConfirmed=false;state.targets.assign(state.reference.liveIds.size(),0);Emit("ready");}}return;
        }
        if(state.reference.target&&observed.epoch!=state.reference.epoch&&(state.pending||state.started)){Fail("目标代次变化；旧案例不重放。");return;}
        if(state.pending){
            if(session.Confirmed(observed,state.targets,state.acceptedRevision,state.reference.epoch)){
                state.pending=false;state.applicationConfirmed=true;state.zeroConfirmed=waiting!=Phase::WaitingApply;
                if(state.ending){Emit("zero_confirmed");const auto result=session.Release();if(result!=MHM_OK){Fail("归零已确认，但来源释放失败。");return;}state.phase=Phase::Ended;state.started=false;state.finished=true;state.normalEnd=true;Emit("ended_released");return;}
                if(waiting==Phase::WaitingZero){if(nextCase)state.caseIndex=*nextCase;nextCase.reset();state.stepIndex=0;state.started=false;state.phase=Phase::NotStarted;}else state.phase=Phase::Holding;
                Emit(state.zeroConfirmed?"zero_confirmed":"application_confirmed");return;
            }
            if(now>=pendingAt&&now-pendingAt>=5000){Fail("等待超时；归零／应用未确认，已释放自身来源，其他来源可能恢复。");return;}
            const bool covered=std::any_of(observed.weights.begin(),observed.weights.end(),[&](const auto& w){return w.source!=session.Source();});
            const auto phase=covered?Phase::Covered:waiting;if(phase!=state.phase){state.phase=phase;Emit("waiting_state_changed");}
        }else if(state.started||state.acceptedRevision){
            const bool confirmed=session.Confirmed(observed,state.targets,state.acceptedRevision,state.reference.epoch);
            const auto phase=confirmed?(state.started?Phase::Holding:Phase::NotStarted):Phase::Covered;
            if(phase!=state.phase||state.applicationConfirmed!=confirmed){state.phase=phase;state.applicationConfirmed=confirmed;state.zeroConfirmed=confirmed&&!state.started;Emit("ownership_state_changed");}
        }
    }
};
}
