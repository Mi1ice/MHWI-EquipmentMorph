#pragma once
#include "include/equipment_morph_client.hpp"
#define mhwi frozen_v040
#include "tests/fixtures/v040/equipment_morph_key_client.hpp"
#undef mhwi
#define morphpartclient internalpartclient
#include "../../runtime/EquipmentMorphPartClient/Client.hpp"
#undef morphpartclient
#include <limits>

namespace capacitytests {
inline MhmApi realApi{};
inline uint32_t largestRead=0,readCalls=0;
inline bool retireOnRead=false;
inline MhmResult MHM_CALL ObservedRead(uint64_t target,MhmWeightState* values,uint32_t capacity,uint32_t* count,uint64_t* req,uint64_t* app){
    largestRead=std::max(largestRead,capacity);++readCalls;
    const auto result=realApi.read_weights(target,values,capacity,count,req,app);
    if(retireOnRead){retireOnRead=false;MhwMorphHostDetach(target);}return result;
}
inline void Check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
inline std::vector<std::string> Ids(uint32_t count){std::vector<std::string> ids;for(uint32_t i=0;i<count;++i)ids.push_back("Key_"+std::to_string(i));return ids;}
inline uint64_t Attach(uint32_t part,uint32_t count){
    mhm::Asset asset;asset.part=part;asset.model="pl/synthetic/body";asset.basis.resize(24);
    for(const auto& id:Ids(count)){mhm::Key key;std::memcpy(key.info.id,id.c_str(),id.size()+1);key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(std::move(key));}
    uint64_t handle=0;Check(Core().Attach(std::move(asset),handle)==MHM_OK,"attach capacity fixture");return handle;
}
inline void Clean(){for(;;){MhmTargetInfo info{};if(realApi.target_at(0,&info)!=MHM_OK)break;Check(MhwMorphHostDetach(info.handle)==MHM_OK,"cleanup");}}

inline void TestLargeDiscoveryAndRead(){
    for(uint32_t count:{3u,489u,1024u}){
        const auto handle=Attach(MHM_BODY,count);auto observed=realApi;observed.read_weights=ObservedRead;
        largestRead=readCalls=0;
        mhwi::KeyClient named;Check(named.Connect(observed)==MHM_OK&&named.Refresh()==MHM_OK&&named.Keys().size()==count,"large named discovery");
        uint64_t revision=0,applied=0;std::vector<MhmWeightState> weights;
        Check(named.Read(handle,weights,revision,applied)==MHM_OK&&weights.size()==count&&largestRead==count,"named read allocates actual count");
        morphpartclient::Client publicPart;Check(publicPart.Connect(observed)==MHM_OK&&publicPart.Refresh()==MHM_OK&&publicPart.Specifications()[0].ids.size()==count,"public full discovery");
        std::vector<morphpartclient::PartState> state;Check(publicPart.Read(state,revision)==MHM_OK&&state[0].weights.size()==count&&largestRead==count,"public full read actual count");
        internalpartclient::Client internal;Check(internal.Connect(observed,{{"pl/synthetic/body",MHM_BODY,Ids(count)}})==MHM_OK&&internal.Refresh()==MHM_OK,"internal large discovery");
        std::vector<internalpartclient::PartState> internalState;Check(internal.Read(internalState,revision)==MHM_OK&&internalState[0].weights.size()==count,"internal large read");
        Check(MhwMorphHostDetach(handle)==MHM_OK,"remove discovery fixture");
    }
}

inline void TestFullBatchAtomicity(){
    std::array<uint64_t,5> handles{};for(uint32_t part=0;part<5;++part)handles[part]=Attach(part,1024);
    mhwi::KeyClient named;Check(named.Connect(realApi)==MHM_OK,"connect full named");
    std::vector<mhwi::NamedWeight> changes;
    for(uint32_t part=0;part<5;++part)for(const auto& id:Ids(1024))changes.push_back({part,"pl/synthetic/body",id,.625f});
    uint64_t receipt=0;Check(named.Set(changes,receipt)==MHM_OK&&named.Keys().size()==5120,"complete 5120 named batch");
    auto verify=[&](uint64_t expected){for(const auto h:handles){std::vector<MhmWeightState> values;uint64_t req=0,app=0;Check(named.Read(h,values,req,app)==MHM_OK&&req==expected&&values.size()==1024&&std::all_of(values.begin(),values.end(),[](const auto& w){return w.requested==.625f;}),"whole contribution and revision retained");}};
    verify(receipt);auto bad=changes;bad.back()=bad.front();uint64_t failed=123;
    Check(named.Set(bad,failed)==MHM_INVALID&&failed==123,"named duplicate fails atomically");verify(receipt);
    bad=changes;bad.back().weight=NAN;Check(named.Set(bad,failed)==MHM_INVALID,"named NaN fails atomically");verify(receipt);
    bad=changes;bad.back().key="missing";Check(named.Set(bad,failed)==MHM_NOT_READY,"named missing fails atomically");verify(receipt);
    Check(named.Release()==MHM_OK,"release named batch");
    morphpartclient::Client full;Check(full.Connect(realApi)==MHM_OK&&full.Refresh()==MHM_OK,"connect public batch");
    std::vector<morphpartclient::PartValues> fullChanges;for(uint32_t part=0;part<5;++part)fullChanges.push_back({part,std::vector<float>(1024,.625f)});
    Check(full.Set(fullChanges,receipt)==MHM_OK,"full client 5120 batch");verify(receipt);
    auto invalid=fullChanges;invalid.back().weights.back()=-1;
    Check(full.Set(invalid,failed)==MHM_INVALID,"full client invalid atomicity");verify(receipt);
    Check(full.Release()==MHM_OK,"release full batch");
    internalpartclient::Client internal;std::vector<internalpartclient::TargetSpec> specs;
    std::vector<internalpartclient::PartValues> internalChanges;
    for(uint32_t part=0;part<5;++part){specs.push_back({"pl/synthetic/body",part,Ids(1024)});internalChanges.push_back({part,std::vector<float>(1024,.625f)});}
    Check(internal.Connect(realApi,specs)==MHM_OK&&internal.Refresh()==MHM_OK&&internal.Set(internalChanges,receipt)==MHM_OK,"internal client 5120 batch");verify(receipt);
    Check(MhwMorphHostDetach(handles.back())==MHM_OK,"retire full member");
    const auto newHandle=Attach(4,3);Check(internal.Set(internalChanges,failed)==MHM_STALE,"internal stale batch");
    std::vector<MhmWeightState> values;uint64_t req=0,app=0;
    Check(named.Read(newHandle,values,req,app)==MHM_OK&&values.size()==3&&values.back().requested==0,"new handle has no replay");
    Check(internal.Refresh()==MHM_NOT_READY,"changed model count does not reuse old mapping");
}

inline void TestRetiredDuringRead(){
    auto handle=Attach(MHM_BODY,489);auto observed=realApi;observed.read_weights=ObservedRead;
    mhwi::KeyClient named;Check(named.Connect(observed)==MHM_OK&&named.Refresh()==MHM_OK,"race client connect");
    std::vector<MhmWeightState> values(1);values[0].requested=.125f;uint64_t req=123,app=456;
    retireOnRead=true;readCalls=0;
    Check(named.Read(handle,values,req,app)==MHM_STALE&&readCalls<=2,"retirement between required count and payload read");
    Check(values.size()==1&&values[0].requested==.125f&&req==123&&app==456,"failed read leaves output untouched");
    handle=Attach(MHM_BODY,3);Check(named.Refresh()==MHM_OK&&named.Keys().size()==3&&named.Keys()[0].target==handle,"refresh new handle and count");
    morphpartclient::Client full;Check(full.Connect(observed)==MHM_OK&&full.Refresh()==MHM_OK,"full race connect");
    std::vector<morphpartclient::PartState> output;retireOnRead=true;readCalls=0;
    Check(full.Read(output,req)==MHM_STALE&&output.empty()&&readCalls<=2,"full client retirement read is bounded");
    handle=Attach(MHM_BODY,489);internalpartclient::Client internal;
    Check(internal.Connect(observed,{{"pl/synthetic/body",MHM_BODY,Ids(489)}})==MHM_OK&&internal.Refresh()==MHM_OK,"internal race connect");
    std::vector<internalpartclient::PartState> internalOutput;retireOnRead=true;readCalls=0;
    Check(internal.Read(internalOutput,req)==MHM_STALE&&internalOutput.empty()&&readCalls<=2,"internal retirement read is bounded");
}

inline void TestFrozenV040RefusesLargeLimits(){frozen_v040::KeyClient frozen;Check(frozen.Connect(realApi)==MHM_CAPACITY,"frozen v040 explicitly refuses large limits");}

inline unsigned Run(){
    Check(MhwMorphGetApi(MHM_API_VERSION,sizeof(realApi),&realApi)==MHM_OK,"capacity API");unsigned passed=0,failed=0;
    for(const auto& test:{std::pair{"TestLargeDiscoveryAndRead",&TestLargeDiscoveryAndRead},std::pair{"TestFullBatchAtomicity",&TestFullBatchAtomicity},std::pair{"TestRetiredDuringRead",&TestRetiredDuringRead},std::pair{"TestFrozenV040RefusesLargeLimits",&TestFrozenV040RefusesLargeLimits}}){
        try{test.second();++passed;std::printf("PASS: %s\n",test.first);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",test.first,e.what());}Clean();
    }
    std::printf("SDK capacity cases: %u/4\n",passed);if(failed)throw std::runtime_error("SDK capacity cases failed");return passed;
}
}
