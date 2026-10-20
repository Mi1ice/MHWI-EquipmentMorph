#define NOMINMAX
#include "Control.hpp"
#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include <cstdio>
#include "preset_management_tests.hpp"

namespace {
MhmApi original{};unsigned submissions=0;
MhmResult MHM_CALL CountSubmit(uint64_t source,const MhmUpdate* updates,uint32_t count,uint64_t* revision){++submissions;return original.submit(source,updates,count,revision);}
uint64_t Attach(uint32_t part,const char* model,char modelHash,char packHash,bool exclusive=false){
    mhm::Asset asset;asset.part=part;asset.model=model;asset.basis.resize(64);asset.modelSha=std::string(64,modelHash);asset.packSha=std::string(64,packHash);
    for(const char* id:{"A","B"}){mhm::Key key;strcpy_s(key.info.id,id);key.info.maximum=1;key.info.exclusive_group=exclusive?1u:0u;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);}
    uint64_t handle=0;if(Core().Attach(std::move(asset),handle)!=MHM_OK)throw std::runtime_error("attach");return handle;
}
}
int wmain(int argc,wchar_t** argv){try{
    using namespace morphpreset;using mhmcontrol::Action;
    unsigned checks=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    const auto directory=std::filesystem::absolute(argv[0]).parent_path()/"preset-fixtures"/NewId();
    Store store(directory);check(store.Load().documents.empty(),"empty preset directory is a valid first-run state");
    check(MhwMorphGetApi(MHM_API_VERSION,sizeof(original),&original)==MHM_OK,"legacy v1 API layout retained");
    auto api=original;api.submit=CountSubmit;const auto body=Attach(MHM_BODY,"pl/test/body",'a','b');auto arm=Attach(MHM_ARMS,"pl/test/arm",'c','d',true);
    MhmAssetIdentity identity{};check(MhwMorphGetAssetIdentity(1,sizeof(identity),body,&identity)==MHM_OK&&identity.handle==body&&std::string(identity.model_sha256)==std::string(64,'a'),"verified loaded identity returned");
    const auto savedIdentity=identity;check(MhwMorphGetAssetIdentity(2,sizeof(identity),body,&identity)==MHM_VERSION&&identity.handle==savedIdentity.handle,"identity version rejection preserves output");
    check(MhwMorphGetAssetIdentity(1,sizeof(identity)-1,body,&identity)==MHM_VERSION&&MhwMorphGetAssetIdentity(1,sizeof(identity),body,nullptr)==MHM_INVALID,"identity size and pointer validation");
    {mhm::Asset asset;asset.part=MHM_HEAD;asset.model="pl/test/unverified";asset.basis.resize(64);mhm::Key key;strcpy_s(key.info.id,"A");key.info.maximum=1;asset.keys.push_back(key);uint64_t handle=0;Core().Attach(asset,handle);
        check(MhwMorphGetAssetIdentity(1,sizeof(identity),handle,&identity)==MHM_NOT_READY&&identity.handle==savedIdentity.handle,"unverified legacy targets never invent an asset fingerprint");Core().Detach(handle);}
    uint64_t underlying=0,revision=0;api.open_source("underlying",100,60000,&underlying);const MhmUpdate base{body,0,.2f};api.submit(underlying,&base,1,&revision);
    morphgui::Control control;control.ConfigurePresets(directory);control.Connect(api,MhwMorphGetAssetIdentity);uint64_t now=0;
    auto service=[&](){control.Service(now+=20,true);};
    auto state=[&](uint64_t target,uint32_t key){const auto data=control.Snapshot()->data;for(uint32_t i=0;i<data.keyCount;i++)if(data.keys[i].target==target&&data.keys[i].index==key)return data.keys[i];throw std::runtime_error("missing key");};
    auto request=[&](morphpreset::Action action,std::string id={},std::string name={}){control.PostPreset({action,std::move(id),std::move(name)});service();return control.Snapshot()->presets.success;};
    const float precise=.123456789f;
    control.Post(Action::Set,body,0,precise);control.Post(Action::Set,body,1,0);control.Post(Action::Set,arm,0,.4f);
    check(request(morphpreset::Action::SaveAs,{},"整套测试 \"一\""),"save drains accepted edits before capturing all controlled parts");
    const auto firstId=control.Snapshot()->presets.activeId;auto first=store.Load().documents.front();
    check(first.parts.size()==2&&first.parts[0].keys.size()==2&&first.parts[0].keys[0].weight==precise&&first.parts[0].keys[1].weight==0,"preset retains exact float32 values and owned zero");
    check(first.parts[0].modelSha==std::string(64,'a')&&first.parts[0].packSha==std::string(64,'b'),"preset records both model and morph-pack digests");
    check(state(body,0).state.applied==0&&first.parts[0].keys[0].weight==precise,"saving records contribution instead of smoothed applied weight");
    const auto encoded=Encode(first);const auto decoded=Decode(encoded);
    check(decoded.name==first.name&&decoded.parts[0].keys==first.parts[0].keys,"UTF-8 escaped-name and float32 JSON round trip");
    check(!control.Snapshot()->presets.dirty,"saving establishes an unmodified baseline");
    control.Post(Action::Set,body,0,.8f);service();check(control.Snapshot()->presets.dirty,"accepted edit marks active preset dirty");
    control.Post(Action::Drop,arm,0);const auto leg=Attach(MHM_LEGS,"pl/test/leg",'e','f');control.Post(Action::Set,leg,0,.9f);service();
    const auto before=submissions;
    check(request(morphpreset::Action::Apply,firstId)&&submissions==before+1,"whole preset is replaced using one atomic API submit");
    check(state(body,0).contribution==precise&&state(body,1).owned&&state(body,1).contribution==0&&state(arm,0).owned&&!state(leg,0).owned,"apply restores all saved parts and drops former panel controls outside the preset");
    check(!control.Snapshot()->presets.dirty,"successful apply resets modified state");
    control.Post(Action::Set,body,0,.6f);service();check(request(morphpreset::Action::Save,firstId),"overwrite saves active preset");
    check(store.Load().documents.front().parts[0].keys[0].weight==.6f,"overwrite persists accepted target value");
    check(!request(morphpreset::Action::SaveAs,{},first.name)&&store.Load().documents.size()==1,"duplicate display names do not overwrite another preset");
    check(!request(morphpreset::Action::SaveAs,{},"   "),"blank preset names are rejected");
    check(request(morphpreset::Action::SaveAs,{},"恢复保存"),"saving can recover after a preset validation error");
    auto conflict=first;conflict.id=NewId();conflict.name="冲突测试";conflict.parts[1].keys.push_back({"B",.5f});store.Save(conflict);
    control.ConfigurePresets(directory);const auto conflictBefore=submissions;
    check(!request(morphpreset::Action::Apply,conflict.id)&&submissions==conflictBefore+1&&state(body,0).contribution==.6f&&!state(arm,1).owned,"exclusive-group batch rejection preserves every previous contribution");
    check(request(morphpreset::Action::Apply,firstId),"valid preset applies after a rejected batch");
    auto changed=first;changed.id=NewId();changed.name="同名模型不同内容";changed.parts[0].modelSha=std::string(64,'f');store.Save(changed);control.ConfigurePresets(directory);
    const auto mismatchBefore=submissions;
    check(!request(morphpreset::Action::Apply,changed.id)&&submissions==mismatchBefore&&state(body,0).contribution==.6f,"same resource path with different MOD3 digest never submits");
    changed.id=NewId();changed.name="数据包变化";changed.parts[0].modelSha=first.parts[0].modelSha;changed.parts[0].packSha=std::string(64,'f');store.Save(changed);control.ConfigurePresets(directory);
    check(!request(morphpreset::Action::Apply,changed.id)&&submissions==mismatchBefore,"changed morph deltas are incompatible even with the same model and key names");
    Core().Detach(arm);service();const auto absentBefore=submissions;
    check(!request(morphpreset::Action::Apply,firstId)&&submissions==absentBefore&&state(body,0).contribution==.6f,"missing part rejects the entire preset without partially changing body");
    check(MhwMorphGetAssetIdentity(1,sizeof(identity),arm,&identity)==MHM_STALE,"retired handles cannot query identities");
    arm=Attach(MHM_ARMS,"pl/test/arm",'c','d',true);control.Service(now+=300,true);
    check(!state(arm,0).owned&&state(arm,0).state.requested==0,"matching re-equipped model is not automatically controlled");
    check(request(morphpreset::Action::Apply,firstId)&&state(arm,0).contribution==.4f,"manual apply resolves fresh handles using stable part and key identities");
    control.Post(Action::Set,body,0,.125f);control.PostPreset({morphpreset::Action::SaveAs,{},"先保存"});
    control.Post(Action::Set,body,0,.875f);control.PostPreset({morphpreset::Action::SaveAs,{},"后保存"});service();
    const auto ordered=store.Load();float early=-1,late=-1;for(const auto& doc:ordered.documents){if(doc.name=="先保存")early=doc.parts[0].keys[0].weight;if(doc.name=="后保存")late=doc.parts[0].keys[0].weight;}
    check(early==.125f&&late==.875f,"save barriers preserve order relative to later edits");
    const auto count=store.Load().documents.size();control.PostPreset({morphpreset::Action::SaveAs,{},"已取消"});control.PostPreset({morphpreset::Action::Apply,firstId,{}});control.Post(Action::Release);service();
    check(store.Load().documents.size()==count&&!control.Snapshot()->data.source&&state(body,0).state.requested==.2f,"close release cancels queued preset operations and preserves other sources");
    control.Post(Action::Set,body,0,0);check(request(morphpreset::Action::SaveAs,{},"零权重接管"),"an explicitly owned zero can form a preset");
    check(state(body,0).owned&&state(body,0).state.requested==0,"owned zero still masks a lower-priority source");
    control.Post(Action::Drop,body,0);check(!request(morphpreset::Action::SaveAs,{},"空控制"),"unowned empty contribution set is not silently saved");
    const auto rejectedCount=store.Load().documents.size();control.Post(Action::Set,body,0,1.1f);
    check(!request(morphpreset::Action::SaveAs,{},"无效编辑")&&store.Load().documents.size()==rejectedCount,"rejected pending edit prevents saving an unintended earlier value");
    uint64_t higher=0;api.open_source("higher",250,60000,&higher);const MhmUpdate masked{body,0,.9f};api.submit(higher,&masked,1,&revision);
    control.Post(Action::Set,body,0,.45f);check(request(morphpreset::Action::SaveAs,{},"独立贡献"),"panel contribution can be saved while another source has priority");
    bool contributionOnly=false;for(const auto& doc:store.Load().documents)if(doc.name=="独立贡献")contributionOnly=doc.parts[0].keys[0].weight==.45f;
    check(contributionOnly&&state(body,0).state.requested==.9f,"preset never captures another plugin's winning weight");api.close_source(higher);
    morphgui::Control legacy;legacy.ConfigurePresets(directory);legacy.Connect(api);
    check(!legacy.Snapshot()->presets.available&&legacy.Snapshot()->ready,"older cores retain ordinary controls while presets are disabled");
    auto reject=[&](const std::string& text,const char* why){bool refused=false;try{Decode(text);}catch(const std::exception&){refused=true;}check(refused,why);};
    reject(encoded.substr(0,encoded.size()-3),"truncated JSON rejected");
    reject(encoded+"x","trailing data rejected");
    reject("{\"schema\":1,\"schema\":1}","duplicate JSON properties rejected");
    reject(std::string(256*1024+1,' '),"oversized JSON rejected");
    reject("[[[[[[[[[[0]]]]]]]]]]","deeply nested JSON rejected");
    auto bad=encoded;bad.replace(bad.find("\"schema\": 1"),11,"\"schema\": 2");reject(bad,"unknown schema version rejected");
    bad=encoded;const auto weightAt=bad.find("\"weight\": ")+10;bad.replace(weightAt,bad.find('}',weightAt)-weightAt,"1e999");reject(bad,"non-finite weights rejected");
    bad=encoded;bad.replace(bad.find("\"name\": ")+8,1,"0,");reject(bad,"wrong JSON type rejected");
    auto invalid=first;invalid.parts.push_back(first.parts.front());bool refused=false;try{Encode(invalid);}catch(const std::exception&){refused=true;}check(refused,"duplicate part rejected");
    invalid=first;invalid.parts[0].keys.push_back(first.parts[0].keys[0]);refused=false;try{Encode(invalid);}catch(const std::exception&){refused=true;}check(refused,"duplicate key rejected");
    invalid=first;invalid.id="../escape";refused=false;try{store.Save(invalid);}catch(const std::exception&){refused=true;}check(refused,"unsafe preset identifiers cannot become file paths");
    const auto path=directory/(firstId+".json");SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY);
    auto replacement=first;replacement.name="不应写入";refused=false;try{store.Save(replacement);}catch(const std::exception&){refused=true;}SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
    bool intact=false;for(const auto& doc:store.Load().documents)if(doc.id==firstId)intact=doc.name==first.name;
    check(refused&&intact,"failed atomic overwrite retains the previous preset file");
    {std::ofstream file(directory/"malformed.json");file<<"broken";}
    check(store.Load().invalid==1,"one malformed file does not hide valid presets");
    control.Post(Action::Release);service();checks+=managementtest::Run(directory/L"management",original,first);
    if(argc==2){
        const auto module=LoadLibraryW(argv[1]);check(module!=nullptr,"load built candidate without a game or test window");
        using Getter=decltype(&MhwMorphGetApi);Getter getter=nullptr;IdentityQuery identityGetter=nullptr;
        const auto address=GetProcAddress(module,"MhwMorphGetApi"),extension=GetProcAddress(module,"MhwMorphGetAssetIdentity");std::memcpy(&getter,&address,sizeof(getter));std::memcpy(&identityGetter,&extension,sizeof(identityGetter));MhmApi table{};
        check(getter&&identityGetter&&getter(MHM_API_VERSION,sizeof(table),&table)==MHM_OK&&identityGetter(1,sizeof(identity),0,&identity)==MHM_STALE,"built DLL exports legacy API and versioned identity extension");FreeLibrary(module);
    }
    control.Post(Action::Release);service();Core().Detach(leg);
    std::printf("PASS: %u preset checks; synthetic assets and local files; no windows or game process\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
