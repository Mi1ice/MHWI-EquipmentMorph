#pragma once
namespace fivepartlifecycletests {
inline unsigned Run(const std::filesystem::path& path){
    unsigned checks=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    retirementtest::Arena arena;privateprobe::Log log;
    log.file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(log.file!=INVALID_HANDLE_VALUE,"five part lifecycle log opened");
    std::array<std::unique_ptr<retirementtest::Fixture>,5> fixtures;
    MhmApi api{};std::uint64_t source=0,revision=0;
    check(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK&&api.open_source("five-part-held",220,60000,&source)==MHM_OK,"one source spans five same-name keys");
    std::array<float,5> weights{.25f,.5f,.75f,1.f,.25f};
    std::uint64_t counter=300;
    for(unsigned part=0;part<5;part++){
        mhm::Asset asset;asset.part=part;asset.model="pl/synthetic/part"+std::to_string(part)+"A";asset.basis.resize(64);
        mhm::Key key;strcpy_s(key.info.id,"same");key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);
        fixtures[part]=std::make_unique<retirementtest::Fixture>(asset,std::array<std::uint64_t,3>{UINT64_MAX,UINT64_MAX,UINT64_MAX});
        fixtures[part]->Bind(arena,counter);
    }
    const auto submitAll=[&]{std::array<MhmUpdate,5> batch{};for(unsigned p=0;p<5;p++)batch[p]={fixtures[p]->part.target,0,weights[p]};
        check(api.submit(source,batch.data(),5,&revision)==MHM_OK,"source submission atomically replaces its complete five-part contribution");};
    submitAll();
    const auto read=[&](unsigned part){MhmWeightState state{};std::uint32_t count=0;std::uint64_t req=0,app=0;
        check(api.read_weights(fixtures[part]->part.target,&state,1,&count,&req,&app)==MHM_OK,"five part state remains readable");return state;};
    const auto replace=[&](unsigned part,bool bodyB){
        auto& f=*fixtures[part];const auto old=f.part.target;const auto oldEpoch=f.part.epoch;
        std::array<std::uint64_t,5> handles{};for(unsigned p=0;p<5;p++)handles[p]=fixtures[p]->part.target;
        arena.Counter(counter+1);arena.drawObject.completed=counter+1;
        f.part.adapter->Invalidate(0,"equipment_part_model_replaced");facecontinuous::EndPart(log,f.part,arena.upload,arena.drawing);
        const MhmUpdate stale{old,0,.9f};
        check(api.submit(source,&stale,1,&revision)==MHM_STALE&&api.heartbeat(source)==MHM_OK,"old nonzero submit rejected without closing shared source");
        for(unsigned p=0;p<5;p++)if(p!=part)check(fixtures[p]->part.target==handles[p]&&read(p).requested==weights[p],"single-part retirement preserves all four surviving contributions");
        f.CpuDrain(arena,counter);arena.Counter(counter+2);arena.drawObject.completed=counter+2;arena.drawing.Poll();
        facecontinuous::DrainStep(log,f.part,arena.upload,arena.drawing);
        check(f.part.reusable&&!f.part.retiring&&f.part.epoch>oldEpoch,"retirement needs successor and dual GPU completion");
        auto asset=std::make_shared<mhm::Asset>(*f.part.asset);asset->model="pl/synthetic/part"+std::to_string(part)+(bodyB?"B":"A");f.part.asset=asset;
        counter+=10;f.Bind(arena,counter);
        check(f.part.target!=old&&read(part).requested==0&&read(part).source==0,"same-name replacement starts neutral with a fresh target");
        submitAll();
        std::vector<float> requested;std::uint64_t req=0;launchpool::MorphVersion version;
        check(f.part.binding->Requested(1,requested,req)&&f.part.binding->Capture(requested,req,version),"fresh replacement captures production host frame");
        std::array<unsigned char,64> output{};float x=0;
        check(MhwMorphHostCompose(version.frame,f.part.target,output.data(),64)==MHM_OK,"fresh replacement composes through production API");
        std::memcpy(&x,output.data(),4);check(x==weights[part],"replacement composition contains only its own same-name key");
        check(MhwMorphHostReleaseFrame(version.frame)==MHM_OK,"offline composition releases its acquired host frame");f.part.binding->pending.erase(version.frame);
    };
    for(unsigned part=0;part<5;part++)for(unsigned round=0;round<3;round++){
        const float saved=weights[part];weights[part]=round==1?0.f:saved;
        submitAll();
        replace(part,false);weights[part]=saved;
        submitAll();
    }
    for(unsigned round=0;round<3;round++){replace(MHM_BODY,true);replace(MHM_BODY,false);}
    check(api.close_source(source)==MHM_OK,"five part shared source closes after all replacements");
    return checks;
}
}
