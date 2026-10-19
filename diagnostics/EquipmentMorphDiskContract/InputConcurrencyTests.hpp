#pragma once
#include <atomic>
#include <chrono>
namespace inputconcurrencytest {
inline std::uintptr_t watched=0;
inline bool writerAllowed=false;
inline void ProbeLocks(LPCVOID address,SIZE_T){
    if(reinterpret_cast<std::uintptr_t>(address)!=watched)return;
    // Sampling uses only value copies, so a lifecycle/worker thread can run.
    std::thread probe([]{
        writerAllowed=facecontinuous::gate.try_lock();
        if(writerAllowed)facecontinuous::gate.unlock();
    });probe.join();
}
inline std::atomic<unsigned> arrivals=0;
inline std::atomic<bool> timedOut=false;
inline inputfasttest::Fixture* changingFixture=nullptr;
inline unsigned changeDuringRead=0,remainingChanges=0;
inline void ChangeGeneration(LPCVOID address,SIZE_T){
    if(reinterpret_cast<std::uintptr_t>(address)!=watched||!remainingChanges)return;--remainingChanges;
    std::thread writer([]{
        auto& f=*changingFixture;
        if(changeDuringRead==0)facecontinuous::PlayerGone(inputfasttest::Address(f.player),"equipment_player_removed");
        else if(changeDuringRead==1){auto context=f.Context(1);context.rcx=inputfasttest::Address(f.player);context.rdx=inputfasttest::Address(f.model)+128;facecontinuous::ModelChanging(context);}
        else {std::lock_guard lock(facecontinuous::gate);
            if(changeDuringRead==2){equipmentnative::profiles[MHM_BODY].path.assign(4096,'X');facecontinuous::ResetPartLocked(f.Part());}
            else if(changeDuringRead==5){facecontinuous::parts[MHM_WAIST].epoch++;facecontinuous::mainPlayerOwners[MHM_WAIST]=0x99000;}
            else {auto entry=facecontinuous::mainPlayerSelection.entry;entry.second++;
                inputfasttest::Put(f.singleton,0x48,entry);
                facecontinuous::mainPlayerSelection=mhm::localplayer::Select([](std::uintptr_t a,auto& value){return observer::Read(a,value);},observer::imageBase);}
        }
    });writer.join();
}
inline void Rendezvous(LPCVOID address,SIZE_T){
    if(reinterpret_cast<std::uintptr_t>(address)!=watched)return;
    arrivals.fetch_add(1);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(arrivals.load()<5){
        if(std::chrono::steady_clock::now()>=deadline){timedOut=true;break;}
        std::this_thread::yield();
    }
}
inline facecontinuous::InputProof Sample(inputfasttest::Fixture& f,unsigned kind=1){
    facecontinuous::InputProof proof;
    {std::lock_guard lock(facecontinuous::gate);proof=facecontinuous::SnapshotInputProof(inputfasttest::Address(f.player));}
    facecontinuous::ReadInputProof(f.Context(kind),kind,inputfasttest::Address(f.player),proof);return proof;
}
inline void Apply(inputfasttest::Fixture& f,safetyhook::Context& context,const facecontinuous::InputProof& proof,unsigned kind=1){
    std::lock_guard lock(facecontinuous::gate);facecontinuous::ApplyInputProof(context,kind,inputfasttest::Address(f.player),proof);
}
inline void Activate(inputfasttest::Fixture& f){f.Request(.6f,1);f.Queue();inputfasttest::Need(f.Adapter().Completed(20,100));f.Cycle();}
inline unsigned Run(){
    using namespace inputfasttest;
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
        Fixture f;Activate(f);watched=Address(f.model);writerAllowed=false;afterRead=ProbeLocks;
        auto context=f.Context(1);facecontinuous::Input(context,1);afterRead=nullptr;
        check(writerAllowed,"runtime header sampling releases the shared state gate");
        check(context.r8==Address(f.wrappers[0])&&f.Adapter().Live(),"parallel sampling still publishes the expected private input");
        watched=Address(f.player)+0x430;writerAllowed=true;afterRead=ProbeLocks;
        context=f.Context(1);facecontinuous::Input(context,1);afterRead=nullptr;
        check(!writerAllowed,"late stream proof and input commit remain under the state gate");
    }
    {
        Fixture f;Activate(f);Put(f.context,0x710,Address(f.wrappers[0]));
        watched=Address(f.model);arrivals=0;timedOut=false;afterRead=Rendezvous;
        std::array<unsigned,5> kinds={1,2,3,1,3};std::array<safetyhook::Context,5> contexts{};std::array<std::thread,5> workers;
        for(unsigned i=0;i<workers.size();i++)contexts[i]=f.Context(kinds[i]);
        for(unsigned i=0;i<workers.size();i++)workers[i]=std::thread([&,i]{facecontinuous::Input(contexts[i],kinds[i]);});
        for(auto& worker:workers)worker.join();afterRead=nullptr;
        check(!timedOut&&arrivals==5,"five actual compute/draw/dispatch callbacks overlap their native header proofs");
        bool correct=true;for(unsigned i=0;i<contexts.size();i++)correct=correct&&contexts[i].r8==(kinds[i]==3?7:Address(f.wrappers[0]));
        check(correct&&f.Adapter().Inputs()==3&&f.Adapter().Dispatches()==2,"parallel callbacks serialize replacement and coverage counters");
        f.Cycle();check(f.Adapter().Live(),"parallel mixed callbacks retain complete native cycle coverage");
    }
    for(unsigned change=0;change<3;change++){
        Fixture f;Activate(f);changingFixture=&f;changeDuringRead=change;remainingChanges=1;watched=Address(f.model);afterRead=ChangeGeneration;
        auto context=f.Context(1);facecontinuous::Input(context,1);afterRead=nullptr;changingFixture=nullptr;
        check(context.r8==Address(f.source)&&f.Adapter().Inputs()==0&&(change==2?f.Adapter().Live():!f.Adapter().Live()),
            "actual callback survives concurrent player removal/model replacement/profile reallocation and reset");
    }
    for(unsigned change=3;change<6;change++){
        Fixture f;Activate(f);changingFixture=&f;changeDuringRead=change;remainingChanges=change==4?2:1;
        watched=Address(f.model);afterRead=ChangeGeneration;reads=0;
        auto context=f.Context(1);facecontinuous::Input(context,1);afterRead=nullptr;changingFixture=nullptr;
        if(change==4)check(context.r8==Address(f.source)&&!f.Adapter().Live()&&remainingChanges==0
            &&std::strcmp(f.Adapter().Reason(),"equipment_part_identity_changed")==0,"repeated selection races have bounded retries and enter automatic retirement");
        else {
            check(context.r8==Address(f.wrappers[0])&&f.Adapter().Live()&&f.Adapter().Inputs()==1&&(change!=5||reads==38),
                "selection race retries safely while an unrelated part reset does not interrupt input coverage");
            f.Input(3,true);f.Cycle();Need(f.Adapter().Live());
        }
    }
    for(unsigned change=0;change<6;change++){
        Fixture f;Activate(f);const auto proof=Sample(f);Need(proof.valid);auto context=f.Context(1);
        {std::lock_guard lock(facecontinuous::gate);
            switch(change){
                case 0:++f.Part().epoch;break;
                case 1:facecontinuous::mainPlayerSelection.entry.second=9;break;
                case 2:facecontinuous::mainPlayerOwners[MHM_BODY]=0;break;
                case 3:facecontinuous::anchorPlayer=0;break;
                case 4:facecontinuous::armed=false;break;
                case 5:facecontinuous::ResetPartLocked(f.Part());break;
            }
        }
        Apply(f,context,proof);
        check(context.r8==Address(f.source)&&f.Adapter().Live()&&f.Adapter().Inputs()==0,"stale player/route/epoch/arm proof cannot alter the current adapter");
    }
    {
        Fixture f;Activate(f);facecontinuous::InputProof proof;
        {std::lock_guard lock(facecontinuous::gate);proof=facecontinuous::SnapshotInputProof(Address(f.player));
            equipmentnative::profiles[MHM_BODY].path.assign(4096,'X');facecontinuous::ResetPartLocked(f.Part());}
        facecontinuous::ReadInputProof(f.Context(1),1,Address(f.player),proof);
        check(proof.valid,"in-flight native reads own their profile bytes after configuration strings are replaced");
        auto context=f.Context(1);Apply(f,context,proof);
        check(!f.Adapter().Target().Valid()&&f.Adapter().Live()&&context.r8==Address(f.source),"a valid old profile cannot capture a newly armed generation");
    }
    for(unsigned change=0;change<7;change++){
        Fixture f;Activate(f);const auto proof=Sample(f);Need(proof.valid);auto context=f.Context(1);
        auto model=f.model,source=f.source,bridge=f.bridge;
        switch(change){
            case 0:Put(f.player,0x2A0,Address(model));Put(f.bridge,0x480,Address(model));break;
            case 1:Put(f.model,0x110,Address(source));break;
            case 2:Put(f.player,0x126C8,Address(bridge));break;
            case 3:Put(f.player,0x430,std::uintptr_t{1});break;
            case 4:Put(f.player,0,std::uintptr_t{0});break;
            case 5:Put(f.backings[0],0x38,std::uintptr_t{1});break;
            case 6:Put(f.renderer,0x1E0,f.counter+1);break;
        }
        Apply(f,context,proof);
        check(context.r8==Address(f.source)&&!f.Adapter().Live(),"fresh commit refuses a changed model/source/bridge/stream/player/private resource/counter");
    }
    {
        Fixture f;Activate(f);const auto proof=Sample(f);Need(proof.valid);auto context=f.Context(1);
        auto changing=f.Context(1);changing.rcx=Address(f.player);changing.rdx=Address(f.model)+128;facecontinuous::ModelChanging(changing);
        Apply(f,context,proof);
        check(context.r8==Address(f.source)&&std::strcmp(f.Adapter().Reason(),"equipment_part_model_replaced")==0,"commit cannot revive a lifecycle-invalidated adapter");
    }
    {
        Fixture f;Activate(f);const auto proof=Sample(f);Need(proof.valid);f.Cycle();auto context=f.Context(1);Apply(f,context,proof);
        check(context.r8==Address(f.source)&&!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"input_outside_launch_cycle")==0,"sample cannot cross a native cycle counter");
    }
    return count;
}
}
