#pragma once
namespace sameweighttest {
inline launchpool::MorphVersion Version(float weight,std::uint64_t frame,unsigned size=1){std::vector<float> values(size);values[0]=weight;return launchpool::MorphVersion::FromWeights(values,frame,frame);}
struct ActivePool {
    launchpool::Pool pool;launchpool::Token token{};std::uint64_t counter=100;
    explicit ActivePool(unsigned size=1){
        using inputfasttest::Need;Need(pool.Start(counter)&&pool.Request(Version(.6f,1,size)));
        const auto next=pool.Prepare();Need(next.has_value());token=*next;
        Need(pool.BeginUpload(token)&&pool.UploadQueued(token,20)&&pool.Completed(20,counter)&&pool.Launch(counter));End();
    }
    void End(){using inputfasttest::Need;Need(pool.Input(counter,counter).has_value()&&pool.Join(counter)&&pool.Signal(counter+1,true,true));counter++;}
};
inline unsigned Run(){
    using namespace inputfasttest;
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    {
        ActivePool f;const auto before=f.pool.Get();Need(f.pool.Request(Version(.6f,2)));
        check(!f.pool.Prepare(),"identical active weights require no new upload");
        check(f.pool.Get().applied==before.applied&&f.pool.Get().sameWeightPublications==0,"new metadata cannot publish during Request or Prepare");
        Need(f.pool.Launch(f.counter));const auto view=f.pool.Get();const auto& slot=view.slots[f.token.slot];
        check(view.applied==Version(.6f,2)&&view.active==before.active,"same-weight revision publishes on the existing active slot at Launch");
        check(slot.token==before.slots[f.token.slot].token&&slot.uploadStamp==20&&slot.lastUseSignal==101,
            "metadata publication preserves allocation token, upload stamp and GPU use history");
        check(slot.phase==launchpool::Phase::Active&&view.publicationCounter==101&&view.sameWeightPublications==1,
            "same-weight publication has a fresh acknowledgement boundary and aggregate counter");
        Need(f.pool.Request(Version(.6f,3)));
        check(!f.pool.Prepare()&&f.pool.Get().applied.frame==2,"mid-cycle same-weight request cannot change applied metadata");
        f.End();Need(f.pool.Launch(f.counter));
        check(f.pool.Get().applied.frame==3&&f.pool.Get().sameWeightPublications==2,"next cycle publishes the latest exact same weights without an upload");
        f.End();const auto stable=f.pool.Get();Need(f.pool.Request(Version(.6f,3))&&f.pool.Launch(f.counter));
        check(f.pool.Get().sameWeightPublications==2&&f.pool.Get().publicationCounter==stable.publicationCounter,
            "identical frame and revision do not create another metadata publication");
        f.End();Need(f.pool.Request(Version(std::nextafter(.6f,1.f),4)));
        check(f.pool.Prepare().has_value(),"a one-ULP weight change still requires a new payload");
    }
    {
        ActivePool f(64);std::vector<float> values(64);values[0]=.6f;values[63]=1e-9f;const auto weight=launchpool::MorphVersion::FromWeights(values,2,2);Need(f.pool.Request(weight));
        check(f.pool.Prepare().has_value(),"exact equality covers all sixty-four weights without rounding or tolerance");
    }
    {
        ActivePool f;Need(f.pool.Request(Version(.6f,2)));f.pool.Invalidate();
        check(!f.pool.Prepare()&&!f.pool.Launch(f.counter)&&!f.pool.Request(Version(.6f,3)),"quarantined generations never reuse an active payload");
    }
    {
        launchpool::Pool pool;Need(pool.Start(100)&&pool.Request(Version(.6f,1)));const auto old=pool.Prepare();
        Need(old&&pool.BeginUpload(*old)&&pool.UploadQueued(*old,20)&&pool.Request(Version(.6f,2)));const auto next=pool.Prepare();
        check(next&&next->slot!=old->slot&&pool.Get().slots[old->slot].phase==launchpool::Phase::Uploading,
            "identical uncompleted uploads cannot substitute for an already active payload");
    }
    {
        ActivePool f;Need(f.pool.Request(Version(0,2))&&f.pool.Launch(f.counter));const auto retired=f.pool.Get().slots[f.token.slot];
        check(retired.phase==launchpool::Phase::Retiring&&!f.pool.Prepare(),"zero still retires the private buffer without an upload");
        Need(f.pool.Join(f.counter)&&f.pool.Signal(f.counter+1,true,true));f.counter++;
        Need(f.pool.Request(Version(.6f,3)));const auto token=f.pool.Prepare();
        check(token&&token->slot!=f.token.slot&&f.pool.Get().slots[f.token.slot].phase==launchpool::Phase::Retiring,
            "returning nonzero cannot reuse a retiring buffer before successor and GPU completion proofs");
    }
    for(bool queued:{false,true}){
        ActivePool f;Need(f.pool.Request(Version(.8f,2)));const auto pending=f.pool.Prepare();Need(pending&&f.pool.BeginUpload(*pending));
        if(queued)Need(f.pool.UploadQueued(*pending,21));Need(f.pool.Request(Version(.6f,3)));
        check(!f.pool.Prepare(),"returning to active weights avoids a third upload while an obsolete upload is pending");
        Need(f.pool.Launch(f.counter));
        check(f.pool.Get().applied==Version(.6f,3)&&f.pool.Get().active==static_cast<int>(f.token.slot),"obsolete pending work cannot replace the matching active buffer");
        if(!queued)Need(f.pool.UploadQueued(*pending,21));f.End();
        check(f.pool.Get().slots[pending->slot].phase==launchpool::Phase::Uploading,"superseded native upload remains tracked until completion");
        Need(f.pool.Completed(21,f.counter));
        check(f.pool.Get().slots[pending->slot].phase==launchpool::Phase::Free,"completed obsolete upload is reclaimed through existing proof");
    }
    {
        ActivePool f;Need(f.pool.Request(Version(.6f,2))&&f.pool.Launch(f.counter));f.End();
        Need(f.pool.Request(Version(.8f,3)));const auto next=f.pool.Prepare();
        Need(next&&f.pool.BeginUpload(*next)&&f.pool.UploadQueued(*next,21)&&f.pool.Completed(21,f.counter)&&f.pool.Launch(f.counter));
        const auto retired=f.pool.Get().slots[f.token.slot];
        check(retired.phase==launchpool::Phase::Retiring&&retired.lastUseSignal==102&&retired.retirementFence==103,
            "later geometry change retains the metadata-reused buffer's actual draw retirement history");
        f.End();Need(f.pool.Completed(21,f.counter));
        check(f.pool.Get().slots[f.token.slot].phase==launchpool::Phase::Retiring,"completed drawing still cannot bypass the successor join");
        Need(f.pool.Launch(f.counter)&&f.pool.Join(f.counter));
        check(f.pool.Get().slots[f.token.slot].phase==launchpool::Phase::Free,"successor join and completed fences release the reused buffer normally");
    }
    {
        Fixture f;auto& binding=*f.Part().binding;std::uint64_t source=0,otherSource=0,revision=0;
        auto asset=*f.Part().asset;asset.part=MHM_WAIST;asset.model="pl/synthetic/same-weight-other-part";
        equipmentmorph::PartBinding other;other.Attach(asset);
        Need(binding.api.open_source("same-weight-ack",1,2000,&source)==MHM_OK);
        const MhmUpdate update{binding.target,0,.6f};Need(binding.api.submit(source,&update,1,&revision)==MHM_OK);
        launchpool::MorphVersion first{};const std::array<float,1> weights={.6f};Need(binding.Capture(weights,revision,first)&&f.Adapter().Request(first));
        f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();const auto original=f.Adapter().View();
        std::vector<unsigned char> a(f.Part().asset->basis.size()),b(a.size());
        Need(MhwMorphHostCompose(first.frame,binding.target,a.data(),static_cast<unsigned>(a.size()))==MHM_OK);
        Need(binding.api.open_source("same-weight-other-part",1,2000,&otherSource)==MHM_OK);
        const MhmUpdate otherUpdate{other.target,0,.3f};Need(binding.api.submit(otherSource,&otherUpdate,1,&revision)==MHM_OK);
        std::vector<float> bodyWeights;std::uint64_t bodyRevision=0;Need(binding.Requested(1,bodyWeights,bodyRevision));
        check(bodyWeights==std::vector<float>{.6f}&&bodyRevision==revision,"another part changes the global revision without changing this part's weight");
        launchpool::MorphVersion second{};
        Need(binding.Capture(weights,revision,second)&&f.Adapter().Request(second));
        Need(MhwMorphHostCompose(second.frame,binding.target,b.data(),static_cast<unsigned>(b.size()))==MHM_OK);
        check(a==b&&!f.Adapter().Prepare(),"real host frames with identical weights have byte-identical payloads and no second upload token");
        f.Input(1,true);f.Input(3,true);f.EndCycle();binding.Acknowledge(f.Adapter().View());binding.Collect(f.Adapter().View());
        check(binding.lastAck==first.frame&&binding.pending.contains(second.frame),"new metadata frame stays pending while the old cycle is acknowledged");
        f.StartCycle();const auto published=f.Adapter().View();binding.Acknowledge(published);
        check(published.applied==second&&published.active==original.active&&binding.lastAck==first.frame,"metadata boundary alone cannot acknowledge before a successful signal");
        f.Input(1,true);f.Input(3,true);f.EndCycle();binding.Acknowledge(f.Adapter().View());binding.Collect(f.Adapter().View());
        MhmWeightState state{};std::uint32_t needed=0;std::uint64_t requested=0,applied=0;
        Need(binding.api.read_weights(binding.target,&state,1,&needed,&requested,&applied)==MHM_OK);
        check(binding.lastAck==second.frame&&applied==revision&&state.applied==.6f&&binding.pending.empty(),
            "same-weight publication acknowledges the current revision and releases host frame storage after Signal");
        check(f.Adapter().Live()&&f.Adapter().View().sameWeightPublications==1&&f.Adapter().Inputs()==2&&f.Adapter().Dispatches()==2,
            "real adapter preserves compute-dispatch coverage across metadata reuse");
        struct Log {std::string text;bool Write(const char* value){text+=value;return true;}} log;
        facecontinuous::WriteState(log,f.Part(),"part_state");
        check(log.text.find("\"same_weight_publications\":1")!=std::string::npos,"state log exposes the same-weight reuse count");
        Need(binding.api.close_source(source)==MHM_OK&&binding.api.close_source(otherSource)==MHM_OK);
    }
    return count;
}
}
