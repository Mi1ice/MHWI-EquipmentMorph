#define NOMINMAX
#include "asset_catalog.hpp"
#include "part_smoothing.hpp"
#include "../EquipmentMorphPartClient/Client.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks=0;
void Check(bool value,const char* label){if(!value){std::printf("FAIL: %s\n",label);std::exit(1);}checks++;}
std::string Ascii(const wchar_t* value){std::string result;while(*value){if(*value>=128)throw std::runtime_error("Non-ASCII hash");result+=static_cast<char>(*value++);}return result;}
MhmHostTarget Descriptor(const mhm::Asset& asset,std::vector<MhmHostKey>& keys) {
    for(const auto& k:asset.keys)keys.push_back({k.info,k.deltas.data(),static_cast<uint32_t>(k.deltas.size())});
    return {asset.part,static_cast<uint32_t>(asset.basis.size()),asset.model.c_str(),asset.basis.data(),keys.data(),static_cast<uint32_t>(keys.size()),static_cast<uint32_t>(asset.rotations.size()),asset.rotations.data()};
}
uint64_t Attach(const mhm::Asset& asset) {
    std::vector<MhmHostKey> keys;const auto data=Descriptor(asset,keys);uint64_t target=0;
    Check(MhwMorphHostAttach(&data,&target)==MHM_OK,"attach actual asset");return target;
}
MhmApi api{};uint64_t removeDuringRead=0;
MhmResult MHM_CALL RacingRead(uint64_t target,MhmWeightState* values,uint32_t capacity,uint32_t* count,uint64_t* requested,uint64_t* applied) {
    const auto status=api.read_weights(target,values,capacity,count,requested,applied);
    if(removeDuringRead){const auto old=removeDuringRead;removeDuringRead=0;MhwMorphHostDetach(old);}return status;
}
MhmTargetInfo Info(uint64_t handle) {
    for(uint32_t i=0;i<5;i++){MhmTargetInfo info{};if(api.target_at(i,&info)!=MHM_OK)break;if(info.handle==handle)return info;}
    std::exit(2);
}
void Baseline(const mhm::Asset& body,const mhm::Asset& arm,const std::filesystem::path& oldDll) {
    const auto module=LoadLibraryW(oldDll.c_str());Check(module!=nullptr,"load frozen comparison core");
    using AttachFn=decltype(&MhwMorphHostAttach);using CaptureFn=decltype(&MhwMorphHostCaptureInterpolatedGroup);
    using ComposeFn=decltype(&MhwMorphHostCompose);using GetFn=decltype(&MhwMorphGetApi);
    const auto attach=reinterpret_cast<AttachFn>(GetProcAddress(module,"MhwMorphHostAttach"));
    const auto capture=reinterpret_cast<CaptureFn>(GetProcAddress(module,"MhwMorphHostCaptureInterpolatedGroup"));
    const auto compose=reinterpret_cast<ComposeFn>(GetProcAddress(module,"MhwMorphHostCompose"));
    const auto get=reinterpret_cast<GetFn>(GetProcAddress(module,"MhwMorphGetApi"));
    Check(attach&&capture&&compose&&get,"frozen host exports");MhmApi old{};
    Check(get(MHM_API_VERSION,sizeof(old),&old)==MHM_OK,"frozen API ABI compatible");
    std::vector<MhmHostKey> bodyKeys,armKeys;const auto b=Descriptor(body,bodyKeys),a=Descriptor(arm,armKeys);uint64_t bt=0,at=0,source=0,revision=0,frame=0;
    Check(attach(&b,&bt)==MHM_OK&&attach(&a,&at)==MHM_OK,"frozen actual asset attach");
    Check(old.open_source("comparison",200,2000,&source)==MHM_OK,"frozen comparison source");
    const MhmUpdate weights[]={{bt,0,.65f},{bt,1,.6f},{at,0,.7f}};
    Check(old.submit(source,weights,3,&revision)==MHM_OK&&capture(weights,3,revision,&frame)==MHM_OK,"frozen comparison frame");
    for(const auto& pair:{std::pair{bt,&body},std::pair{at,&arm}}) {
        std::vector<unsigned char> frozen(pair.second->basis.size()),current;
        Check(compose(frame,pair.first,frozen.data(),static_cast<uint32_t>(frozen.size()))==MHM_OK,"frozen payload composed");
        const std::vector<float> values=pair.first==bt?std::vector<float>{.65f,.6f}:std::vector<float>{.7f};
        Check(mhm::Compose(*pair.second,values,current)&&current==frozen,"new loader/composition preserves all accepted payload bytes");
    }
    FreeLibrary(module);
}
}

