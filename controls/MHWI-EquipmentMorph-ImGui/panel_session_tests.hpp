#pragma once
#include "PanelSession.hpp"
#include "panel_capacity_test_fixture.hpp"
namespace paneltest {
inline void TestActualCounts(){
    for(const auto count:{1u,3u,64u,65u,489u,1024u}){
        const auto h=Attach(MHM_BODY,count);morphpanel::Session session;Check(session.Connect(Api())==MHM_OK,"connect expanded limits");morphpanel::Snapshot s;
        Check(session.Snapshot(s)==MHM_OK&&s.keys.size()==count&&s.keyCount==count&&s.targets.size()==1,"actual count without padding");
        Check(morphpanel::Valid(s,session.Limits()),"valid dynamic snapshot");Core().Detach(h);
    }
    std::vector<uint64_t> handles;for(uint32_t p=0;p<5;++p)handles.push_back(Attach(p,1024));
    morphpanel::Session session;Check(session.Connect(Api())==MHM_OK,"five targets connect");morphpanel::Snapshot s;
    Check(session.Snapshot(s)==MHM_OK&&s.keyCount==5120,"five complete target tables");
    std::vector<MhmUpdate> updates;for(const auto& k:s.keys)updates.push_back({k.target,k.index,.5f});
    Check(session.Replace(updates)==MHM_OK&&session.Snapshot(s)==MHM_OK&&s.keys.back().owned&&s.keys.back().contribution==.5f,"single full 5120 submission");
    updates.push_back(updates.back());Check(session.Replace(updates)==MHM_CAPACITY,"5121 rejected");
    auto bad=s;bad.keys.back().info.id[63]='x';Check(!morphpanel::Valid(bad,session.Limits()),"truncated ID rejected");
    bad=s;bad.targets.back().info.part=0;Check(!morphpanel::Valid(bad,session.Limits()),"duplicate part rejected");
    bad=s;bad.keys[1].info=bad.keys[0].info;Check(!morphpanel::Valid(bad,session.Limits()),"duplicate ID rejected");
    bad=s;bad.keys.back().state.requested=std::numeric_limits<float>::infinity();Check(!morphpanel::Valid(bad,session.Limits()),"nonfinite rejected");
    for(auto h:handles)Core().Detach(h);
}
inline void TestAtomicChangeMany(){
    auto api=Api();const auto body=Attach(MHM_BODY,489),arm=Attach(MHM_ARMS,3,true);morphpanel::Session session;Check(session.Connect(api)==MHM_OK,"atomic connect");
    const MhmUpdate initial[]{{body,0,.3f},{arm,0,.4f}};Check(session.Replace(initial)==MHM_OK,"initial replace");morphpanel::Snapshot before,after;session.Snapshot(before);
    using mhmcontrol::Action;
    auto a=Request(Action::Set,body,488,.8f),b=Request(Action::Set,arm,1,.8f);const mhmcontrol::Request conflict[]{a,b};
    Check(session.ChangeMany(conflict)==MHM_CONFLICT,"conflict rejects whole batch");session.Snapshot(after);Check(after.revision==before.revision&&!after.keys[488].owned&&after.keys[0].contribution==.3f,"rejected batch unchanged");
    b=Request(Action::Set,arm,2,2);const mhmcontrol::Request invalid[]{a,b};Check(session.ChangeMany(invalid)==MHM_INVALID,"invalid batch rejected");
    const mhmcontrol::Request duplicate[]{a,a};Check(session.ChangeMany(duplicate)==MHM_INVALID,"duplicate edits rejected");
    Core().Detach(arm);const mhmcontrol::Request stale[]{a,Request(Action::Set,arm,0,.1f)};Check(session.ChangeMany(stale)==MHM_STALE,"stale batch rejected");
    session.Snapshot(after);Check(after.keys[0].owned&&after.keys[0].contribution==.3f&&!after.keys.back().owned,"healthy contribution retained");
    const auto zero=Request(Action::Set,body,488,0);Check(session.ChangeMany({&zero,1})==MHM_OK,"explicit zero accepted");session.Snapshot(after);Check(after.keys.back().owned&&after.keys.back().contribution==0,"explicit zero owned");
    const auto drop=Request(Action::Drop,body,488);Check(session.ChangeMany({&drop,1})==MHM_OK,"drop last key");session.Snapshot(after);Check(after.keys[0].owned&&!after.keys.back().owned,"drop preserves other key");
    Core().Tick(2001);Check(session.Heartbeat()==MHM_STALE,"lease expiry");session.Snapshot(after);Check(!after.source&&!after.keys[0].owned,"expired edits not replayed");Core().Detach(body);
}
inline MhmApi raceApi{};inline uint64_t removeOnRead=0;inline uint32_t keyReads=0;
inline MhmResult MHM_CALL RacingRead(uint64_t h,MhmWeightState* v,uint32_t cap,uint32_t* n,uint64_t* r,uint64_t* a){if(removeOnRead){Core().Detach(removeOnRead);removeOnRead=0;}return raceApi.read_weights(h,v,cap,n,r,a);}
inline MhmResult MHM_CALL CountKey(uint64_t h,uint32_t i,MhmKeyInfo* k){++keyReads;return raceApi.key_at(h,i,k);}
inline void TestSnapshotAtomicRead(){
    auto body=Attach(MHM_BODY,65),arm=Attach(MHM_ARMS,3);raceApi=Api();auto api=raceApi;api.read_weights=RacingRead;api.key_at=CountKey;keyReads=0;
    morphpanel::Session session;session.Connect(api);morphpanel::Snapshot s;Check(session.Snapshot(s)==MHM_OK,"race baseline");
    const auto reads=keyReads;Check(session.Snapshot(s)==MHM_OK&&session.Weights(s)==MHM_OK&&keyReads==reads,"cached keys avoid enumeration");
    MhmUpdate edit{body,0,.7f};session.Replace({&edit,1});session.Snapshot(s);const auto oldSource=s.source;removeOnRead=arm;
    Check(session.Snapshot(s)!=MHM_OK&&s.targetCount==2&&s.source==oldSource&&s.keys[0].owned,"partial read does not publish or prune");
    Check(session.Snapshot(s)==MHM_OK&&s.targetCount==1&&s.keys[0].owned,"healthy ownership after retirement");Core().Detach(body);
}
inline uint32_t advertisedKeys=0;
inline MhmResult MHM_CALL OversizedTarget(uint32_t i,MhmTargetInfo* info){const auto result=raceApi.target_at(i,info);if(result==MHM_OK&&advertisedKeys)info->key_count=advertisedKeys;return result;}
inline void TestBoundaryAndLayers(){
    const auto h=Attach(MHM_BODY,3);raceApi=Api();auto api=raceApi;api.target_at=OversizedTarget;morphpanel::Session session;session.Connect(api);morphpanel::Snapshot s;session.Snapshot(s);advertisedKeys=1025;
    Check(session.Snapshot(s)==MHM_CAPACITY&&s.keyCount==3,"actual 1025 rejected without publishing");advertisedKeys=0;
    uint64_t lower=0,revision=0;api.open_source("lower",100,60000,&lower);const MhmUpdate under{h,2,.8f};api.submit(lower,&under,1,&revision);const MhmUpdate zero{h,2,0};session.Replace({&zero,1});session.Snapshot(s);
    Check(s.keys.back().owned&&s.keys.back().state.requested==0,"owned zero masks lower layer");session.Release();session.Snapshot(s);Check(!s.keys.back().owned&&s.keys.back().state.requested==.8f,"release reveals lower layer");api.close_source(lower);Core().Detach(h);
}
inline uint64_t changeOnTarget=0,changingSource=0;inline std::vector<MhmUpdate> changedValues;
inline MhmResult MHM_CALL ChangingRead(uint64_t h,MhmWeightState* values,uint32_t capacity,uint32_t* count,uint64_t* revision,uint64_t* applied){
    if(h==changeOnTarget){changeOnTarget=0;uint64_t receipt=0;raceApi.submit(changingSource,changedValues.data(),static_cast<uint32_t>(changedValues.size()),&receipt);}
    return raceApi.read_weights(h,values,capacity,count,revision,applied);
}
inline void TestReadRevisionConsistency(){
    const auto body=Attach(MHM_BODY,3),arm=Attach(MHM_ARMS,3);raceApi=Api();auto api=raceApi;api.read_weights=ChangingRead;morphpanel::Session session;session.Connect(api);
    api.open_source("changing",100,60000,&changingSource);changedValues={{body,0,.3f},{arm,0,.4f}};uint64_t receipt=0;api.submit(changingSource,changedValues.data(),2,&receipt);
    morphpanel::Snapshot s;session.Snapshot(s);const auto before=s.revision;changedValues={{body,0,.8f},{arm,0,.9f}};changeOnTarget=arm;
    Check(session.Snapshot(s)==MHM_NOT_READY&&s.revision==before&&s.keys[0].state.requested==.3f,"mixed requested revisions never publish a partial snapshot");
    Check(session.Snapshot(s)==MHM_OK&&s.keys[0].state.requested==.8f&&s.keys[3].state.requested==.9f,"next stable scan reads complete changed batch");
    uint64_t frame=0;const float weights[]{.8f,0,0};Check(Core().CaptureInterpolated(body,weights,s.revision,frame)==MHM_OK&&Core().Acknowledge(frame)==MHM_OK,"publish only one synthetic target");Core().ReleaseFrame(frame);
    Check(session.Snapshot(s)==MHM_OK&&s.targets[0].appliedRevision!=s.targets[1].appliedRevision,"independent applied revisions remain valid");
    api.close_source(changingSource);Core().Detach(body);Core().Detach(arm);
}
inline void SessionTests(){TestActualCounts();TestAtomicChangeMany();TestSnapshotAtomicRead();TestBoundaryAndLayers();TestReadRevisionConsistency();Check(sizeof(mhmcontrol::Request)==40&&sizeof(mhmcontrol::Response)==39256,"legacy wire layout unchanged");}
}
