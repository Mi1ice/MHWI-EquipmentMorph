#pragma once
#include "../../tools/morph_reference_json.hpp"
namespace capacitytests {
inline mhm::Asset Asset(unsigned count){
    mhm::Asset asset;asset.part=MHM_BODY;asset.model="pl/synthetic/host-capacity";asset.basis.resize(64);
    for(unsigned i=0;i<count;++i){mhm::Key key;const auto name="capacity_"+std::to_string(i);std::memcpy(key.info.id,name.c_str(),name.size()+1);key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(std::move(key));}
    return asset;
}
inline unsigned Run(){
    using launchpool::MorphVersion;using inputfasttest::Need;
    unsigned count=0;auto check=[&](bool ok,const char* message){if(!ok)throw std::runtime_error(message);++count;};
    MorphVersion empty;
    check(empty.Weights().empty()&&empty.Valid()&&empty.IsZero(),"empty uncaptured snapshot is valid zero");
    for(unsigned size:{65u,489u,1024u}){
        std::vector<float> input(size);input.back()=.75f;
        auto snapshot=MorphVersion::FromWeights(input,7,9);const auto copy=snapshot;
        check(snapshot.Valid()&&!snapshot.IsZero()&&snapshot.Weights().size()==size&&snapshot.Weights().back()==.75f,"snapshot owns actual count and final key");
        check(copy.Weights().data()==snapshot.Weights().data(),"snapshot copies share immutable owned storage");
        input.assign(size,0);input.clear();input.shrink_to_fit();
        check(copy.Weights().back()==.75f,"snapshot survives changed and freed source memory");
        std::vector<float> same(size);same.back()=.75f;
        const auto independent=MorphVersion::FromWeights(same,7,9);
        check(independent.Weights().data()!=snapshot.Weights().data()&&independent==snapshot,"snapshot equality compares values rather than allocation identity");
        auto changed=snapshot;changed.frame++;check(changed!=snapshot,"frame participates in snapshot equality");
        changed=snapshot;changed.revision++;check(changed!=snapshot,"revision participates in snapshot equality");
        same.push_back(0);
        if(size<1024)check(MorphVersion::FromWeights(same,7,9)!=snapshot,"different declared counts are not equal");
        equipmentmorph::PartBinding binding;binding.Attach(Asset(size));std::uint64_t source=0,revision=0;
        Need(binding.api.open_source("host-capacity",20,60000,&source)==MHM_OK);
        const MhmUpdate update{binding.target,size-1,.75f};Need(binding.api.submit(source,&update,1,&revision)==MHM_OK);
        std::vector<float> requested;std::uint64_t readRevision=0;
        check(binding.Requested(size,requested,readRevision)&&requested.size()==size&&requested.back()==.75f,"production binding reads high final key");
        MorphVersion captured;check(binding.Capture(requested,readRevision,captured)&&captured.Weights().size()==size,"production binding captures actual count");
        std::array<unsigned char,64> output{};
        Need(MhwMorphHostCompose(captured.frame,binding.target,output.data(),static_cast<unsigned>(output.size()))==MHM_OK);
        float position=0;std::memcpy(&position,output.data(),4);check(position==.75f,"captured final key reaches production composition");
        const auto old=binding.target;Need(MhwMorphHostDetach(old)==MHM_OK);
        check(!binding.Requested(size,requested,readRevision),"retired target read rejected");
        check(MhwMorphHostCompose(captured.frame,old,output.data(),static_cast<unsigned>(output.size()))==MHM_STALE,"retired frame composition rejected");
        binding.target=0;Need(binding.api.close_source(source)==MHM_OK);
    }
    {
        std::vector<float> invalid(1024);invalid.back()=std::numeric_limits<float>::quiet_NaN();
        check(!MorphVersion::FromWeights(invalid,1,1).Valid(),"snapshot finite-value validation preserved");
        invalid.back()=1.01f;check(!MorphVersion::FromWeights(invalid,1,1).Valid(),"snapshot weight range preserved");
        bool rejected=false;try{MorphVersion::FromWeights(std::vector<float>(1025),1,1);}catch(const std::exception&){rejected=true;}
        check(rejected,"snapshot rejects count above declared trial capacity");
    }
    {
        equipmentmorph::PartBinding binding;binding.Attach(Asset(1024));std::vector<float> values(1024);values.back()=.75f;
        std::uint64_t source=0,revision=0;Need(binding.api.open_source("host-fault",20,60000,&source)==MHM_OK);
        const MhmUpdate update{binding.target,1023,.75f};Need(binding.api.submit(source,&update,1,&revision)==MHM_OK);
        unsigned failures=0;
        for(size_t failAt=0;failAt<9;++failAt){
            MorphVersion version;hostallocationtest::calls=0;hostallocationtest::failAt=failAt;hostallocationtest::enabled=true;
            try{if(!binding.Capture(values,revision,version))++failures;}catch(const std::exception&){++failures;}
            hostallocationtest::enabled=false;
            for(auto frame:binding.pending)Need(MhwMorphHostReleaseFrame(frame)==MHM_OK);binding.pending.clear();
            bool all=true;for(unsigned i=0;i<4;++i)all&=binding.Capture(values,revision,version);
            check(all&&binding.pending.size()==4,"allocation fault releases every acquired host frame");
            for(auto frame:binding.pending)Need(MhwMorphHostReleaseFrame(frame)==MHM_OK);binding.pending.clear();
        }
        check(failures>0,"allocation failure injection exercised capture paths");Need(binding.api.close_source(source)==MHM_OK);
    }
    {
        inputfasttest::Fixture f(1024);std::vector<float> values(1024);values.back()=.75f;
        const auto snapshot=MorphVersion::FromWeights(values,1,1);Need(f.Adapter().Request(snapshot));
        f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();f.Input(1,true);f.Input(3,true);
        hostallocationtest::calls=0;hostallocationtest::failAt=SIZE_MAX;hostallocationtest::enabled=true;
        bool shared=true;
        for(unsigned i=0;i<100;++i){const auto view=f.Adapter().View();shared&=view.requested.Weights().data()==snapshot.Weights().data();f.Input(1,true);f.Input(3,true);}
        hostallocationtest::enabled=false;
        check(shared&&hostallocationtest::calls==0,"prepared callback views allocate nothing and do not deep-copy weights");
    }
    for(float value:{.234567895f,std::numeric_limits<float>::denorm_min()}){
        inputfasttest::Fixture f(1024);const std::vector<float> values(1024,value);
        Need(f.Adapter().Request(MorphVersion::FromWeights(values,1,1)));
        struct Log {std::string text;bool Write(const char* line){text=line;return true;}} log;
        facecontinuous::WriteState(log,f.Part(),"part_state");
        auto parsed=morphreference::json::Reader(log.text).Parse();
        const auto& initial=morphreference::json::Field(parsed,"applied",morphreference::json::Value::Array).array;
        check(initial.size()==1024&&std::all_of(initial.begin(),initial.end(),[](const auto& w){return w.number==0;}),"empty initial applied snapshot logs actual target zero count");
        f.Queue();Need(f.Adapter().Completed(20,100));f.Cycle();facecontinuous::WriteState(log,f.Part(),"part_state");
        parsed=morphreference::json::Reader(log.text).Parse();
        for(const char* field:{"requested","applied"}){
            const auto& array=morphreference::json::Field(parsed,field,morphreference::json::Value::Array).array;
            check(array.size()==1024&&std::all_of(array.begin(),array.end(),[&](const auto& w){return static_cast<float>(w.number)==value;}),"1024 ordinary/subnormal log weights parse completely");
        }
        check(log.text.size()>4096&&log.text.size()<=65536&&log.text.back()=='\n',"large complete log remains within existing writer guard");
    }
    std::printf("Host capacity: %u checks passed\n",count);return count;
}
}