int Run(int argc,wchar_t** argv) {
    if(argc!=6)return 2;
    const auto body=mhm::pack::Load(argv[1],Ascii(argv[2])),arm=mhm::pack::Load(argv[3],Ascii(argv[4]));
    const auto catalog=mhm::pack::LoadCatalog(std::filesystem::path(argv[1]).parent_path()/"catalog.mhmcatalog");
    Check(catalog.size()==2&&catalog[0].asset.basis==body.basis&&catalog[1].asset.basis==arm.basis
            &&catalog[0].asset.keys.size()==2&&catalog[1].asset.keys.size()==1,"native catalog loads every configured key without hardcoded IDs");
    Check(body.part==MHM_BODY&&body.keys.size()==2&&arm.part==MHM_ARMS&&arm.keys.size()==1,"manifest-selected actual assets");
    bool rejected=false;try{mhm::pack::Load(argv[1],std::string(64,'0'));}catch(const std::exception&){rejected=true;}Check(rejected,"digest mismatch fails before loading asset");
    {std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
        raw.push_back(0);rejected=false;try{mhm::pack::Decode(raw);}catch(const std::exception&){rejected=true;}Check(rejected,"trailing pack data rejected");
        raw.resize(40);rejected=false;try{mhm::pack::Decode(raw);}catch(const std::exception&){rejected=true;}Check(rejected,"truncated pack rejected");}
    Baseline(body,arm,argv[5]);
    Check(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK&&(api.capabilities&MHM_CAP_PART_LIFECYCLE),"part lifecycle capability published");
    auto bt=Attach(body),at=Attach(arm);const auto oldArm=at;
    morphpartclient::Client client;const std::vector<morphpartclient::TargetSpec> specs={{body.model,MHM_BODY,{"Blink","mouth_01"}},{arm.model,MHM_ARMS,{"HairFront_short"}}};
    Check(client.Connect(api,specs)==MHM_OK&&client.Refresh()==MHM_OK,"discover both parts");
    uint64_t revision=0;Check(client.Set({{MHM_BODY,{.65f,.6f}},{MHM_ARMS,{.7f}}},revision)==MHM_OK,"one atomic cross-part request");
    const auto source=client.Source();uint64_t bodyFrame=0,groupFrame=0;float bodyWeights[]={.65f,.6f};
    Check(MhwMorphHostCaptureInterpolated(bt,bodyWeights,2,revision,&bodyFrame)==MHM_OK,"body-only frame while arm remains live");
    const MhmUpdate full[]={{bt,0,.65f},{bt,1,.6f},{at,0,.7f}};
    Check(MhwMorphHostCaptureInterpolatedGroup(full,3,revision,&groupFrame)==MHM_OK,"complete group remains available");
    Check(MhwMorphHostDetach(at)==MHM_OK,"retire only arms");
    std::vector<unsigned char> payload(body.basis.size(),0xCD),expected;
    Check(MhwMorphHostCompose(groupFrame,bt,payload.data(),static_cast<uint32_t>(payload.size()))==MHM_STALE
            &&std::all_of(payload.begin(),payload.end(),[](unsigned char c){return c==0xCD;}),"invalid group yields no partial body output");
    Check(MhwMorphHostAcknowledge(groupFrame)==MHM_STALE,"cannot acknowledge partial retired group");
    Check(MhwMorphHostCompose(bodyFrame,bt,payload.data(),static_cast<uint32_t>(payload.size()))==MHM_OK
            &&mhm::Compose(body,bodyWeights,expected)&&payload==expected,"unrelated arm retirement preserves body snapshot bytes");
    Check(MhwMorphHostAcknowledge(bodyFrame)==MHM_OK,"body frame still acknowledges");
    Check(client.Refresh()==MHM_OK&&client.Target(MHM_BODY)==bt&&client.Target(MHM_ARMS)==0&&client.Source()==source,"partial discovery retains body handle and source");
    std::vector<morphpartclient::PartState> states;uint64_t requested=0;
    Check(client.Read(states,requested)==MHM_OK&&states.size()==1&&states[0].weights[0].requested==.65f
            &&states[0].weights[0].source==source&&states[0].weights[0].applied==.65f,"body control survives arm absence");
    const auto untouched=revision;
    Check(client.Set({{MHM_BODY,{.2f,.3f}},{MHM_ARMS,{.5f}}},revision)==MHM_NOT_READY&&revision==untouched,"missing-arm batch rejects before changing body");
    Check(client.Set({{MHM_BODY,{.25f,.2f}}},revision)==MHM_OK&&client.Source()==source,"body can still receive new instructions");
    Check(MhwMorphHostReleaseFrame(groupFrame)==MHM_OK&&MhwMorphHostReleaseFrame(bodyFrame)==MHM_OK,"old snapshot references released");
    float half[]={.25f,.2f};Check(MhwMorphHostCaptureInterpolated(bt,half,2,revision,&bodyFrame)==MHM_OK,"capture survivor before arm addition");
    at=Attach(arm);Check(at!=oldArm&&client.Refresh()==MHM_OK&&client.Target(MHM_ARMS)==at&&client.Source()==source,"new arm ID does not replace surviving body source");
    Check(MhwMorphHostAcknowledge(bodyFrame)==MHM_OK,"unrelated arm addition does not stale body frame");
    Check(client.Read(states,requested)==MHM_OK&&states.size()==2&&states[0].appliedRevision!=states[1].appliedRevision
            &&states[1].weights[0].requested==0&&states[1].weights[0].source==0,"new arm neutral and different applied revisions readable");
    Check(client.Set({{MHM_BODY,{.4f,.3f}}},revision)==MHM_OK&&client.Read(states,requested)==MHM_OK
            &&states[1].weights[0].requested==0&&states[1].weights[0].source==0,"body edits do not replay cached retired hair weight");
    Check(client.Set({{MHM_ARMS,{.35f}}},revision)==MHM_OK&&client.Read(states,requested)==MHM_OK
            &&states[0].weights[0].requested==.4f&&states[1].weights[0].requested==.35f,"fresh arm command preserves body contribution");
    float armWeights[]={.35f};uint64_t armFrame=0;
    Check(MhwMorphHostCaptureInterpolated(at,armWeights,1,revision,&armFrame)==MHM_OK&&MhwMorphHostAcknowledge(armFrame)==MHM_OK,"arm can independently publish a frame");
    Check(MhwMorphHostReleaseFrame(bodyFrame)==MHM_OK&&MhwMorphHostReleaseFrame(armFrame)==MHM_OK,"member frames released");
    Check(client.Release()==MHM_OK&&client.Read(states,requested)==MHM_OK&&states[0].weights[0].requested==0
            &&states[1].weights[0].requested==0,"release clears both contributions");
    // Keep a stable one-part reference trajectory while membership changes.
    mhm::PartSmoothing bank,reference;const auto bd=Info(bt),ad=Info(at);const MhmTargetInfo descriptors[]={bd,ad};
    Check(bank.Reconcile(descriptors,0)==MHM_OK&&reference.Reconcile({&bd,1},0)==MHM_OK,"independent smoothing setup");
    const MhmUpdate initial[]={{bt,0,1},{bt,1,.6f},{at,0,1}},bodyInitial[]={{bt,0,1},{bt,1,.6f}};
    Check(bank.SetTargets(initial,1,0)==MHM_OK&&reference.SetTargets(bodyInitial,1,0)==MHM_OK,"same starting body response");
    std::vector<MhmUpdate> sampled,control;bank.Sample(200,sampled);reference.Sample(200,control);
    Check(bank.Reconcile({&bd,1},200)==MHM_OK&&bank.Sample(350,sampled)==MHM_OK&&reference.Sample(350,control)==MHM_OK
            &&sampled[0].weight==control[0].weight&&sampled[1].weight==control[1].weight,"arm removal preserves body position and velocity");
    Check(bank.Reconcile(descriptors,350)==MHM_OK&&bank.Sample(450,sampled)==MHM_OK&&reference.Sample(450,control)==MHM_OK
            &&sampled[0].weight==control[0].weight&&sampled[2].weight==0,"new smoothing member neutral without disturbing body");
    auto invalid=bd;invalid.key_count++;
    Check(bank.Reconcile({&invalid,1},450)==MHM_INVALID&&bank.Sample(450,sampled)==MHM_OK&&sampled.size()==3,"descriptor mutation cannot reset surviving response");
    // Detach during a public read: preserve caller outputs AND body contribution.
    Check(client.Set({{MHM_BODY,{.8f,.7f}},{MHM_ARMS,{.6f}}},revision)==MHM_OK,"race fixture weights");
    auto racing=api;racing.read_weights=RacingRead;morphpartclient::Client raceClient;
    Check(raceClient.Connect(racing,specs)==MHM_OK&&raceClient.Refresh()==MHM_OK,"race client connected");
    Check(raceClient.Set({{MHM_BODY,{.9f,.8f}},{MHM_ARMS,{.7f}}},revision)==MHM_OK,"race client owns older-priority tie handled by core");
    const auto raceSource=raceClient.Source();removeDuringRead=at;states={{MHM_BODY,777,999,{}}};requested=999;
    Check(raceClient.Read(states,requested)!=MHM_OK&&states[0].target==777&&requested==999&&raceClient.Source()==raceSource,"mid-read removal never emits partial state or closes surviving source");
    Check(raceClient.Refresh()==MHM_OK&&raceClient.Target(MHM_BODY)==bt&&raceClient.Target(MHM_ARMS)==0,"race refresh converges on survivor");
    Check(client.Release()==MHM_OK&&raceClient.Read(states,requested)==MHM_OK&&states[0].weights[0].requested==.9f,"retiring arm preserves racing source's actual body contribution");
    Check(MhwMorphHostTick(2000)==MHM_OK&&raceClient.Heartbeat()==MHM_STALE&&raceClient.Source()==0,"source lease still clears surviving control");
    Check(raceClient.Set({{MHM_BODY,{.3f,.2f}}},revision)==MHM_OK&&raceClient.Source()!=raceSource
            &&raceClient.Read(states,requested)==MHM_OK&&states[0].weights[0].requested==.3f,"fresh command after lease expiry uses only new contribution");
    Check(raceClient.Release()==MHM_OK,"release reacquired source");
    Check(MhwMorphHostDetach(bt)==MHM_OK,"cleanup survivor");
    std::printf("PASS: %u independent-part checks; actual DLL and frozen payload comparison; no game access\n",checks);
    return 0;
}

int wmain(int argc,wchar_t** argv) {
    try{return Run(argc,argv);}
    catch(const std::exception& error){std::printf("FAIL: %s\n",error.what());return 1;}
}
