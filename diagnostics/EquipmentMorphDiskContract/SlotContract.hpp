#pragma once
// Controller owner fields are checked against the same player's backlink pair.
// Unconfigured slots have no capture or publication path.
namespace facecontinuous {
inline bool PlayerAnchorMatches(std::uintptr_t player,std::uintptr_t* validatedBridge=nullptr) noexcept {
    std::uintptr_t table=0,bridge=0,pointer=0;
    observer::ReadBlock<0x98> links;observer::ReadBlock<39*8> methods;
    if(!observer::Read(player,table)||table!=observer::imageBase+observed::ownerVtableRva
       ||!observer::Read(player+0x126C8,bridge)||!observer::Read(bridge,table)||table!=observer::imageBase+0x324A830
       ||!links.Load(bridge+0x60)||links.Value<std::uintptr_t,0>()!=player||links.Value<std::uintptr_t,0x90>()!=player
       ||!methods.Load(observer::imageBase+observed::ownerVtableRva))return false;
    if(methods.Value<std::uintptr_t,0>()!=observer::imageBase+facelife::destructorRva
       ||methods.Value<std::uintptr_t,25*8>()!=observer::imageBase+facelife::removeRva
       ||methods.Value<std::uintptr_t,38*8>()!=observer::imageBase+facelife::modelRva)return false;
    if(!observer::Read(player+0x126C8,pointer)||pointer!=bridge)return false;
    if(validatedBridge)*validatedBridge=bridge;return true;
}
inline bool SlotMatches(unsigned index,Identity id,bool wholeMesh){
    if(index>=equipmentnative::profiles.size()||!equipmentnative::profiles[index].configured
       ||!id.Valid()||!PlayerAnchorMatches(id.player))return false;
    const auto& profile=equipmentnative::profiles[index];
    std::uintptr_t bridge=0,pointer=0,mesh=0;unsigned count=0;
    if(!observer::Read(id.player+0x126C8,bridge))return false;
    if(index==MHM_BODY){
        if(id.owner!=id.player||!observer::Read(bridge+0x480,pointer)||pointer!=id.model)return false;
    }else if(!observer::Read(bridge+profile.ownerField,pointer)||pointer!=id.owner)return false;
    const auto ownerVtable=index==MHM_BODY?observed::ownerVtableRva:0x34A7560;
    observer::ReadBlock<0x2A8> owner;observer::ReadBlock<0x118> model;
    observer::ReadBlock<0x68> source;observer::ReadBlock<0x28> backing;
    if(!owner.Load(id.owner)||owner.Value<std::uintptr_t,0>()!=observer::imageBase+ownerVtable
       ||owner.Value<std::uintptr_t,0x2A0>()!=id.model||!model.Load(id.model)
       ||model.Value<std::uintptr_t,0>()!=observer::imageBase+observed::modelVtableRva)return false;
    const auto path=model.Value<std::array<char,80>,0xC>();count=model.Value<unsigned,0xD0>();
    if(std::memcmp(path.data(),profile.path.c_str(),profile.path.size()+1)!=0||count!=profile.meshCount
       ||model.Value<unsigned,0x100>()!=profile.vertexBytes||model.Value<std::uintptr_t,0x110>()!=id.source
       ||!source.Load(id.source)||source.Value<std::uintptr_t,0>()!=observer::imageBase+observed::vertexVtableRva
       ||source.Value<unsigned,0x5C>()!=profile.vertexBytes||source.Value<unsigned,0x58>()!=1||!source.Value<unsigned,0x48>()
       ||!backing.Load(source.Value<std::uintptr_t,0x60>())||backing.Value<std::uintptr_t,0>()!=observer::imageBase+0x3539948
       ||backing.Value<std::uintptr_t,0x20>()<=0xFFFF)return false;
    // Streams can change independently of model identity; retain their late
    // fresh read rather than using an earlier owner-header snapshot.
    std::array<std::uintptr_t,3> streams{};
    if(!observer::Read(id.owner+0x430,streams)||std::any_of(streams.begin(),streams.end(),[](auto p){return p!=0;}))return false;
    if(wholeMesh){
        if(!count||count>4096||!observer::Read(id.model+0xC8,mesh))return false;
        std::vector<unsigned char> rows(count*80);if(!observer::Read(mesh,rows.data(),rows.size()))return false;
        if(profile.diskContract&&!mhm::meshcontract::FormatsMatch(rows,profile.diskFormats))return false;
        if(!mhm::meshcontract::Normalize(rows,profile.diskContract||index!=MHM_BODY))return false;
        if(privateprobe::Digest(rows.data(),rows.size())!=profile.meshSha)return false;
    }
    if(!PlayerAnchorMatches(id.player)||!observer::Read(id.player+0x126C8,pointer)||pointer!=bridge)return false;
    if(index==MHM_BODY){if(!observer::Read(bridge+0x480,pointer)||pointer!=id.model)return false;}
    else if(!observer::Read(bridge+profile.ownerField,pointer)||pointer!=id.owner)return false;
    return observer::Read(id.owner+0x2A0,pointer)&&pointer==id.model
        &&observer::Read(id.model+0x110,pointer)&&pointer==id.source;
}
}
