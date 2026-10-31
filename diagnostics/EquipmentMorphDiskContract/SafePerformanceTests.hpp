#pragma once
namespace safeperformancetests {
// Test-only member access via explicit instantiation (no production observer).
template<class Tag,typename Tag::Type Member> struct MemberAccess {
    friend typename Tag::Type Access(Tag){return Member;}
};
struct StorageTag {
    using Type=std::shared_ptr<const std::vector<float>> launchpool::MorphVersion::*;
    friend Type Access(StorageTag);
};
template struct MemberAccess<StorageTag,&launchpool::MorphVersion::storage>;
struct PoolTag {
    using Type=launchpool::Pool facecontinuous::LaunchAdapter::*;
    friend Type Access(PoolTag);
};
template struct MemberAccess<PoolTag,&facecontinuous::LaunchAdapter::pool>;
inline unsigned TestPoolIsLive(){
    using inputfasttest::Need;
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);++count;};
    launchpool::Pool pool;
    auto equal=[&]{return pool.IsLive()==pool.Get().live;};
    check(equal()&&!pool.IsLive(),"IsLive agrees before Start");
    Need(pool.Start(100)&&pool.Launch(100));
    check(equal()&&pool.IsLive()&&!pool.Input(100,100),"valid frame without active slot stays live");
    Need(pool.Join(100)&&pool.Signal(101,true,true));
    auto weight=sameweighttest::Version(.6f,1);Need(pool.Request(weight));const auto token=pool.Prepare();
    Need(token&&pool.BeginUpload(*token)&&pool.UploadQueued(*token,20)&&pool.Completed(20,101)&&pool.Launch(101));
    check(equal()&&pool.IsLive()&&pool.Input(101,101)==token->slot,"active slot stays live");
    const auto& storage=weight.*Access(StorageTag{});const auto owners=storage.use_count();
    {const auto view=pool.Get();check(storage.use_count()==owners+3&&view.live,"Get copies requested,applied and active slot owners");}
    check(pool.IsLive()&&storage.use_count()==owners,"IsLive returns bool without retaining weight owners");
    check(!pool.Input(101,102)&&equal()&&!pool.IsLive(),"mismatched input counters disable live");
    pool.Invalidate();check(equal()&&!pool.IsLive(),"Invalidate stays disabled");
    for(bool active:{false,true}){
        inputfasttest::Fixture f;
        if(active){f.Request(.6f,1);f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();}
        auto& adapter=f.Adapter();const auto id=adapter.Target();const auto inputs=adapter.Inputs(),dispatches=adapter.Dispatches();
        const auto expected=active?inputfasttest::Address(f.wrappers[0]):id.source;
        check(adapter.Input(id,id.source,f.counter,f.counter,false,1)==expected&&adapter.Inputs()==inputs+1,
            "adapter preserves private and no-slot source selection");
        check(adapter.Input(id,expected,f.counter,f.counter,true,3)==expected&&adapter.Dispatches()==dispatches+1,
            "adapter preserves dispatch coverage");
        (adapter.*Access(PoolTag{})).Invalidate();
        check(adapter.Input(id,id.source,f.counter,f.counter,false,2)==id.source&&!adapter.Live()
            &&std::string_view(adapter.Reason())=="pool_input_rejected"&&adapter.Inputs()==inputs+1&&adapter.Dispatches()==dispatches+1,
            "pool rejection returns original, retains reason and coverage counters");
        check(adapter.Input(id,id.source,f.counter,f.counter,false,1)==id.source&&adapter.Inputs()==inputs+1,
            "disabled adapter never records new input");
    }
    return count;
}
inline unsigned Run(){return TestPoolIsLive();}
}
