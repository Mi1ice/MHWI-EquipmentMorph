#pragma once
#include "LaunchAdapter.hpp"
namespace facecontinuous {
// Observe a whole NEW native worker cycle after the old uploader has stopped.
// Require its Signal, a successor join, and both GPU completions before reusing
// any private slot. No native/GPU calls, allocations or pointers are retained here.
class DrainBoundary {
    Frame fixed{},batch{};
    std::array<std::uintptr_t,8> launchEvents{},waitEvents{};
    std::uint64_t floor=0,uploadFloor=0,covered=0,uploadDone=0,drawDone=0;
    unsigned launches=0,waits=0;
    bool begun=false,failed=false,open=false,joined=false,successor=false,successorJoined=false;
    const char* failure="none";
    bool Bad(const char* why){failed=true;failure=why;return false;}
    bool Same(Frame f)const{return f.Stable()&&f.engine==fixed.engine&&f.renderer==fixed.renderer&&f.fence==fixed.fence&&f.thread==fixed.thread;}
    bool Batch(Frame f)const{return Same(f)&&f.workers==batch.workers&&f.before==batch.before&&f.mode==batch.mode;}
public:
    bool Start(Frame f,std::uint64_t counterFloor,std::uint64_t requiredUpload){
        if(begun||!f.Stable()||counterFloor<f.before||counterFloor>=UINT64_MAX-2||requiredUpload==UINT64_MAX)return false;
        fixed=f;floor=counterFloor;uploadFloor=requiredUpload;begun=true;return true;
    }
    bool Observing()const{return begun&&!failed&&!successorJoined;}
    bool Failed()const{return failed;}
    const char* Reason()const{return failure;}
    bool CpuJoined()const{return begun&&!failed&&successorJoined;}
    std::uint64_t CoveredSignal()const{return covered;}
    std::uint64_t UploadFloor()const{return uploadFloor;}
    bool Ready()const{return CpuJoined()&&uploadDone>=uploadFloor&&drawDone>=covered;}
    bool Launch(Frame f,unsigned index,std::uintptr_t event){
        if(!Observing())return false;
        if(!Same(f)||index>=f.workers||!event)return Bad("drain_launch_identity");
        // Starting in an existing cycle: ignore its suffix; no proof from it.
        if(!open&&index!=0)return true;
        if(index==0){
            if(open||launches||waits||f.before<floor||f.before>=UINT64_MAX-1||(successor&&f.before!=covered))return Bad("drain_launch_sequence");
            batch=f;open=true;joined=false;
        }
        if(!open||index!=launches||!Batch(f)||(launchEvents[index]&&launchEvents[index]!=event))return Bad("drain_launch_batch");
        launchEvents[index]=event;launches++;return true;
    }
    bool Wait(Frame f,unsigned index,std::uintptr_t event,unsigned result){
        if(!Observing())return false;
        if(!open)return true;
        if(!Batch(f)||launches!=batch.workers||index!=waits||index>=batch.workers||!event||result!=0||(waitEvents[index]&&waitEvents[index]!=event))return Bad("drain_wait_contract");
        waitEvents[index]=event;waits++;return true;
    }
    bool Join(Frame f){
        if(!Observing())return false;
        if(!open)return true;
        if(!Batch(f)||joined||launches!=batch.workers||waits!=batch.workers)return Bad("drain_join_contract");
        joined=true;if(successor)successorJoined=true;return true;
    }
    bool Signal(std::uintptr_t renderer,std::uintptr_t fence,std::uint64_t before,std::uint64_t after,std::uint64_t value,bool success){
        if(!Observing())return false;
        if(!open)return true;
        if(successor||!joined||renderer!=fixed.renderer||fence!=fixed.fence||!success||before!=after||before!=value||value!=batch.before+1)return Bad("drain_signal_contract");
        covered=value;successor=true;open=false;joined=false;launches=waits=0;return true;
    }
    bool Completed(std::uint64_t upload,std::uint64_t drawing){
        if(failed)return false;
        if(!begun||upload==UINT64_MAX||drawing==UINT64_MAX||upload<uploadDone||drawing<drawDone)return Bad("drain_completion_contract");
        uploadDone=upload;drawDone=drawing;return true;
    }
    void Refuse(){Bad("drain_native_snapshot");}
};
}
