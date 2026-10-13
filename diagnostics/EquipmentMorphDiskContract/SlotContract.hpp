#pragma once
// Controller owner fields are checked against the same player's backlink pair.
// Unconfigured slots have no capture or publication path.
namespace facecontinuous {
inline bool PlayerAnchorMatches(std::uintptr_t player) noexcept {
    std::uintptr_t table=0,bridge=0,pointer=0;
    if(!observer::Read(player,table)||table!=observer::imageBase+observed::ownerVtableRva
       ||!observer::Read(player+0x126C8,bridge)||!observer::Read(bridge,table)||table!=observer::imageBase+0x324A830
       ||!observer::Read(bridge+0x60,pointer)||pointer!=player
       ||!observer::Read(bridge+0xF0,pointer)||pointer!=player)return false;
    for(unsigned slot:{0u,25u,38u}){
        const auto expected=slot==0?facelife::destructorRva:slot==25?facelife::removeRva:facelife::modelRva;
        if(!observer::Read(observer::imageBase+observed::ownerVtableRva+slot*8,pointer)
                ||pointer!=observer::imageBase+expected)return false;
    }
    return observer::Read(player+0x126C8,pointer)&&pointer==bridge;
}
inline bool SlotMatches(unsigned index,Identity id,bool wholeMesh){
    if(index>=equipmentnative::profiles.size()||!equipmentnative::profiles[index].configured
       ||!id.Valid()||!PlayerAnchorMatches(id.player))return false;
    const auto& profile=equipmentnative::profiles[index];
    std::uintptr_t bridge=0,table=0,pointer=0,mesh=0,backing=0,resource=0;
    unsigned count=0,bytes=0,mode=0,refs=0;std::array<char,80> path{};
    if(!observer::Read(id.player+0x126C8,bridge))return false;
    if(index==MHM_BODY){
        if(id.owner!=id.player||!observer::Read(bridge+0x480,pointer)||pointer!=id.model)return false;
    }else if(!observer::Read(bridge+profile.ownerField,pointer)||pointer!=id.owner)return false;
    const auto ownerVtable=index==MHM_BODY?observed::ownerVtableRva:0x34A7560;
    if(!observer::Read(id.owner,table)||table!=observer::imageBase+ownerVtable
       ||!observer::Read(id.owner+0x2A0,pointer)||pointer!=id.model
       ||!observer::Read(id.model,table)||table!=observer::imageBase+observed::modelVtableRva
       ||!observer::Read(id.model+0xC,path)||std::memcmp(path.data(),profile.path.c_str(),profile.path.size()+1)!=0
       ||!observer::Read(id.model+0xD0,count)||count!=profile.meshCount
       ||!observer::Read(id.model+0x100,bytes)||bytes!=profile.vertexBytes
       ||!observer::Read(id.model+0x110,pointer)||pointer!=id.source
       ||!observer::Read(id.source,table)||table!=observer::imageBase+observed::vertexVtableRva
       ||!observer::Read(id.source+0x5C,bytes)||bytes!=profile.vertexBytes
       ||!observer::Read(id.source+0x58,mode)||mode!=1||!observer::Read(id.source+0x48,refs)||!refs
       ||!observer::Read(id.source+0x60,backing)||!observer::Read(backing,table)||table!=observer::imageBase+0x3539948
       ||!observer::Read(backing+0x20,resource)||resource<=0xFFFF)return false;
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
