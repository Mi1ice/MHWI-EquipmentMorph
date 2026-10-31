#pragma once
#include "../../tools/fixtures/vertex_declaration_memory.hpp"
namespace vertexdeclarationtests {
struct PrefixMemory {
    std::vector<unsigned char> root=std::vector<unsigned char>(9496),metadata=std::vector<unsigned char>(260);
    std::vector<uintptr_t> records,natives;uintptr_t pointer=0;mhm::declarations::Snapshot snapshot;
    explicit PrefixMemory(uint32_t count):records(count,uintptr_t{0x123000}),natives(count,uintptr_t{0x456000}){
        using inputfasttest::Put;using inputfasttest::Address;
        pointer=Address(root);Put(root,56,Address(metadata));Put(root,9312,Address(natives));Put(root,9492,count);
        Put(metadata,200,Address(records));Put(metadata,256,count);
        snapshot.slot=reinterpret_cast<uintptr_t>(&pointer);
        snapshot.header={pointer,Address(metadata),Address(records),Address(natives),count,count};
        snapshot.records=records;snapshot.natives=natives;
    }
};
inline unsigned PrefixSafety(){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);++count;};
    auto read=[](uintptr_t address,void* output,size_t bytes){return observer::Read(address,output,bytes);};
    // A saved snapshot must describe exactly the prefix filled by each read.
    for(unsigned which=0;which<2;which++)for(unsigned shape=0;shape<3;shape++){
        PrefixMemory m(2);auto& saved=which?m.snapshot.natives:m.snapshot.records;
        if(shape==0)saved.clear();else if(shape==1)saved.pop_back();else saved.push_back(0);
        check(!m.snapshot.Stable(read),"declaration scratch refuses empty, short or long saved vectors");
    }
    for(uint32_t n:{0u,1u,4096u,4097u}){
        PrefixMemory m(n);check(m.snapshot.Stable(read)==(n>=1&&n<=4096),"declaration scratch obeys count boundaries");
    }
    {
        PrefixMemory m(4096);m.snapshot.records.push_back(0);
        check(!m.snapshot.Stable(read),"declaration scratch refuses saved vector beyond capacity before comparing");
    }
    for(unsigned failed:{7u,8u})for(bool partial:{false,true}){
        PrefixMemory m(2);unsigned calls=0;
        auto fail=[&](uintptr_t address,void* output,size_t bytes){
            if(++calls==failed){if(partial)std::memcpy(output,reinterpret_cast<void*>(address),bytes/2);return false;}
            return observer::Read(address,output,bytes);
        };
        check(!m.snapshot.Stable(fail)&&calls==failed,"declaration scratch short-circuits failed or partially filled pointer arrays");
    }
    for(unsigned which=0;which<2;which++){
        PrefixMemory m(2);unsigned calls=0;
        auto mutate=[&](uintptr_t address,void* output,size_t bytes){
            const bool ok=observer::Read(address,output,bytes);
            if(++calls==(which?7u:6u))(which?m.natives:m.records)[0]^=1;
            return ok;
        };
        check(!m.snapshot.Stable(mutate),"declaration scratch still detects changed pointer arrays");
    }
    for(unsigned which=0;which<2;which++){
        PrefixMemory m(2);unsigned calls=0;
        auto mutate=[&](uintptr_t address,void* output,size_t bytes){
            const bool ok=observer::Read(address,output,bytes);
            if(++calls==8){if(which)inputfasttest::Put(m.metadata,256,1u);else inputfasttest::Put(m.root,56,uintptr_t{0});}
            return ok;
        };
        check(!m.snapshot.Stable(mutate),"declaration scratch retains final header and generation checks");
    }
    return count;
}
inline unsigned Run(){
    unsigned count=PrefixSafety();auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
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
