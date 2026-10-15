#pragma once
namespace batchedreadtest {
using inputfasttest::Address;
using inputfasttest::Put;
using inputfasttest::Need;
inline std::uintptr_t watched=0,changed=0,replacement=0;
inline void ReplaceDuringRead(LPCVOID address,SIZE_T){
    if(reinterpret_cast<std::uintptr_t>(address)==watched){
        std::memcpy(reinterpret_cast<void*>(changed),&replacement,sizeof(replacement));
        inputfasttest::afterRead=nullptr;
    }
}
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
        inputfasttest::Fixture f;f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();
        check(f.Input(1,true)<=60,"nonzero compute preserves contracts with at most sixty reads");
        check(f.Input(2,true)<=60,"nonzero draw preserves contracts with at most sixty reads");
        check(f.Input(3,true)<=60,"nonzero dispatch preserves contracts with at most sixty reads");
    }
    {
        inputfasttest::Fixture f;const auto id=f.Adapter().Target();
        const auto slot=[&]{return facecontinuous::SlotMatches(MHM_BODY,id,false);};Need(slot());
        auto mutate=[&](auto& bytes,size_t at,auto value,const char* why){
            using T=decltype(value);T old{};std::memcpy(&old,bytes.data()+at,sizeof(old));Put(bytes,at,value);
            const bool refused=!slot();Put(bytes,at,old);check(refused,why);
        };
        mutate(f.player,0,std::uintptr_t{0},"player vtable remains guarded");
        mutate(f.player,0x126C8,std::uintptr_t{0},"player bridge remains guarded");
        mutate(f.bridge,0,std::uintptr_t{0},"bridge vtable remains guarded");
        mutate(f.bridge,0x60,std::uintptr_t{0},"first bridge backlink remains guarded");
        mutate(f.bridge,0xF0,std::uintptr_t{0},"second bridge backlink remains guarded");
        mutate(f.bridge,0x480,std::uintptr_t{0},"cached body model remains guarded");
        for(unsigned index:{0u,25u,38u})mutate(f.image,observed::ownerVtableRva+index*8,std::uintptr_t{0},"lifecycle vtable entry remains guarded");
        mutate(f.player,0x2A0,std::uintptr_t{0},"owner model remains guarded");
        mutate(f.model,0,std::uintptr_t{0},"model vtable remains guarded");
        mutate(f.model,0xC,char{'X'},"model path remains byte exact");
        mutate(f.model,0x100,65u,"model byte count remains guarded");
        mutate(f.model,0xD0,2u,"model mesh count remains guarded");
        mutate(f.model,0x110,std::uintptr_t{0},"source wrapper identity remains guarded");
        mutate(f.source,0,std::uintptr_t{0},"source vtable remains guarded");
        mutate(f.source,0x5C,65u,"source byte count remains guarded");
        mutate(f.source,0x58,2u,"source mode remains guarded");
        mutate(f.source,0x48,0u,"source reference count remains guarded");
        mutate(f.source,0x60,std::uintptr_t{0},"source backing remains guarded");
        mutate(f.backing,0,std::uintptr_t{0},"source backing vtable remains guarded");
        mutate(f.backing,0x20,std::uintptr_t{0},"source resource remains guarded");
        mutate(f.player,0x430,std::uintptr_t{1},"owner streams remain guarded");
        watched=Address(f.model);changed=Address(f.player)+0x2A0;inputfasttest::afterRead=ReplaceDuringRead;
        const bool refused=!slot();inputfasttest::afterRead=nullptr;Put(f.player,0x2A0,Address(f.model));
        check(refused,"identity reread rejects replacement during a successful model read");
        watched=Address(f.model);changed=Address(f.player)+0x430;replacement=1;inputfasttest::afterRead=ReplaceDuringRead;
        const bool streamsRefused=!slot();inputfasttest::afterRead=nullptr;replacement=0;Put(f.player,0x430,std::uintptr_t{0});
        check(streamsRefused,"late owner-stream read rejects a stream enabled after model capture");
    }
    {
        inputfasttest::Fixture f;auto& b=f.Part().buffers[0];Need(privateprobe::Contract(b));
        auto mutate=[&](auto& bytes,size_t at,auto value,const char* why){
            using T=decltype(value);T old{};std::memcpy(&old,bytes.data()+at,sizeof(old));Put(bytes,at,value);
            const bool refused=!privateprobe::Contract(b);Put(bytes,at,old);check(refused,why);
        };
        auto& w=f.wrappers[0];auto& backing=f.backings[0];
        mutate(w,0,std::uintptr_t{0},"private wrapper vtable remains guarded");
        mutate(w,0x48,0u,"private reference count remains guarded");
        mutate(w,0x5C,65u,"private byte count remains guarded");
        mutate(w,0x58,1u,"private mode remains guarded");
        mutate(w,0x60,std::uintptr_t{0},"private backing identity remains guarded");
        mutate(w,0x78,b.view+1,"private view remains guarded");
        mutate(backing,0,std::uintptr_t{0},"private backing vtable remains guarded");
        mutate(backing,0x10,65u,"backing byte count remains guarded");
        mutate(backing,0x14,1u,"backing mode remains guarded");
        mutate(backing,0x20,std::uintptr_t{0},"private resource remains guarded");
        mutate(backing,0x28,std::uintptr_t{1},"private pool offset remains guarded");
        mutate(backing,0x38,std::uintptr_t{1},"mapped private buffer remains rejected");
        mutate(backing,0x6C,static_cast<unsigned char>(1),"persistent mapping remains rejected");
        mutate(backing,0x78,UINT64_MAX,"nonfinite initialization upload remains rejected");
        mutate(backing,0x78,b.initialization+1,"changed initialization upload remains rejected");
        privateprobe::Buffer initial{};initial.wrapper=b.wrapper;initial.bytes=b.bytes;
        check(privateprobe::Contract(initial,true)&&initial.backing==b.backing&&initial.resource==b.resource&&initial.view==b.view
            &&initial.initialization==b.initialization,"constructor capture still records complete private identity");
    }
    {
        inputfasttest::Fixture f;std::array<unsigned char,0x500> waist{};
        auto& profile=equipmentnative::profiles[MHM_WAIST];profile=equipmentnative::profiles[MHM_BODY];
        profile.part=MHM_WAIST;profile.ownerField=equipmentnative::routes[MHM_WAIST];profile.path="pl\\f_equip\\pl001_0000\\wst\\mod\\f_wst001_0000";
        std::memset(f.model.data()+0xC,0,80);std::memcpy(f.model.data()+0xC,profile.path.c_str(),profile.path.size()+1);
        Put(waist,0,observer::imageBase+0x34A7560);Put(waist,0x2A0,Address(f.model));Put(f.bridge,profile.ownerField,Address(waist));
        const facecontinuous::Identity id{Address(waist),Address(f.model),Address(f.source),Address(f.player)};
        check(facecontinuous::SlotMatches(MHM_WAIST,id,false),"non-body identity keeps the controller owner route");
        Put(f.ownerSlot,0,Address(waist));
        for(unsigned kind:{1u,2u,3u}){
            auto c=f.Context(kind);if(kind==1)c.rdi=Address(waist);c.r15=Address(waist);observer::Event e{};
            check(observer::Snapshot(c,kind,e)&&e.part==MHM_WAIST&&e.owner==Address(waist),"non-body snapshot keeps the owner and part identity");
        }
        Put(f.bridge,profile.ownerField,std::uintptr_t{0});
        check(!facecontinuous::SlotMatches(MHM_WAIST,id,false),"non-body replacement cannot survive a changed controller route");
    }
    {
        inputfasttest::Fixture f;observer::Event e{};
        for(unsigned kind:{1u,2u,3u})check(observer::Snapshot(f.Context(kind),kind,e)&&e.owner==Address(f.player)
            &&e.model==Address(f.model)&&e.originalInput==Address(f.source),"all snapshot routes preserve captured identity");
        auto c=f.Context(1);c.r13++;
        check(!observer::Snapshot(c,1,e),"unaligned mesh remains rejected");c.r13=Address(f.mesh)+80;
        check(!observer::Snapshot(c,1,e),"out-of-range mesh remains rejected");
        Put(f.image,observed::sourceBindingIdRva,UINT32_MAX);
        check(!observer::Snapshot(f.Context(3),3,e),"invalid dispatch binding remains rejected");Put(f.image,observed::sourceBindingIdRva,0u);
        std::array<unsigned char,0x60> object{};object[0x58]=0x6A;
        Put(f.context,0x150,0x1234u);Put(f.context,0x1D0,Address(object));Put(f.draw,0x58,7u);f.draw[0x87]=1;
        check(observer::Snapshot(f.Context(2),2,e)&&e.contextFlags==0x1234&&e.contextObjectFlags==0x6A&&e.lod==7&&e.auxEnabled==1,
            "batching retains diagnostic context and draw fields");
        watched=Address(f.model);changed=Address(f.player)+0x2A0;inputfasttest::afterRead=ReplaceDuringRead;
        const bool refused=!observer::Snapshot(f.Context(1),1,e);inputfasttest::afterRead=nullptr;Put(f.player,0x2A0,Address(f.model));
        check(refused,"snapshot reread rejects model replacement during capture");
        SYSTEM_INFO info{};GetSystemInfo(&info);void* inaccessible=VirtualAlloc(nullptr,info.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);Need(inaccessible!=nullptr);
        Put(f.source,0x60,reinterpret_cast<std::uintptr_t>(inaccessible));
        const bool denied=!facecontinuous::SlotMatches(MHM_BODY,f.Adapter().Target(),false);
        Put(f.source,0x60,Address(f.backing));Need(VirtualFree(inaccessible,0,MEM_RELEASE)!=0);
        check(denied,"unreadable object fails closed without unprotected game reads");
    }
    return count;
}
}
