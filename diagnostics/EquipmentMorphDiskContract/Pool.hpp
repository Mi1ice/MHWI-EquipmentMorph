#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include "MorphVersion.hpp"

namespace launchpool {
// Experimental observed protocol, not proof of all native reference lifetimes.
enum class Phase {Free,Preparing,AwaitingStamp,Uploading,Ready,Active,Retiring,Quarantined};
struct Token {
    unsigned slot=3;
    std::uint64_t epoch=0,generation=0,request=0;
    bool operator==(const Token&) const = default;
};
struct Slot {
    Phase phase=Phase::Free;
    Token token{};
    MorphVersion weight{};
    std::uint64_t publicationCounter=0,uploadStamp=0,lastUseSignal=0,retirementFence=0,coverageFence=0;
    bool usePending=false,joinedAfterUse=false;
};
struct View {
    bool live=false,frameOpen=false;
    int active=-1;
    MorphVersion requested{},applied{};
    std::uint64_t epoch=0,request=0,lastSignal=0,publicationCounter=0;
    std::array<Slot,3> slots{};
};
class Pool {
    std::mutex gate;
    std::array<Slot,3> slots{};
    bool started=false,live=false,frameOpen=false;
    int active=-1;
    MorphVersion requested{},original{};
    std::uint64_t epoch=0,request=0,lastSignal=0,originalCounter=0,openCounter=0,uploadCompleted=0,drawCompleted=0;
    bool Match(Token t) const {return t.slot<slots.size()&&t.epoch==epoch&&slots[t.slot].token==t;}
    void Disable() {
        live=false;frameOpen=false;active=-1;
        for(auto& s:slots)if(s.phase!=Phase::Free)s.phase=Phase::Quarantined;
    }
    void RetireActive() {
        if(active<0)return;
        auto& s=slots[static_cast<unsigned>(active)];
        s.phase=Phase::Retiring;s.retirementFence=lastSignal+1;
        s.coverageFence=0;s.joinedAfterUse=false;active=-1;
    }
    void Reclaim() {
        for(auto& s:slots) {
            if(s.phase==Phase::Uploading&&s.uploadStamp<=uploadCompleted)
                s.phase=s.token.request==request?Phase::Ready:Phase::Free;
            if((s.phase==Phase::Ready||s.phase==Phase::Preparing)&&s.token.request!=request)s.phase=Phase::Free;
            if(s.phase==Phase::Retiring&&s.joinedAfterUse&&!s.usePending&&
               s.coverageFence>=s.retirementFence&&drawCompleted>=s.retirementFence&&
               drawCompleted>=s.lastUseSignal&&uploadCompleted>=s.uploadStamp)s.phase=Phase::Free;
        }
    }
public:
    bool Start(std::uint64_t counter,std::uint64_t bindingEpoch=1) {
        std::lock_guard lock(gate);
        if(started||counter==UINT64_MAX||bindingEpoch==0)return false;
        started=true;live=true;epoch=bindingEpoch;lastSignal=counter;return true;
    }
    bool Request(MorphVersion weight) {
        std::lock_guard lock(gate);
        if(!live||!weight.Valid())return false;
        if(weight==requested)return true;
        if(request==UINT64_MAX){Disable();return false;}
        requested=weight;request++;Reclaim();return true;
    }
    std::optional<Token> Prepare() {
        std::lock_guard lock(gate);
        if(!live||requested.IsZero()||(active>=0&&slots[static_cast<unsigned>(active)].weight==requested))return {};
        Reclaim();
        for(const auto& s:slots)if(s.phase!=Phase::Free&&s.phase!=Phase::Retiring&&s.token.request==request)return {};
        for(unsigned i=0;i<slots.size();i++)if(slots[i].phase==Phase::Free) {
            auto& s=slots[i];if(s.token.generation==UINT64_MAX){Disable();return {};}
            const auto generation=s.token.generation+1;
            s={};s.phase=Phase::Preparing;s.token={i,epoch,generation,request};s.weight=requested;return s.token;
        }
        return {};
    }
    bool BeginUpload(Token t) {
        std::lock_guard lock(gate);
        if(!live||!Match(t)||t.request!=request||slots[t.slot].phase!=Phase::Preparing)return false;
        slots[t.slot].phase=Phase::AwaitingStamp;return true;
    }
    bool UploadQueued(Token t,std::uint64_t stamp) {
        std::lock_guard lock(gate);
        if(!live||!Match(t)||slots[t.slot].phase!=Phase::AwaitingStamp)return false;
        if(stamp==0||stamp==UINT64_MAX){Disable();return false;}
        auto& s=slots[t.slot];s.uploadStamp=stamp;s.phase=Phase::Uploading;return true;
    }
    bool Completed(std::uint64_t upload,std::uint64_t drawing) {
        std::lock_guard lock(gate);
        if(!live)return false;
        if(upload==UINT64_MAX||drawing==UINT64_MAX||upload<uploadCompleted||drawing<drawCompleted){Disable();return false;}
        uploadCompleted=upload;drawCompleted=drawing;Reclaim();return true;
    }
    bool Launch(std::uint64_t counter) {
        std::lock_guard lock(gate);
        if(!live)return false;
        if(frameOpen||counter!=lastSignal||counter>=UINT64_MAX-1){Disable();return false;}
        if(requested.IsZero()){RetireActive();if(original!=requested){original=requested;originalCounter=counter;}}
        else if(active<0||slots[static_cast<unsigned>(active)].weight!=requested) {
            for(unsigned i=0;i<slots.size();i++)if(slots[i].phase==Phase::Ready&&slots[i].token.request==request) {
                RetireActive();active=static_cast<int>(i);slots[i].phase=Phase::Active;slots[i].publicationCounter=counter;break;
            }
        }
        Reclaim();openCounter=counter;frameOpen=true;return true;
    }
    bool Join(std::uint64_t counter) {
        std::lock_guard lock(gate);
        if(!live)return false;
        if(!frameOpen||counter!=openCounter){Disable();return false;}
        for(auto& s:slots)if(s.phase==Phase::Retiring&&!s.usePending&&
            counter>=s.retirementFence&&s.coverageFence>=s.retirementFence)s.joinedAfterUse=true;
        Reclaim();return true;
    }
    std::optional<unsigned> Input(std::uint64_t before,std::uint64_t after) {
        std::lock_guard lock(gate);
        if(!live)return {};
        if(!frameOpen||before!=after||before!=openCounter){Disable();return {};}
        if(active<0)return {};
        auto& s=slots[static_cast<unsigned>(active)];s.usePending=true;s.joinedAfterUse=false;
        return static_cast<unsigned>(active);
    }
    bool Signal(std::uint64_t value,bool success,bool covered) {
        std::lock_guard lock(gate);
        if(!live)return false;
        if(!frameOpen||!success||!covered||value==UINT64_MAX||value!=openCounter+1){Disable();return false;}
        if(active>=0){auto& s=slots[static_cast<unsigned>(active)];if(s.usePending){s.lastUseSignal=value;s.usePending=false;}}
        for(auto& s:slots)if(s.phase==Phase::Retiring)s.coverageFence=value;
        lastSignal=value;frameOpen=false;return true;
    }
    void Invalidate(){std::lock_guard lock(gate);Disable();}
    View Get(){std::lock_guard lock(gate);return {live,frameOpen,active,requested,active<0?original:slots[static_cast<unsigned>(active)].weight,epoch,request,lastSignal,active<0?originalCounter:slots[static_cast<unsigned>(active)].publicationCounter,slots};}
};
}
