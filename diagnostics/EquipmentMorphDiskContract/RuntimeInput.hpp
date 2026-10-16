#pragma once
namespace facecontinuous {
// Runtime facts are validated once per callback. The full observer Snapshot
// remains available for diagnostics; no proof is cached across callbacks.
struct InputFacts {
    unsigned part=5,auxEnabled=0;
    std::uintptr_t owner=0,model=0,input=0,originalInput=0;
    std::array<std::uintptr_t,3> ownerStreams{};
};
inline bool ReadInputFacts(const safetyhook::Context& context,unsigned kind,std::uintptr_t player,InputFacts& facts) noexcept {
    if(kind<1||kind>3)return false;
    std::uintptr_t contextWord=0,bridge=0;
    // Retain a readable native binding context without walking its diagnostic
    // child objects or collecting flags not used by input replacement.
    if(!observer::Read(context.rcx,contextWord)||!PlayerAnchorMatches(player,&bridge))return false;
    facts.owner=kind==1?context.rdi:context.r15;facts.input=context.r8;
    if(kind==3){
        unsigned binding=0;
        if(!observer::Read(context.rbp+0xD0,facts.owner)||!observer::Read(observer::imageBase+observed::sourceBindingIdRva,binding)
           ||binding==UINT32_MAX||!observer::Read(context.rcx+0x710+(binding&0xFFF)*8,facts.input))return false;
    }
    const auto mesh=kind==2?context.rsi:context.r13;
    observer::ReadBlock<0x2A8> owner;observer::ReadBlock<0x118> model;
    if(!owner.Load(facts.owner))return false;facts.model=owner.Value<std::uintptr_t,0x2A0>();
    if(!model.Load(facts.model)||model.Value<std::uintptr_t,0>()!=observer::imageBase+observed::modelVtableRva)return false;
    const auto table=owner.Value<std::uintptr_t,0>();const auto path=model.Value<std::array<char,80>,0xC>();
    facts.part=5;
    for(const auto& profile:equipmentnative::profiles){
        const auto expected=profile.part==MHM_BODY?observed::ownerVtableRva:0x34A7560;
        if(profile.configured&&table==observer::imageBase+expected&&profile.path.size()<path.size()
           &&std::memcmp(path.data(),profile.path.c_str(),profile.path.size()+1)==0){facts.part=profile.part;break;}
    }
    if(facts.part>=equipmentnative::profiles.size())return false;const auto& profile=equipmentnative::profiles[facts.part];
    const bool body=facts.part==MHM_BODY;std::uintptr_t routed=0;
    if((body&&facts.owner!=player)||!observer::Read(bridge+(body?0x480:profile.ownerField),routed)
       ||routed!=(body?facts.model:facts.owner))return false;
    const auto bytes=model.Value<unsigned,0x100>(),count=model.Value<unsigned,0xD0>();
    const auto meshBase=model.Value<std::uintptr_t,0xC8>();facts.originalInput=model.Value<std::uintptr_t,0x110>();
    if(bytes!=profile.vertexBytes||count!=profile.meshCount||mesh<meshBase||(mesh-meshBase)%80!=0||(mesh-meshBase)/80>=count)return false;
    std::array<unsigned char,80> meshBytes{};if(!observer::Read(mesh,meshBytes))return false;
    observer::ReadBlock<0x68> source;observer::ReadBlock<0x28> backing;
    if(!source.Load(facts.originalInput)||source.Value<std::uintptr_t,0>()!=observer::imageBase+observed::vertexVtableRva
       ||source.Value<unsigned,0x5C>()!=bytes||source.Value<unsigned,0x58>()!=1||!source.Value<unsigned,0x48>()
       ||!backing.Load(source.Value<std::uintptr_t,0x60>())||backing.Value<std::uintptr_t,0>()!=observer::imageBase+0x3539948
       ||backing.Value<std::uintptr_t,0x20>()<=0xFFFF)return false;
    if(facts.input!=facts.originalInput){
        observer::ReadBlock<0x68> input;
        if(!input.Load(facts.input)||input.Value<std::uintptr_t,0>()!=observer::imageBase+observed::vertexVtableRva
           ||input.Value<unsigned,0x5C>()!=bytes)return false;
    }
    if(kind==2){unsigned char enabled=0;if(!observer::Read(context.rdi+0x87,enabled))return false;facts.auxEnabled=enabled;}
    // Keep the late stream read and repeated player/route/model/source proof.
    // These fields may change after the initial headers were captured.
    std::uintptr_t afterBridge=0,after=0;
    return observer::Read(facts.owner+0x430,facts.ownerStreams)
        &&PlayerAnchorMatches(player,&afterBridge)&&afterBridge==bridge
        &&observer::Read(bridge+(body?0x480:profile.ownerField),after)&&after==(body?facts.model:facts.owner)
        &&observer::Read(facts.owner+0x2A0,after)&&after==facts.model
        &&observer::Read(facts.model+0x110,after)&&after==facts.originalInput;
}
}
