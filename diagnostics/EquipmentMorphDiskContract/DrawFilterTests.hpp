#pragma once
namespace drawfiltertest {
inline std::uintptr_t Unknown(){
    for(std::uintptr_t value=0x10000;value<0x100000;value+=16)if(!drawfilter::owners.MayMatch(value))return value;
    throw std::runtime_error("Synthetic filter unexpectedly saturated");
}
inline bool ReturnsUnderGate(safetyhook::Context& context){
    std::atomic<bool> entered=false,done=false;bool bypassed=false;
    std::thread callback;
    {std::lock_guard lock(facecontinuous::gate);
        callback=std::thread([&]{entered=true;facecontinuous::Input(context,2);done=true;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        while(!done.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        bypassed=entered&&done;
    }
    callback.join();
    return bypassed;
}
inline unsigned Run(const std::filesystem::path& path){
    using namespace inputfasttest;
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
        Fixture f;auto context=f.Context(2);context.r15=Unknown();
        check(ReturnsUnderGate(context),"unrelated draw returns while runtime gate is held");
        check(context.r8==Address(f.source)&&f.Adapter().Live(),"unrelated lock-free rejection preserves original input and live binding");
        reads=0;diagnosticstest::qpcCalls=0;facecontinuous::Input(context,2);
        check(!reads&&!diagnosticstest::qpcCalls,"rejected draw reads neither process memory nor QPC");
        context=f.Context(2);
        check(!ReturnsUnderGate(context),"known owner still waits for authoritative locked validation");
        check(f.Input(2)==0,"known zero draw keeps its existing original-input path");
        inputconcurrencytest::Activate(f);
        check(f.Input(2,true)==39,"known nonzero draw retains all thirty-nine native reads");
        std::uintptr_t collision=0;
        for(std::uintptr_t value=0x10000;value<0x1000000;value+=16)
            if(value!=Address(f.player)&&drawfilter::owners.MayMatch(value)){collision=value;break;}
        check(collision!=0,"synthetic non-owner collision exercises fail-open behavior");
        context=f.Context(2);context.r15=collision;reads=0;facecontinuous::Input(context,2);
        check(!reads&&context.r8==Address(f.source)&&f.Adapter().Live(),"filter collision cannot authorize private input");
        drawfilter::owners.bits=UINT64_MAX;context=f.Context(2);context.r15=0x12345;
        check(!ReturnsUnderGate(context),"saturation falls back to the original locked owner check");
        drawfilter::owners.bits=0;drawfilter::owners.Publish(facecontinuous::mainPlayerOwners);
        check(drawfilter::owners.MayMatch(0),"null owners retain the original validation path");
        facecontinuous::mainPlayerOwners[MHM_BODY]=0;context=f.Context(2);reads=0;facecontinuous::Input(context,2);
        check(!reads&&context.r8==Address(f.source),"retained stale filter bits cannot bypass the current route check");
    }
    {
        Fixture f;inputconcurrencytest::Activate(f);
        inputconcurrencytest::changingFixture=&f;inputconcurrencytest::changeDuringRead=1;inputconcurrencytest::remainingChanges=1;
        inputconcurrencytest::watched=Address(f.model);afterRead=inputconcurrencytest::ChangeGeneration;
        auto context=f.Context(2);facecontinuous::Input(context,2);afterRead=nullptr;inputconcurrencytest::changingFixture=nullptr;
        check(context.r8==Address(f.source)&&!f.Adapter().Live(),"filtered positive draw still rejects concurrent model replacement at commit");
    }
    {
        Fixture f;const auto oldCatalogue=equipmentnative::catalogue;equipmentnative::catalogue={};
        mhm::configuration::Profile profile;profile.model=equipmentnative::profiles[MHM_BODY].path;
        equipmentnative::catalogue.parts[MHM_BODY].push_back(std::make_shared<const mhm::configuration::Loaded>(mhm::configuration::Loaded{profile,*f.Part().asset}));
        drawfilter::owners.bits=0;facecontinuous::RefreshMainPlayer();
        check(drawfilter::owners.MayMatch(Address(f.player))&&facecontinuous::mainPlayerOwners[MHM_BODY]==Address(f.player),"production main-player refresh publishes its authoritative owner to the filter");
        auto replacement=f.player;const auto next=Address(replacement);
        Put(f.singleton,0x48,mhm::localplayer::Entry{0x60000,0,-1,next});Put(f.bridge,0x60,next);Put(f.bridge,0xF0,next);
        facecontinuous::RefreshMainPlayer();
        check(drawfilter::owners.MayMatch(next)&&drawfilter::owners.MayMatch(Address(f.player))&&facecontinuous::mainPlayerOwners[MHM_BODY]==next,"player switch publishes new owner without removing prior owner bits");
        facecontinuous::PlayerGone(next,"equipment_player_removed");
        check(!facecontinuous::anchorPlayer&&drawfilter::owners.MayMatch(next),"player removal leaves conservative bits while normal lifecycle invalidates the target");
        equipmentnative::catalogue=oldCatalogue;
    }
    {
        drawfilter::Owners owners;std::array<std::array<std::uintptr_t,5>,4> routes{};std::array<std::thread,4> writers;
        std::uint64_t expected=0;
        for(unsigned i=0;i<routes.size();i++)for(unsigned j=0;j<5;j++){routes[i][j]=0x10000+i*0x1000+j*16;expected|=drawfilter::Owners::Bits(routes[i][j]);}
        for(unsigned i=0;i<writers.size();i++)writers[i]=std::thread([&,i]{owners.Publish(routes[i]);});
        for(auto& writer:writers)writer.join();
        check(owners.bits==expected,"concurrent atomic publication loses no owner bits");
        bool matched=true;for(const auto& row:routes)for(auto owner:row)matched=matched&&owners.MayMatch(owner);
        check(matched,"all concurrently published owners remain candidates");
        owners.Publish({});check(owners.bits==expected,"empty publication never clears historical bits");
        for(std::uintptr_t value=1;value<10000;value++)owners.Publish({value,0,0,0,0});
        check(owners.bits==UINT64_MAX&&owners.MayMatch(0x12345678),"long owner history safely degrades to the full validation path");
    }
    {
        const auto old=drawfilter::reported.load();const auto oldPending=drawfilter::pending;
        drawfilter::reported=0;drawfilter::pending=0;
        for(unsigned i=0;i<255;i++)drawfilter::Bypassed();
        check(drawfilter::reported==0&&drawfilter::pending==255,"unrelated telemetry remains thread-local until a complete batch");
        drawfilter::Bypassed();check(drawfilter::reported==256&&drawfilter::pending==0,"one batch reports exactly two hundred fifty-six calls");
        std::array<std::thread,4> callbacks;
        for(auto& callback:callbacks)callback=std::thread([]{for(unsigned i=0;i<513;i++)drawfilter::Bypassed();});
        for(auto& callback:callbacks)callback.join();
        check(drawfilter::reported==2304,"concurrent diagnostic batches accumulate without per-call shared updates");
        diagnosticstest::UnlockedLog log;drawfilter::Write(log,1000);
        check(log.unlocked&&log.text.find("\"bypassed_calls_reported\":2304")!=std::string::npos,"filter telemetry logs outside the gate with cumulative batched semantics");
        drawfilter::reported=UINT64_MAX-128;for(unsigned i=0;i<256;i++)drawfilter::Bypassed();
        check(drawfilter::reported==UINT64_MAX,"batched diagnostic count saturates without wrapping");
        privateprobe::Log output;output.file=::CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(output.file!=INVALID_HANDLE_VALUE,"draw filter diagnostic log opens");drawfilter::Write(output,1000);
        check(output.Flush(),"draw filter diagnostic record flushes");
        drawfilter::reported=old;drawfilter::pending=oldPending;
    }
    {
        inputtiming::WindowClock clock;
        check(!clock.Due(0)&&!clock.Due(999),"first diagnostic window uses relative runtime time and waits one second");
        check(clock.Due(1000)==1000,"first elapsed diagnostic window is one second rather than an uptime underflow");
        check(!clock.Due(500)&&!clock.Due(1999)&&clock.Due(2005)==1005,"later diagnostic windows reject backwards time and preserve their interval");
    }
    return count;
}
}
