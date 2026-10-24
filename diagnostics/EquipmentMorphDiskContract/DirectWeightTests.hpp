#pragma once
namespace directweighttests {
using inputfasttest::Need;
struct Fixture : idlesamplingtest::Fixture {
    std::uint64_t stamp=20,now=16;
    Fixture(){Part().payload.resize(Part().asset->basis.size());PublishZero();}
    void Sample(){
        now+=16;
        // The synthetic image deliberately has no native Map implementation.
        // Run the real worker through capture and CPU composition, stopping at
        // its existing code guard before any GPU/native call is possible.
        try{Step(now);}catch(const std::runtime_error& e){
            if(std::string_view(e.what())!="Part map code/contract")throw;
            Need(!facecontinuous::CodeMatches());
        }
    }
    void Settle(){
        const auto view=Adapter().View();
        for(const auto& slot:view.slots)if(slot.phase==launchpool::Phase::AwaitingStamp){
            Need(Adapter().QueueUpload(slot.token,++stamp));
        }
        upload.completed=stamp;drawing.completed=counter;
        Need(Adapter().Completed(stamp,counter));Cycle();Cycle();
        drawing.completed=counter;Need(Adapter().Completed(stamp,counter));
        Part().binding->Acknowledge(Adapter().View());Part().binding->Collect(Adapter().View());
    }
};
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);++count;};
    Fixture f;auto& api=f.Part().binding->api;const auto target=f.Part().target;
    std::uint64_t low=0,high=0,revision=0;
    Need(api.open_source("direct-low",20,60000,&low)==MHM_OK);
    auto submit=[&](std::uint64_t source,float value){const MhmUpdate update{target,0,value};Need(api.submit(source,&update,1,&revision)==MHM_OK);};
    auto sample=[&](float value,const char* why){
        f.Sample();const auto view=f.Adapter().View();
        check(f.Part().lastSample==std::vector<float>{value}&&f.Part().lastRevision==revision&&
            view.requested.Weights().size()==1&&view.requested.Weights()[0]==value&&view.requested.revision==revision,why);
        f.Settle();MhmWeightState state{};std::uint32_t needed=0;std::uint64_t read=0,applied=0;
        Need(api.read_weights(target,&state,1,&needed,&read,&applied)==MHM_OK);
        check(state.requested==value&&state.applied==value&&applied==revision,"direct capture is acknowledged only after publication and successor signal");
    };
    submit(low,1);sample(1,"real worker captures 0 to 1 directly at next eligible sample");
    submit(low,.25f);sample(.25f,"real worker captures 1 to quarter directly");
    submit(low,1);sample(1,"real worker reverses quarter to 1 without an intermediate weight");
    const auto before=f.Adapter().View();submit(low,1);sample(1,"same-value revision still reaches capture and acknowledgement");
    const auto after=f.Adapter().View();
    check(after.active==before.active&&after.sameWeightPublications==before.sameWeightPublications+1,
        "same-value revision reuses active resource while publishing new metadata");
    Need(api.open_source("direct-high",200,60000,&high)==MHM_OK);submit(high,0);sample(0,"high-priority explicit zero masks lower nonzero directly");
    Need(api.close_source(high)==MHM_OK);revision=f.Part().lastRevision+1;sample(1,"release reveals lower source without host smoothing");
    const auto retained=f.Part().lastRevision;const MhmUpdate invalid[]{ {target,0,.5f},{target,0,.75f} };
    std::uint64_t rejected=0;
    check(api.submit(low,invalid,2,&rejected)==MHM_INVALID,"invalid atomic batch is rejected");
    revision=retained;sample(1,"invalid batch preserves previous value and revision");
    // Advance the core's explicit test clock; wall-clock sleeps are unnecessary.
    Need(MhwMorphHostTick(1000)==MHM_OK&&api.open_source("direct-expiry",200,100,&high)==MHM_OK);
    submit(high,.25f);sample(.25f,"leased high-priority source captures exact endpoint");
    Need(MhwMorphHostTick(1100)==MHM_OK);revision=f.Part().lastRevision+1;sample(1,"expired source reveals lower endpoint directly");
    Need(api.close_source(low)==MHM_OK);revision=f.Part().lastRevision+1;sample(0,"final release captures zero and returns to original input");
    f.Cycle();f.drawing.completed=f.counter;Need(f.Adapter().Completed(f.stamp,f.counter));f.Cycle();
    check(f.Adapter().View().active<0&&f.Adapter().Live(),"zero publication preserves live original-input route and resource retirement");
    std::printf("PASS: %u direct-weight worker checks\n",count);return count;
}
}
