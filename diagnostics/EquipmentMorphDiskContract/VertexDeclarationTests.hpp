#pragma once
#include "../../tools/fixtures/vertex_declaration_memory.hpp"
namespace vertexdeclarationtests {
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
    inputfasttest::Fixture f;captureddeclarations::Memory m;using inputfasttest::Put;using inputfasttest::Address;
    auto& profile=equipmentnative::profiles[MHM_BODY];profile.diskContract=true;profile.diskFormats={0xf637401c};
    Put(f.mesh,2,uint16_t{2});f.mesh[14]=32;Put(f.mesh,24,4u);
    auto normalized=f.mesh;mhm::meshcontract::Normalize(normalized,true);profile.meshSha=mhm::pack::Digest(normalized);
    Put(f.image,observed::declarationRootRva,Address(m.root));const auto id=f.Adapter().Target();
    check(facecontinuous::SlotMatches(MHM_BODY,id,true),"production slot accepts captured declaration semantics and disk digest");
    check(facecontinuous::SlotMatches(MHM_BODY,id,false),"production slot rechecks copied declarations before use");
    Put(f.bridge,0x60,uintptr_t{0});
    check(!facecontinuous::SlotMatches(MHM_BODY,id,false)&&!facecontinuous::declarationBindings[MHM_BODY].snapshot.slot,"early player identity failure clears declaration cache");
    Put(f.bridge,0x60,id.player);check(facecontinuous::SlotMatches(MHM_BODY,id,true),"restored player must receive a fresh full declaration proof");
    Put(m.native[4],76,0xf471fe45u);
    check(!facecontinuous::SlotMatches(MHM_BODY,id,false)&&!facecontinuous::declarationBindings[MHM_BODY].snapshot.slot,"in-place declaration mutation clears binding and refuses publication");
    Put(m.native[4],76,0xf637401cu);check(facecontinuous::SlotMatches(MHM_BODY,id,true),"fresh generation can bind after complete revalidation");
    Put(m.array,4*8,Address(m.source[5]));
    check(!facecontinuous::SlotMatches(MHM_BODY,id,false),"production slot refuses changed declaration pointer array");
    Put(m.array,4*8,Address(m.source[4]));check(facecontinuous::SlotMatches(MHM_BODY,id,true),"restored full snapshot revalidates");
    batchedreadtest::watched=Address(f.player)+0x2A0;batchedreadtest::changed=Address(m.root)+56;batchedreadtest::replacement=0;
    inputfasttest::afterRead=batchedreadtest::ReplaceDuringRead;
    const bool refused=!facecontinuous::SlotMatches(MHM_BODY,id,true);inputfasttest::afterRead=nullptr;
    check(refused&&!facecontinuous::declarationBindings[MHM_BODY].snapshot.slot,"table switch at final identity read cannot publish or retain stale cache");
    Put(m.root,56,Address(m.metadata));check(facecontinuous::SlotMatches(MHM_BODY,id,true),"new full snapshot after switch refusal");
    Put(f.mesh,24,5u);check(!facecontinuous::SlotMatches(MHM_BODY,id,true),"production row wrong format cannot hide behind normalized digest");
    Put(f.mesh,24,4u);Put(f.mesh,20,1u);check(!facecontinuous::SlotMatches(MHM_BODY,id,true),"original normalized row identity remains guarded");
    facecontinuous::declarationBindings[MHM_BODY]={};
    }
    for(unsigned kind:{1u,2u,3u}){
        inputfasttest::Fixture f;captureddeclarations::Memory m;using inputfasttest::Put;using inputfasttest::Address;
        auto& profile=equipmentnative::profiles[MHM_BODY];profile.diskContract=true;profile.diskFormats={0xf637401c};
        Put(f.mesh,2,uint16_t{2});f.mesh[14]=32;Put(f.mesh,24,4u);
        auto normalized=f.mesh;mhm::meshcontract::Normalize(normalized,true);profile.meshSha=mhm::pack::Digest(normalized);
        Put(f.image,observed::declarationRootRva,Address(m.root));
        check(facecontinuous::SlotMatches(MHM_BODY,f.Adapter().Target(),true),"input callback fixture validates declarations before launch");
        f.Request(.6f,1);f.Queue();inputfasttest::Need(f.Adapter().Completed(20,100));f.Cycle();
        f.Input(kind,true);Put(m.native[4],76,0xf471fe45u);
        auto context=f.Context(kind);
        if(kind==3)Put(f.context,0x710,Address(f.wrappers[0]));
        facecontinuous::Input(context,kind);
        check(!f.Adapter().Live()&&!facecontinuous::declarationBindings[MHM_BODY].snapshot.slot
            &&context.r8==(kind==3?7:Address(f.source)),"successful compute/draw/dispatch path rejects declaration changes after launch");
    }
    return count;
}
}
