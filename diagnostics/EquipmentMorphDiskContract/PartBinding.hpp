#pragma once
#include <set>
namespace equipmentmorph {
struct PartBinding {
    std::uint64_t target=0,lastAck=0;MhmApi api{};std::set<std::uint64_t> pending;
    ~PartBinding(){for(auto frame:pending)MhwMorphHostReleaseFrame(frame);if(target)MhwMorphHostDetach(target);}
    void Attach(const mhm::Asset& asset){
        if(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)!=MHM_OK||Core().Attach(asset,target)!=MHM_OK)throw std::runtime_error("Part attach");
    }
    bool Requested(unsigned count,std::vector<float>& values,std::uint64_t& revision){
        std::vector<MhmWeightState> state(count);std::uint32_t needed=0;std::uint64_t applied=0;
        const auto result=api.read_weights(target,state.data(),count,&needed,&revision,&applied);
        if(result==MHM_STALE)return false;if(result!=MHM_OK||needed!=count)throw std::runtime_error("Part read");
        values.clear();for(const auto& w:state)values.push_back(w.requested);return true;
    }
    bool Capture(std::span<const float> values,std::uint64_t revision,launchpool::MorphVersion& version){
        std::uint64_t frame=0;const auto result=MhwMorphHostCaptureInterpolated(target,values.data(),static_cast<std::uint32_t>(values.size()),revision,&frame);
        if(result==MHM_STALE||result==MHM_CAPACITY)return false;if(result!=MHM_OK)throw std::runtime_error("Part capture");
        try{pending.insert(frame);}catch(...){MhwMorphHostReleaseFrame(frame);throw;}
        version={};std::copy(values.begin(),values.end(),version.weights.begin());version.frame=frame;version.revision=revision;return true;
    }
    void Acknowledge(const launchpool::View& view){
        const auto frame=view.applied.frame;
        if(frame&&frame>lastAck&&view.lastSignal>view.publicationCounter){
            if(!pending.contains(frame)||MhwMorphHostAcknowledge(frame)!=MHM_OK)throw std::runtime_error("Part acknowledgement");lastAck=frame;
        }
    }
    void Collect(const launchpool::View& view){
        for(auto it=pending.begin();it!=pending.end();){
            const auto frame=*it;bool needed=frame!=lastAck&&(view.requested.frame==frame||view.applied.frame==frame);
            for(const auto& s:view.slots)if(s.weight.frame==frame&&(s.phase==launchpool::Phase::Preparing||s.phase==launchpool::Phase::AwaitingStamp||s.phase==launchpool::Phase::Uploading||s.phase==launchpool::Phase::Ready))needed=true;
            if(frame<=lastAck)needed=false;
            if(needed)++it;else{if(MhwMorphHostReleaseFrame(frame)!=MHM_OK)throw std::runtime_error("Part release");it=pending.erase(it);}
        }
    }
};
}
