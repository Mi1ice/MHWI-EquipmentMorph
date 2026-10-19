#pragma once
namespace runtimeinputtest {
using inputfasttest::Address;
using inputfasttest::Put;
using inputfasttest::Need;
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        check(f.Input(1,true)<=40,"runtime compute validates once with at most forty reads");
        check(f.Input(2,true)<=41,"runtime draw validates once with at most forty-one reads");
        check(f.Input(3,true)<=43,"runtime dispatch validates once with at most forty-three reads");
    }
    {
        inputfasttest::Fixture f;
        const auto valid=[&]{facecontinuous::InputFacts facts{};
            return facecontinuous::ReadInputFacts(f.Context(1),1,Address(f.player),facts)
                &&!facts.auxEnabled&&std::all_of(facts.ownerStreams.begin(),facts.ownerStreams.end(),[](auto value){return value==0;});};
        Need(valid());
        auto mutate=[&](auto& bytes,size_t at,auto value,const char* why){
            using T=decltype(value);T old{};std::memcpy(&old,bytes.data()+at,sizeof(old));Put(bytes,at,value);
            const bool refused=!valid();Put(bytes,at,old);check(refused,why);
        };
        mutate(f.player,0,std::uintptr_t{0},"runtime player vtable remains guarded");
        mutate(f.player,0x126C8,std::uintptr_t{0},"runtime player bridge remains guarded");
        mutate(f.bridge,0,std::uintptr_t{0},"runtime bridge vtable remains guarded");
        mutate(f.bridge,0x60,std::uintptr_t{0},"runtime first player backlink remains guarded");
        mutate(f.bridge,0xF0,std::uintptr_t{0},"runtime second player backlink remains guarded");
        mutate(f.bridge,0x480,std::uintptr_t{0},"runtime cached body model remains guarded");
        for(unsigned slot:{0u,25u,38u})mutate(f.image,observed::ownerVtableRva+slot*8,std::uintptr_t{0},"runtime lifecycle vtable remains guarded");
        mutate(f.player,0x2A0,std::uintptr_t{0},"runtime owner model remains guarded");
        mutate(f.model,0,std::uintptr_t{0},"runtime model vtable remains guarded");
        mutate(f.model,0xC,char{'X'},"runtime profile path remains byte exact");
        mutate(f.model,0x100,65u,"runtime model size remains guarded");
        mutate(f.model,0xD0,2u,"runtime mesh count remains guarded");
        mutate(f.model,0x110,std::uintptr_t{0},"runtime source identity remains guarded");
        mutate(f.source,0,std::uintptr_t{0},"runtime source vtable remains guarded");
        mutate(f.source,0x5C,65u,"runtime source size remains guarded");
        mutate(f.source,0x58,0u,"runtime original source mode remains guarded");
        mutate(f.source,0x48,0u,"runtime original source references remain guarded");
        mutate(f.source,0x60,std::uintptr_t{0},"runtime source backing remains guarded");
        mutate(f.backing,0,std::uintptr_t{0},"runtime source backing vtable remains guarded");
        mutate(f.backing,0x20,std::uintptr_t{0},"runtime source resource remains guarded");
        mutate(f.player,0x430,std::uintptr_t{1},"runtime owner streams remain guarded");
        batchedreadtest::watched=Address(f.model);batchedreadtest::changed=Address(f.player)+0x2A0;
        inputfasttest::afterRead=batchedreadtest::ReplaceDuringRead;
        const bool modelRefused=!valid();inputfasttest::afterRead=nullptr;Put(f.player,0x2A0,Address(f.model));
        check(modelRefused,"runtime final owner reread detects model replacement during capture");
        batchedreadtest::changed=Address(f.player)+0x126C8;inputfasttest::afterRead=batchedreadtest::ReplaceDuringRead;
        const bool bridgeRefused=!valid();inputfasttest::afterRead=nullptr;Put(f.player,0x126C8,Address(f.bridge));
        check(bridgeRefused,"runtime final player proof detects changed bridge during capture");
        batchedreadtest::changed=Address(f.player)+0x430;batchedreadtest::replacement=1;inputfasttest::afterRead=batchedreadtest::ReplaceDuringRead;
        const bool streamsRefused=!valid();inputfasttest::afterRead=nullptr;batchedreadtest::replacement=0;Put(f.player,0x430,std::uintptr_t{0});
        check(streamsRefused,"runtime late stream read rejects an independently enabled stream");
    }
    {
        inputfasttest::Fixture f;facecontinuous::InputFacts facts{};observer::Event diagnostic{};
        for(unsigned kind:{1u,2u,3u}){
            const auto context=f.Context(kind);Need(observer::Snapshot(context,kind,diagnostic));
            check(facecontinuous::ReadInputFacts(context,kind,Address(f.player),facts)&&facts.part==diagnostic.part
                &&facts.owner==diagnostic.owner&&facts.model==diagnostic.model&&facts.input==diagnostic.input
                &&facts.originalInput==diagnostic.originalInput,"runtime facts agree with the full diagnostic snapshot");
        }
        auto context=f.Context(1);context.r13++;
        check(!facecontinuous::ReadInputFacts(context,1,Address(f.player),facts),"runtime rejects unaligned mesh");context.r13=Address(f.mesh)+80;
        check(!facecontinuous::ReadInputFacts(context,1,Address(f.player),facts),"runtime rejects out-of-range mesh");
        SYSTEM_INFO info{};GetSystemInfo(&info);void* inaccessible=VirtualAlloc(nullptr,info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);Need(inaccessible!=nullptr);
        Put(f.model,0xC8,reinterpret_cast<std::uintptr_t>(inaccessible));context=f.Context(1);context.r13=reinterpret_cast<std::uintptr_t>(inaccessible);
        const bool unreadableMesh=!facecontinuous::ReadInputFacts(context,1,Address(f.player),facts);
        Put(f.model,0xC8,Address(f.mesh));Need(VirtualFree(inaccessible,0,MEM_RELEASE)!=0);
        check(unreadableMesh,"runtime retains mesh readability even without diagnostic mesh output");
        context=f.Context(1);context.rcx=0;
        check(!facecontinuous::ReadInputFacts(context,1,Address(f.player),facts),"runtime retains native context readability");
        Put(f.context,0x1D0,std::uintptr_t{1});
        check(facecontinuous::ReadInputFacts(f.Context(1),1,Address(f.player),facts)&&!observer::Snapshot(f.Context(1),1,diagnostic),
            "runtime skips unused diagnostic child traversal while full diagnostics retain it");Put(f.context,0x1D0,std::uintptr_t{0});
        check(observer::Snapshot(f.Context(1),1,diagnostic)&&diagnostic.qpc>0&&diagnostic.thread==GetCurrentThreadId(),
            "explicit diagnostic capture retains timing and thread data");
        f.draw[0x87]=1;Need(facecontinuous::ReadInputFacts(f.Context(2),2,Address(f.player),facts));
        check(facts.auxEnabled==1,"runtime still captures the unsupported draw-auxiliary flag");
        Put(f.image,observed::sourceBindingIdRva,UINT32_MAX);
        check(!facecontinuous::ReadInputFacts(f.Context(3),3,Address(f.player),facts),"runtime rejects invalid dispatch binding");
    }
    {
        inputfasttest::Fixture f;std::array<unsigned char,0x500> waist{};
        auto& profile=equipmentnative::profiles[MHM_WAIST];profile=equipmentnative::profiles[MHM_BODY];
        profile.part=MHM_WAIST;profile.ownerField=equipmentnative::routes[MHM_WAIST];profile.path="pl\\f_equip\\pl001_0000\\wst\\mod\\f_wst001_0000";
        std::memset(f.model.data()+0xC,0,80);std::memcpy(f.model.data()+0xC,profile.path.c_str(),profile.path.size()+1);
        Put(waist,0,observer::imageBase+0x34A7560);Put(waist,0x2A0,Address(f.model));Put(f.bridge,profile.ownerField,Address(waist));Put(f.ownerSlot,0,Address(waist));
        facecontinuous::InputFacts facts{};
        for(unsigned kind:{1u,2u,3u}){
            auto context=f.Context(kind);if(kind==1)context.rdi=Address(waist);context.r15=Address(waist);
            check(facecontinuous::ReadInputFacts(context,kind,Address(f.player),facts)&&facts.part==MHM_WAIST&&facts.owner==Address(waist),
                "runtime non-body path proves the controller route");
        }
        Put(f.bridge,profile.ownerField,std::uintptr_t{0});auto context=f.Context(1);context.rdi=Address(waist);
        check(!facecontinuous::ReadInputFacts(context,1,Address(f.player),facts),"runtime rejects detached non-body owner");
    }
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        Put(f.context,0x710,Address(f.wrappers[0]));facecontinuous::InputFacts facts{};
        check(facecontinuous::ReadInputFacts(f.Context(3),3,Address(f.player),facts)&&facts.input==Address(f.wrappers[0]),
            "runtime dispatch reads the already-published private wrapper");
        Put(f.wrappers[0],0,std::uintptr_t{0});
        check(!facecontinuous::ReadInputFacts(f.Context(3),3,Address(f.player),facts),"runtime dispatch rejects invalid bound wrapper vtable");
        Put(f.wrappers[0],0,observer::imageBase+observed::vertexVtableRva);Put(f.wrappers[0],0x5C,65u);
        check(!facecontinuous::ReadInputFacts(f.Context(3),3,Address(f.player),facts),"runtime dispatch rejects invalid bound wrapper size");
    }
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        Put(f.context,0x710,Address(f.wrappers[1]));auto context=f.Context(3);facecontinuous::Input(context,3);
        check(!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"input_or_dispatch_contract")==0,
            "runtime dispatch still rejects a valid wrapper from the wrong slot");
    }
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        Put(f.backings[0],0x38,std::uintptr_t{1});auto context=f.Context(1);facecontinuous::Input(context,1);
        check(context.r8==Address(f.source)&&!f.Adapter().Live(),"runtime cannot substitute a mapped private buffer");
    }
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        auto model=f.model;Put(f.player,0x2A0,Address(model));Put(f.bridge,0x480,Address(model));
        auto context=f.Context(1);facecontinuous::Input(context,1);
        check(context.r8==Address(f.source)&&!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"equipment_part_identity_changed")==0,
            "a fully valid replacement model still invalidates the old binding generation");
    }
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        auto source=f.source;Put(f.model,0x110,Address(source));
        auto context=f.Context(1);context.r8=Address(source);facecontinuous::Input(context,1);
        check(context.r8==Address(source)&&!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"equipment_part_identity_changed")==0,
            "a fully valid new source still invalidates the old binding generation");
    }
    {
        inputfasttest::Fixture f;const auto identity=f.Adapter().Target();f.EndCycle();
        f.Part().adapter=std::make_unique<facecontinuous::LaunchAdapter>(++f.Part().epoch);Need(f.Adapter().Arm());
        auto context=f.Context(1);context.r8=Address(f.wrappers[0]);facecontinuous::Input(context,1);
        check(!f.Adapter().Target().Valid(),"initial capture refuses private input before binding");
        context=f.Context(1);facecontinuous::Input(context,1);
        check(f.Adapter().Target()==identity&&!f.Adapter().Ready(),"initial capture uses the validated original identity without publishing resources");
    }
    return count;
}
}
