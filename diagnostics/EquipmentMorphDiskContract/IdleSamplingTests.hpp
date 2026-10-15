#pragma once
namespace idlesamplingtest {
using inputfasttest::Need;
struct Fixture : inputfasttest::Fixture {
    privateprobe::Log log;
    privateprobe::UploadFence upload;
    facecontinuous::DrawingFence drawing;
    Fixture(){
        MhmTargetInfo info{};info.handle=Part().target;info.part=PartIndex;info.key_count=1;
        Need(Part().smoother.Reset({&info,1},0)==MHM_OK&&Part().cadence.Reset(0));
        upload.renderer=inputfasttest::Address(renderer);drawing.pointer=0x30000;
    }
    bool Step(std::uint64_t now){return facecontinuous::PartStep(log,Part(),upload,drawing,now);}
    void PublishZero(){Step(0);Step(16);Cycle();Cycle();}
};
inline decltype(MhmApi{}.read_weights) originalRead=nullptr;
inline MhmResult MHM_CALL StaleRead(std::uint64_t target,MhmWeightState* state,std::uint32_t capacity,
    std::uint32_t* needed,std::uint64_t* requested,std::uint64_t* applied){
    const auto result=originalRead(target,state,capacity,needed,requested,applied);
    if(result==MHM_OK){std::uint64_t source=0;Need(Core().Open("capture-race",1,2000,source)==MHM_OK&&Core().Close(source)==MHM_OK);}
    return result;
}
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
        Fixture f;f.PublishZero();const auto frame=f.Part().lastRevision;
        check(!f.Step(32),"settled zero worker reports no outstanding smoothing");
        check(!f.Part().cadence.Due(32),"unchanged sample advances the sampling clock");
        check(!f.Part().cadence.Due(47)&&f.Part().cadence.Due(48),"idle samples remain sixteen milliseconds apart");
        const auto requested=f.Adapter().View().requested;
        f.Step(33);f.Step(47);f.Step(48);
        check(f.Adapter().View().requested==requested&&f.Part().lastRevision==frame,"unchanged idle samples create no new host frame");
        check(!f.Part().cadence.Due(48)&&!f.Part().cadence.Due(63),"repeated unchanged sample consumes its next interval");
        std::uint64_t source=0,revision=0;auto& api=f.Part().binding->api;
        Need(api.open_source("idle-revision",1,2000,&source)==MHM_OK);
        const MhmUpdate zero{f.Part().target,0,0};Need(api.submit(source,&zero,1,&revision)==MHM_OK);
        f.Step(49);check(f.Part().lastRevision!=revision,"revision changes still respect the sample interval");
        f.Step(64);check(f.Part().lastRevision==revision&&f.Adapter().View().requested.revision==revision,"same-value new revision is captured at next sample");
        f.Cycle();f.Cycle();f.Step(80);
        check(f.Part().binding->pending.empty()&&f.Part().binding->lastAck!=0,"idle path retains frame acknowledgement and collection");
        Need(api.close_source(source)==MHM_OK);f.Step(96);
        check(f.Part().lastRevision>revision,"source release revision remains observable at zero");
    }
    {
        Fixture f;f.Step(0);originalRead=f.Part().binding->api.read_weights;f.Part().binding->api.read_weights=StaleRead;
        f.Step(16);check(f.Part().cadence.Due(16)&&f.Part().lastSample.empty(),"stale capture leaves sample due for retry");
        f.Part().binding->api.read_weights=originalRead;f.Step(17);
        check(!f.Part().cadence.Due(17)&&!f.Part().lastSample.empty(),"fresh revision retries without another sixteen millisecond delay");
    }
    {
        Fixture f;f.Step(0);std::vector<float> values;std::uint64_t revision=0;Need(f.Part().binding->Requested(1,values,revision));
        std::array<std::uint64_t,4> frames{};
        for(auto& frame:frames)Need(MhwMorphHostCaptureInterpolated(f.Part().target,values.data(),1,revision,&frame)==MHM_OK);
        f.Step(16);check(f.Part().cadence.Due(16)&&f.Part().lastSample.empty(),"host frame capacity leaves sample due for retry");
        Need(MhwMorphHostReleaseFrame(frames[0])==MHM_OK);f.Step(17);
        check(!f.Part().cadence.Due(17)&&!f.Part().lastSample.empty(),"released frame capacity permits immediate capture retry");
        for(unsigned i=1;i<frames.size();i++)Need(MhwMorphHostReleaseFrame(frames[i])==MHM_OK);
    }
    {
        Fixture f;f.Step(0);f.Request(.6f,1);f.Queue();
        f.Step(16);check(f.Part().cadence.Due(16),"pending upload does not consume sample interval");
        check(f.Adapter().Live()&&f.Adapter().View().slots[0].phase==launchpool::Phase::Uploading,"idle optimization does not touch pending native uploads");
    }
    {
        Fixture f;f.PublishZero();auto c=f.Context(1);c.rcx=inputfasttest::Address(f.player);c.rdx=inputfasttest::Address(f.model)+128;
        facecontinuous::ModelChanging(c);check(!f.Step(32)&&!f.Adapter().Live(),"idle cadence cannot hide model invalidation");
        facecontinuous::ResetPart(f.Part());
        check(f.Part().lastSample.empty()&&f.Part().lastRevision==0&&!f.Part().cadence.Due(100),"new generation clears old sample and cadence state");
    }
    {
        Fixture f;auto asset=*f.Part().asset;asset.keys.clear();
        for(unsigned k=0;k<64;k++){auto key=f.Part().asset->keys.front();std::snprintf(key.info.id,sizeof(key.info.id),"key%u",k);asset.keys.push_back(key);}
        f.Part().binding.reset();f.Part().binding=std::make_unique<equipmentmorph::PartBinding>();f.Part().binding->Attach(asset);
        auto& binding=*f.Part().binding;std::uint64_t source=0,revision=0;
        Need(binding.api.open_source("bounded-read",1,2000,&source)==MHM_OK);
        std::array<MhmUpdate,64> updates{};for(unsigned k=0;k<updates.size();k++)updates[k]={binding.target,k,float(k)/63};
        Need(binding.api.submit(source,updates.data(),64,&revision)==MHM_OK);
        std::vector<float> values;std::uint64_t readRevision=0;
        check(binding.Requested(64,values,readRevision)&&values.size()==64&&values.front()==0&&values.back()==1&&readRevision==revision,
            "bounded request buffer reads all sixty-four keys");
        const auto* storage=values.data();Need(binding.Requested(64,values,readRevision));
        check(values.data()==storage,"repeated request reads reuse output capacity");
        bool refused=false;try{binding.Requested(65,values,readRevision);}catch(const std::exception&){refused=true;}
        check(refused,"oversized read cannot exceed bounded state storage");
        Need(binding.api.close_source(source)==MHM_OK&&Core().Detach(binding.target)==MHM_OK);
        check(!binding.Requested(64,values,readRevision),"retired public target remains stale to request reader");
    }
    {
        // Compare the original per-poll target refresh to revision-only refresh
        // through attack, reversal and release, including the 64-key limit.
        MhmTargetInfo info{};info.handle=42;info.part=MHM_BODY;info.key_count=64;
        mhm::GroupSmoothing original,changed;Need(original.Reset({&info,1},0)==MHM_OK&&changed.Reset({&info,1},0)==MHM_OK);
        std::vector<MhmUpdate> targets;for(unsigned k=0;k<64;k++)targets.push_back({42,k,0});
        std::uint64_t revision=1;double maximum=0;bool bounded=true;
        std::vector<MhmUpdate> a,b;std::uint64_t ar=0,br=0;
        for(std::uint64_t now=0;now<=4000;now++){
            const bool update=now==0||now==7||now==251||now==429||now==701;
            if(update){revision++;for(auto& v:targets)v.weight=now==7?1.f:now==251?.1f:now==429?.9f:0;}
            Need(original.SetTargets(targets,revision,now)==MHM_OK);
            if(update)Need(changed.SetTargets(targets,revision,now)==MHM_OK);
            if(now%16==0){
                Need(original.Sample(now,a,ar)==MHM_OK&&changed.Sample(now,b,br)==MHM_OK&&ar==br);
                for(unsigned k=0;k<a.size();k++){
                    maximum=std::max(maximum,std::abs(double(a[k].weight)-b[k].weight));
                    bounded=bounded&&b[k].weight>=0&&b[k].weight<=1;
                }
            }
        }
        check(maximum<=1e-6,"revision-only target updates preserve the analytic response within snap tolerance");
        check(bounded&&b.front().weight==0,"reversal and release stay bounded and settle exactly to zero");
    }
    return count;
}
}
