#pragma once
#include "../runtime/EquipmentMorphPerKeyCore/asset_pack.hpp"
namespace c07bench {
inline mhm::Asset Expand(const mhm::Asset& source,unsigned count){
    if(count<source.keys.size()||count>1024||source.keys.empty())throw std::runtime_error("synthetic key bounds");
    auto a=source;for(unsigned i=unsigned(source.keys.size());i<count;i++){
        auto key=source.keys[(i-source.keys.size())%source.keys.size()];
        std::snprintf(key.info.id,sizeof(key.info.id),"c07_synthetic_%04u",i);a.keys.push_back(std::move(key));
    }
    if(!mhm::Validate(a))throw std::runtime_error("expanded asset fails production validation");return a;
}
inline uint64_t Logical(const mhm::Asset& a){
    uint64_t bytes=sizeof(a)+a.basis.size()+a.model.size()+a.keys.size()*sizeof(mhm::Key)+a.rotations.size()*sizeof(MhmRotation);
    for(const auto& k:a.keys)bytes+=(k.deltas.size()+k.normals.size())*sizeof(MhmDelta);return bytes;
}
inline std::vector<float> Weights(const mhm::Asset& a,unsigned active,bool dynamic,unsigned step){
    std::vector<float> w(a.keys.size());unsigned chosen=0;
    for(unsigned i=0;i<a.keys.size()&&chosen<active;i++)if(!a.keys[i].deltas.empty()&&!a.keys[i].info.exclusive_group){
        w[i]=.1f;if(dynamic&&chosen>=active-std::min(5u,active)){const auto phase=double(step%60)/30.;w[i]=float(.05+.1*(phase<=1?phase:2-phase));}++chosen;
    }
    if(chosen!=active)throw std::runtime_error("not enough nonempty nonexclusive keys");return w;
}
}
