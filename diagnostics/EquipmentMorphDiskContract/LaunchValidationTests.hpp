#pragma once
namespace launchvalidationtest {
using inputfasttest::Address;
using inputfasttest::Put;
using inputfasttest::Need;
// Exercise production LaunchStarting with owned engine/renderer/part memory.
// The inherited fixture supplies the real selection and source contracts.
struct Fixture : inputfasttest::Fixture {
    std::vector<unsigned char> engine=std::vector<unsigned char>(0x7B000);
    unsigned workers=3;
    Fixture(){
        EndCycle();ResetAdapter();
        Put(image,0x51C4480,Address(engine));Put(engine,0x78,Address(renderer));
        Put(engine,0x68D14,workers);Put(engine,0x1F3CC,0u);
        for(unsigned i=0;i<8;i++)Put(engine,0x7AE98+i*8,std::uintptr_t{0x70000+i*0x100});
    }
    void ResetAdapter(){
        Part().adapter=std::make_unique<facecontinuous::LaunchAdapter>(++Part().epoch);
        std::array<std::uintptr_t,3> handles{};
        for(unsigned i=0;i<3;i++)handles[i]=Part().buffers[i].wrapper;
        Need(Adapter().Arm()&&Adapter().Capture({Address(player),Address(model),Address(source),Address(player)},Address(source))
            &&Adapter().PublishResources(handles));
    }
    std::uint64_t Launch(unsigned index){
        safetyhook::Context c{};c.rdi=Address(engine);c.rsi=index;inputfasttest::reads=0;
        facecontinuous::LaunchStarting(c);return inputfasttest::reads;
    }
    void Finish(){
        const facecontinuous::Frame frame{Address(engine),Address(renderer),0x30000,counter,counter,workers,0,GetCurrentThreadId()};
        for(unsigned i=0;i<workers;i++)Need(Adapter().Wait(frame,i,0x80000+i*0x100,0));
        Need(Adapter().Boundary(frame));counter++;Put(renderer,0x1E0,counter);
        Need(Adapter().Signal(Address(renderer),0x30000,counter,counter,counter,true));
    }
};
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);count++;};
    constexpr std::uint64_t frameReads=9; // EngineFacts plus this worker's event.
    {
        Fixture f;check(f.Launch(0)>frameReads&&f.Adapter().Started(),"first worker validates the complete equipment identity");
        check(f.Launch(1)==frameReads&&f.Adapter().Live(),"later worker avoids duplicate equipment reads");
        check(f.Launch(2)==frameReads&&f.Adapter().Live(),"last worker retains frame and event reads only");
        f.Finish();check(f.Launch(0)>frameReads&&f.Adapter().Signals()==1,"next cycle repeats the full identity validation");
        f.Launch(1);f.Launch(2);f.Finish();
        Put(f.model,0x110,std::uintptr_t{0});f.Launch(0);
        check(!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"equipment_part_identity_changed")==0,"next publication refuses changed source identity");
    }
    {
        Fixture f;check(f.Launch(1)>frameReads&&!f.Adapter().Started()&&f.Adapter().Live(),"new binding observed mid-cycle still validates and waits for worker zero");
        f.Launch(0);f.Launch(1);f.Launch(2);f.Finish();f.ResetAdapter();
        check(f.Launch(1)>frameReads&&!f.Adapter().Started(),"replacement adapter cannot inherit another generation's validation");
        Put(f.model,0x110,std::uintptr_t{0});f.Launch(0);
        check(!f.Adapter().Live(),"replacement generation revalidates before publication");
    }
    {
        Fixture f;Put(f.singleton,0xAE40,std::uintptr_t{0});f.Launch(0);
        check(!f.Adapter().Live(),"first worker rejects changed main player selection");
    }
    {
        Fixture f;f.Launch(0);auto c=f.Context(1);c.rcx=Address(f.player);c.rdx=Address(f.model)+128;
        facecontinuous::ModelChanging(c);check(!f.Adapter().Live(),"mid-cycle model change immediately invalidates binding");
        check(f.Launch(1)==0&&!f.Adapter().Live(),"later worker cannot revive invalidated binding");
    }
    {
        Fixture f;f.Launch(0);facecontinuous::PlayerGone(Address(f.player),"equipment_player_removed");f.Launch(1);
        check(!f.Adapter().Live(),"mid-cycle player removal remains invalidated");
    }
    {
        Fixture f;f.Launch(0);f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));
        f.Launch(1);f.Launch(2);f.Finish();f.Launch(0);
        check(f.Input(1,true)>0&&f.Input(3,true)>1,"nonzero publication keeps complete input and dispatch checks");
        Put(f.model,0x110,std::uintptr_t{0});f.Input(1);
        check(!f.Adapter().Live(),"silent source change is rejected before nonzero substitution");
    }
    {
        Fixture f;f.Launch(0);f.Launch(2);
        check(!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"mixed_launch_batch")==0,"later worker order remains strict");
    }
    {
        Fixture f;f.Launch(0);f.Launch(0);
        check(!f.Adapter().Live(),"duplicate first worker is rejected");
    }
    {
        Fixture f;f.Launch(0);Put(f.renderer,0x1E0,f.counter+1);f.Launch(1);
        check(!f.Adapter().Live(),"later worker rejects changed frame counter");
    }
    {
        Fixture f;f.Launch(0);Put(f.engine,0x1F3CC,1u);f.Launch(1);
        check(!f.Adapter().Live(),"later worker rejects mixed frame mode");
    }
    {
        Fixture f;f.Launch(0);Put(f.engine,0x68D14,4u);f.Launch(1);
        check(!f.Adapter().Live(),"later worker rejects changed worker count");
    }
    {
        Fixture f;f.Launch(0);f.Launch(1);f.Launch(2);f.Finish();f.Launch(0);
        Put(f.engine,0x7AEA0,std::uintptr_t{0x90000});f.Launch(1);
        check(!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"launch_event_changed")==0,"later worker retains event identity across cycles");
    }
    {
        Fixture f;f.Launch(0);Put(f.engine,0x78,std::uintptr_t{0});f.Launch(1);
        check(!f.Adapter().Live()&&std::strcmp(f.Adapter().Reason(),"launch_snapshot")==0,"later worker retains renderer backlink validation");
    }
    {
        Fixture f;f.Launch(0);Put(f.engine,0x7AEA0,std::uintptr_t{0});f.Launch(1);
        check(!f.Adapter().Live(),"later worker rejects missing event");
    }
    return count;
}
}
