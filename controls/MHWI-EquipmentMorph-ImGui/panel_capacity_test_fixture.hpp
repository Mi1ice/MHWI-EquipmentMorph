#pragma once
#define NOMINMAX
#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <string>
namespace paneltest {
inline unsigned checks=0;
inline void Check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;}
inline uint64_t Attach(uint32_t part,uint32_t count,bool exclusive=false){
    mhm::Asset asset;asset.part=part;asset.model="synthetic/part"+std::to_string(part);asset.basis.resize(64);
    for(uint32_t i=0;i<count;++i){mhm::Key key;const auto id="Key_"+std::to_string(i);strcpy_s(key.info.id,id.c_str());key.info.maximum=1;key.info.exclusive_group=exclusive?1u:0u;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);}
    uint64_t handle=0;Check(Core().Attach(std::move(asset),handle)==MHM_OK,"synthetic attach");return handle;
}
inline MhmApi Api(){MhmApi api{};Check(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK,"public API");return api;}
inline mhmcontrol::Request Request(mhmcontrol::Action action,uint64_t handle,uint32_t key,float value=0){mhmcontrol::Request r;r.action=action;r.target=handle;r.key=key;r.weight=value;return r;}
}
