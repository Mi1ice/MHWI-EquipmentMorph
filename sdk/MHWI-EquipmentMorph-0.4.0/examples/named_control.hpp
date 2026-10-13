#pragma once
#include "../include/equipment_morph_client.hpp"
#include "../include/equipment_morph_key_client.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace mhwi_example {
template<class Client>MhmResult ConnectLoaded(Client& client){
    HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"MHWEquipmentMorphConfiguredPrototype.dll",&module))return MHM_NOT_READY;
    using Getter=MhmResult(MHM_CALL*)(uint32_t,uint32_t,MhmApi*);Getter getter=nullptr;
    const auto address=GetProcAddress(module,"MhwMorphGetApi");static_assert(sizeof(getter)==sizeof(address));std::memcpy(&getter,&address,sizeof(getter));
    if(!getter)return MHM_VERSION;MhmApi api{};const auto result=getter(MHM_API_VERSION,sizeof(api),&api);return result==MHM_OK?client.Connect(api):result;
}
struct PartRequest {uint32_t part;std::string model;std::vector<std::pair<std::string,float>> keys;};
// Full-part ownership: every discovered key of each selected part is explicitly included.
inline MhmResult ApplyFullParts(morphpartclient::Client& client,const std::vector<PartRequest>& requests,uint64_t& revision){
    const auto result=client.Refresh();if(result!=MHM_OK)return result;
    std::vector<morphpartclient::PartValues> changes;
    for(const auto& part:requests){
        const auto& specs=client.Specifications();const auto found=std::find_if(specs.begin(),specs.end(),[&](const auto& s){return s.part==part.part&&s.model==part.model;});
        if(found==specs.end())return MHM_NOT_READY;
        morphpartclient::PartValues values{part.part,std::vector<float>(found->ids.size(),0)};std::vector<std::string> used;
        for(const auto& [id,weight]:part.keys){
            if(std::find(used.begin(),used.end(),id)!=used.end())return MHM_INVALID;
            const auto key=std::find(found->ids.begin(),found->ids.end(),id);if(key==found->ids.end())return MHM_NOT_READY;
            used.push_back(id);values.weights[static_cast<size_t>(key-found->ids.begin())]=weight;
        }changes.push_back(std::move(values));
    }return client.Set(changes,revision);
}
}
