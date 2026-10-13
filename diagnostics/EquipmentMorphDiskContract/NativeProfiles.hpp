#pragma once
#include <array>
#include <string>
#include "../../runtime/EquipmentMorphDiskConfiguration/loader.hpp"
namespace equipmentnative {
struct Profile {
    bool configured=false;unsigned part=0,vertexBytes=0,meshCount=0,modelBytes=0;
    std::size_t ownerField=0;std::string path,modelSha,meshSha;
    bool diskContract=false;std::vector<uint32_t> diskFormats;
};
inline constexpr std::array<std::size_t,5> routes={0x70,0,0x170,0x1F0,0x270};
// Catalogue is immutable; the worker changes active profiles under the adapter gate.
inline mhm::configuration::Collection catalogue;
inline std::array<Profile,5> profiles{};
inline Profile Active(unsigned part,const mhm::configuration::Loaded* loaded){
    Profile p;p.part=part;p.ownerField=routes[part];
    if(loaded){const auto& input=loaded->profile;p.configured=true;p.vertexBytes=input.vertexBytes;
        p.meshCount=input.meshCount;p.modelBytes=input.modelBytes;p.modelSha=input.modelSha;p.meshSha=input.meshSha;
        p.diskContract=true;p.diskFormats=input.diskFormats;p.path=mhm::configuration::Fold(input.model);
        std::replace(p.path.begin(),p.path.end(),'/','\\');}
    return p;
}
inline void Initialize(const mhm::configuration::Collection& loaded){
    catalogue=loaded;
    for(unsigned i=0;i<5;i++)profiles[i]=Active(i,nullptr);
}
}
