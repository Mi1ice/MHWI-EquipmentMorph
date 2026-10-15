#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
namespace inputfasttest {inline thread_local std::uint64_t reads=0;inline void(*afterRead)(LPCVOID,SIZE_T)=nullptr;}
BOOL WINAPI CountInputReads(HANDLE process,LPCVOID address,LPVOID output,SIZE_T bytes,SIZE_T* copied){
    ++inputfasttest::reads;const auto result=::ReadProcessMemory(process,address,output,bytes,copied);
    if(result&&inputfasttest::afterRead)inputfasttest::afterRead(address,bytes);return result;
}
#define ReadProcessMemory CountInputReads
#include "prototype.cpp"
#undef ReadProcessMemory
#include <cstdio>
#include <thread>
#include "RetirementTests.hpp"
#include "InputFastPathTests.hpp"
#include "LaunchValidationTests.hpp"
#include "IdleSamplingTests.hpp"
#include "BatchedReadTests.hpp"
int wmain(int argc,wchar_t** argv){try{
    unsigned checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);checks++;};
    checks+=retirementtest::Run(std::filesystem::absolute(argv[0]).parent_path()/"retirement-regression.ndjson");
    checks+=inputfasttest::Run();
    checks+=launchvalidationtest::Run();
    checks+=idlesamplingtest::Run();
    checks+=batchedreadtest::Run();
    auto put=[](std::vector<unsigned char>& raw,size_t at,uint64_t value,size_t bytes){for(size_t i=0;i<bytes;i++)raw[at+i]=static_cast<unsigned char>(value>>(i*8));};
    constexpr size_t meshStart=128,vertexStart=meshStart+3*80;
    std::vector<unsigned char> model(vertexStart+112);std::memcpy(model.data(),"MOD\0",4);put(model,4,237,2);put(model,8,3,2);put(model,24,112,4);put(model,72,meshStart,8);put(model,80,vertexStart,8);
    uint32_t offset=0;std::vector<uint32_t> diskFormats;
    for(size_t i=0;i<3;i++){const auto& f=mhm::meshcontract::formats[i];const auto row=meshStart+i*80;put(model,row+2,1,2);put(model,row+14,f.stride,1);put(model,row+20,offset,4);put(model,row+24,f.disk,4);offset+=f.stride;diskFormats.push_back(f.disk);}
    std::vector<unsigned char> disk(model.begin()+meshStart,model.begin()+vertexStart),runtime=disk;
    for(size_t i=0;i<3;i++){put(runtime,i*80+24,mhm::meshcontract::formats[i].native,4);runtime[i*80+42]=static_cast<unsigned char>(i+1);put(runtime,i*80+72,0x12340000+i*4096,8);}
    check(mhm::meshcontract::FormatsMatch(runtime,diskFormats),"all proven disk/native format pairs and strides");
    auto changed=runtime;put(changed,24,6,4);check(!mhm::meshcontract::FormatsMatch(changed,diskFormats),"wrong initialized format rejected even though digest would ignore field");
    changed=runtime;changed[14]++;check(!mhm::meshcontract::FormatsMatch(changed,diskFormats),"wrong stride rejected");
    auto unknown=diskFormats;unknown[0]=0xa756f2f9;check(!mhm::meshcontract::FormatsMatch(runtime,unknown),"unproven mapping refused");
    check(!mhm::meshcontract::FormatsMatch(std::span(runtime).first(runtime.size()-1),diskFormats),"partial table refused");
    auto normalized=disk;auto loaded=runtime;check(mhm::meshcontract::Normalize(normalized,true)&&mhm::meshcontract::Normalize(loaded,true)&&normalized==loaded,"uniform disk/runtime table contract");
    changed=loaded;changed[20]^=1;check(mhm::pack::Digest(changed)!=mhm::pack::Digest(normalized),"vertex start remains part of exact contract");
    changed=runtime;auto legacy=disk;mhm::meshcontract::Normalize(changed,false);mhm::meshcontract::Normalize(legacy,false);check(changed!=legacy,"legacy body still retains initialized fields");
    mhm::configuration::Profile p;p.diskContract=true;p.part=MHM_BODY;p.model="pl/example/body";p.vertexBytes=112;p.meshCount=3;p.modelBytes=static_cast<uint32_t>(model.size());p.modelSha=mhm::pack::Digest(model);p.meshSha=mhm::pack::Digest(normalized);
    mhm::configuration::SourceObject object;object.name="Example";object.values.push_back({"Example_A",0,2});p.objects.push_back(object);
    mhm::Asset asset;asset.part=MHM_BODY;asset.model=p.model;asset.basis.resize(112);mhm::Key key;strcpy_s(key.info.id,"Example_A");key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);
    mhm::configuration::ValidateModel(p,asset,model);check(p.diskFormats==diskFormats,"new body profile derives format proof from hash-checked disk model");
    auto reject=[&](auto profile,auto data,const char* name){bool rejected=false;try{mhm::configuration::ValidateModel(profile,asset,data);}catch(const std::exception&){rejected=true;}check(rejected,name);};
    auto bad=p;bad.meshSha=std::string(64,'0');reject(bad,model,"caller cannot supply arbitrary normalized identity");
    bad=p;bad.modelSha=std::string(64,'0');reject(bad,model,"model file identity still enforced");
    auto data=model;put(data,meshStart+24,0xa756f2f9,4);put(data,meshStart+14,24,1);bad=p;bad.modelSha=mhm::pack::Digest(data);reject(bad,data,"known disk format with unproven runtime mapping refused");
    data=model;put(data,meshStart+20,113,4);bad=p;bad.modelSha=mhm::pack::Digest(data);reject(bad,data,"vertex range still enforced");
    auto different=asset;different.basis[0]=1;bool rejected=false;try{mhm::configuration::ValidateModel(p,different,model);}catch(const std::exception&){rejected=true;}check(rejected,"pack baseline still byte-exact");
    check(mhm::configuration::Fold("PL\\Example\\Body")=="pl/example/body","model catalogue identity normalizes path separators and case");
    auto& native=equipmentnative::profiles[MHM_BODY];native.configured=true;native.diskContract=true;native.diskFormats=diskFormats;check(native.ownerField==0,"body owner route remains unchanged");
    if(argc==2){
        auto raw=mhm::configuration::ReadFile(argv[1],512*1024);auto decoded=mhm::configuration::DecodeProfile(raw);
        check(decoded.diskContract&&decoded.part==MHM_BODY&&decoded.objects[0].values[1].weight==.6f,"Python v2 encoder parsed natively with nonzero fixed baseline");
        mhm::configuration::ValidateModel(decoded,asset,model);check(decoded.diskFormats==diskFormats,"cross-language body profile validates actual synthetic file");
        const std::string marker="mhw-c2ebbbd2-equipment-v2";const auto found=std::search(raw.begin(),raw.end(),marker.begin(),marker.end());
        check(found!=raw.end(),"new adapter marker present");*(found+static_cast<std::ptrdiff_t>(marker.size()-1))='1';
        bool oldRejected=false;try{mhm::configuration::DecodeProfile(raw);}catch(const std::exception&){oldRejected=true;}
        check(oldRejected,"obsolete profile adapter rejected");
    }
    // Individually valid endpoints can cancel or point opposite the baseline
    // when added. Keep only that field's baseline shading and still compose xyz.
    mhm::Asset normals;normals.part=MHM_BODY;normals.model="pl/example/normals";normals.perKeyNormals=true;normals.basis.resize(64);
    for(unsigned at:{12u,44u}){normals.basis[at]=127;normals.basis[at+3]=7;normals.basis[at+5]=127;normals.basis[at+7]=255;}
    for(int sign:{1,-1}){
        mhm::Key k;strcpy_s(k.info.id,sign==1?"A":"B");k.info.maximum=1;k.info.flags=MHM_PER_KEY_SHADING;
        k.deltas.push_back({sign==1?0u:32u,sign==1?1.f:2.f,0,0});k.normals.push_back({12,-1,float(sign),0});
        if(sign==-1)k.normals.push_back({44,-1,1,0});normals.keys.push_back(k);
    }
    check(mhm::Validate(normals),"opposed endpoint pair individually valid");
    auto fieldEquals=[&](const auto& output,unsigned at){return std::equal(normals.basis.begin()+at,normals.basis.begin()+at+8,output.begin()+at);};
    auto xyz=[](const auto& output,unsigned at){float value=0;std::memcpy(&value,output.data()+at,4);return value;};
    std::vector<unsigned char> output;
    const float firstOnly[]={1,0},secondOnly[]={0,1},zero[]={0,0};
    check(mhm::Compose(normals,zero,output)&&output==normals.basis,"zero weights restore every baseline byte");
    check(mhm::Compose(normals,firstOnly,output)&&output[12]==0&&output[13]==127&&static_cast<int8_t>(output[16])==-127
        &&output[17]==0&&output[15]==7&&output[19]==255&&xyz(output,0)==1&&fieldEquals(output,44),"single endpoint and tangent transport preserved");
    check(mhm::Compose(normals,secondOnly,output)&&output[12]==0&&static_cast<int8_t>(output[13])==-127
        &&output[44]==0&&output[45]==127&&xyz(output,32)==2,"other single endpoint preserved");
    for(unsigned step=0;step<=128;step++){
        const float weight=static_cast<float>(step)/128;const float pair[]={weight,weight};
        check(mhm::Compose(normals,pair,output)&&fieldEquals(output,12)&&xyz(output,0)==weight&&xyz(output,32)==2*weight,
            "continuous opposed pair composes positions through cancellation and reversal");
    }
    const float asymmetric[]={.75f,.750001f};
    check(mhm::Compose(normals,asymmetric,output)&&fieldEquals(output,12),"near antipodal pair retains baseline shading");
    const float regular[]={1,.25f};
    check(mhm::Compose(normals,regular,output)&&static_cast<int8_t>(output[12])==-40&&output[13]==120
        &&static_cast<int8_t>(output[16])==-120&&static_cast<int8_t>(output[17])==-40,"regular obtuse blend and tangent transport preserved");
    const float endpoints[]={1,1};
    check(mhm::Compose(normals,endpoints,output)&&fieldEquals(output,12)&&output[44]==0&&output[45]==127
        &&static_cast<int8_t>(output[48])==-127&&output[47]==7&&output[51]==255,"fallback is local; healthy normal field still transported");
    auto invalid=normals;invalid.keys[0].normals[0].y=std::numeric_limits<float>::quiet_NaN();
    check(!mhm::Validate(invalid),"nonfinite endpoint still rejected at asset validation");
    output.assign(64,0xCD);const auto untouched=output;const float invalidWeights[]={1,std::numeric_limits<float>::quiet_NaN()};
    check(!mhm::Compose(normals,invalidWeights,output)&&output==untouched,"invalid weight leaves caller output unchanged");
    uint64_t body=0,arms=0,source=0,revision=0,frame=0;MhmApi api{};
    check(Core().Attach(normals,body)==MHM_OK&&MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK,"normal regression public host attached");
    auto armAsset=normals;armAsset.part=MHM_ARMS;armAsset.model="pl/example/arms";armAsset.perKeyNormals=false;
    for(auto& k:armAsset.keys){k.normals.clear();k.info.flags=0;}
    check(Core().Attach(armAsset,arms)==MHM_OK&&api.open_source("normal-regression",200,2000,&source)==MHM_OK,"healthy other part attached");
    const MhmUpdate smoothRequest[]={{body,0,1},{body,1,1}};
    MhmTargetInfo descriptor{};descriptor.handle=body;descriptor.part=MHM_BODY;descriptor.key_count=2;
    mhm::GroupSmoothing smoothing;
    check(api.submit(source,smoothRequest,2,&revision)==MHM_OK&&smoothing.Reset({&descriptor,1},0)==MHM_OK
        &&smoothing.SetTargets(smoothRequest,revision,0)==MHM_OK,"opposed endpoints enter host smoother");
    for(uint64_t now:{0ull,200ull,210ull,220ull,300ull,1000ull}){
        std::vector<MhmUpdate> sampled;uint64_t sampledRevision=0;
        check(smoothing.Sample(now,sampled,sampledRevision)==MHM_OK&&sampled.size()==2,"opposed weights sampled across midpoint");
        const float pair[]={sampled[0].weight,sampled[1].weight};output.resize(64);
        check(MhwMorphHostCaptureInterpolated(body,pair,2,sampledRevision,&frame)==MHM_OK
            &&MhwMorphHostCompose(frame,body,output.data(),64)==MHM_OK&&fieldEquals(output,12)&&xyz(output,0)==pair[0]
            &&MhwMorphHostAcknowledge(frame)==MHM_OK&&MhwMorphHostReleaseFrame(frame)==MHM_OK,"smoothed opposed pair composes and acknowledges without stopping");
    }
    for(float weight:{.5f,1.f}){
        const MhmUpdate updates[]={{body,0,weight},{body,1,weight},{arms,0,.75f}};const float pair[]={weight,weight};
        check(api.submit(source,updates,3,&revision)==MHM_OK
            &&MhwMorphHostCaptureInterpolated(body,pair,2,revision,&frame)==MHM_OK,"opposed request accepted and captured");
        output.resize(64);
        check(MhwMorphHostCompose(frame,body,output.data(),64)==MHM_OK&&fieldEquals(output,12)&&xyz(output,0)==weight
            &&MhwMorphHostAcknowledge(frame)==MHM_OK&&MhwMorphHostReleaseFrame(frame)==MHM_OK,"host composition succeeds without worker failure path");
        MhmWeightState states[2]{};uint32_t count=0;uint64_t requested=0,applied=0;
        check(api.read_weights(body,states,2,&count,&requested,&applied)==MHM_OK&&count==2&&requested==revision&&applied==revision
            &&states[0].applied==weight&&states[1].applied==weight,"opposed weights acknowledged accurately");
        const float armWeights[]={.75f,0};
        check(MhwMorphHostCaptureInterpolated(arms,armWeights,2,revision,&frame)==MHM_OK
            &&MhwMorphHostCompose(frame,arms,output.data(),64)==MHM_OK&&xyz(output,0)==.75f
            &&MhwMorphHostAcknowledge(frame)==MHM_OK&&MhwMorphHostReleaseFrame(frame)==MHM_OK,"healthy part remains composable and acknowledges");
    }
    check(api.close_source(source)==MHM_OK&&Core().Detach(body)==MHM_OK&&Core().Detach(arms)==MHM_OK,"normal regression resources released");
    // Exercise the real worker retirement with owned synthetic wrapper memory.
    // Unlaunched buffers need no renderer/fence interface or draw-cycle record.
    privateprobe::Log rebindLog;
    const auto logPath=std::filesystem::temp_directory_path()/("mhwi-rebind-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".ndjson");
    rebindLog.file=CreateFileW(logPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    check(rebindLog.file!=INVALID_HANDLE_VALUE,"temporary rebind regression log opened");
    facecontinuous::armed=true;
    uint64_t healthy=0,healthySource=0;
    check(Core().Attach(armAsset,healthy)==MHM_OK&&api.open_source("rebind-survivor",200,2000,&healthySource)==MHM_OK,"healthy arm attached during body rebind");
    const MhmUpdate healthyWeight{healthy,0,.25f};
    check(api.submit(healthySource,&healthyWeight,1,&revision)==MHM_OK,"healthy arm has active contribution");
    auto setMemory=[](auto& raw,size_t at,uint64_t value,size_t bytes){for(size_t i=0;i<bytes;i++)raw[at+i]=static_cast<unsigned char>(value>>(i*8));};
    for(unsigned allocatedCount=0;allocatedCount<=3;allocatedCount++){
        facecontinuous::Part retired;retired.asset=std::make_shared<const mhm::Asset>(normals);
        std::array<std::array<unsigned char,128>,3> wrappers{};
        std::array<std::array<unsigned char,144>,3> backings{};
        std::array<std::uintptr_t,3> handles{};
        for(unsigned i=0;i<allocatedCount;i++){
            auto& buffer=retired.buffers[i];auto& wrapper=wrappers[i];auto& backing=backings[i];
            buffer.bytes=64;buffer.wrapper=reinterpret_cast<std::uintptr_t>(wrapper.data());buffer.backing=reinterpret_cast<std::uintptr_t>(backing.data());
            buffer.resource=0x80000+i*0x1000;buffer.view=0x90000+i*0x1000;buffer.initialization=10+i;
            handles[i]=buffer.wrapper;retired.stamps[i]=buffer.initialization;
            setMemory(wrapper,0,observer::imageBase+observed::vertexVtableRva,8);setMemory(wrapper,0x48,1,4);setMemory(wrapper,0x5C,64,4);
            setMemory(wrapper,0x60,buffer.backing,8);setMemory(wrapper,0x78,buffer.view,8);
            setMemory(backing,0,observer::imageBase+0x3539948,8);setMemory(backing,0x10,64,4);setMemory(backing,0x20,buffer.resource,8);
            setMemory(backing,0x78,buffer.initialization,8);setMemory(backing,0x80,UINT64_MAX,8);
            check(privateprobe::Contract(buffer),"synthetic initialization buffer contract valid");
        }
        retired.allocated=allocatedCount==3;
        check(retired.adapter->Arm()&&retired.adapter->Capture({0x10000,0x20000,0x30000,0x40000},0x30000),"initial body identity captured");
        uint64_t oldHandle=0;
        if(retired.allocated){
            retired.binding=std::make_unique<equipmentmorph::PartBinding>();retired.binding->Attach(normals);oldHandle=retired.target=retired.binding->target;
            check(retired.adapter->PublishResources(handles)&&!retired.adapter->Fixed().Stable(),"buffers ready before first native launch");
        }
        retired.adapter->Invalidate(0,"equipment_part_model_replaced");
        privateprobe::UploadFence upload;facecontinuous::DrawingFence drawing;upload.completed=12;
        bool restored=true;try{facecontinuous::EndPart(rebindLog,retired,upload,drawing);}catch(const std::exception&){restored=false;}
        check(restored&&retired.epoch==2&&retired.adapter->Live()&&!retired.adapter->Ready()&&!retired.target&&!retired.binding,
            "unlaunched resources retire without native fence calls");
        for(unsigned i=0;i<3;i++)check(retired.buffers[i].wrapper==handles[i],"partial or full allocation preserved without additional buffers");
        if(oldHandle){
            MhmKeyInfo info{};check(api.key_at(oldHandle,0,&info)==MHM_STALE,"unlaunched public target retired");
            retired.binding=std::make_unique<equipmentmorph::PartBinding>();retired.binding->Attach(normals);
            MhmWeightState states[2]{};uint32_t count=0;uint64_t requested=0,applied=0;
            check(retired.binding->target!=oldHandle&&api.read_weights(retired.binding->target,states,2,&count,&requested,&applied)==MHM_OK
                &&states[0].requested==0&&states[1].requested==0,"replacement public target starts without old weights");
            check(retired.reusable&&retired.reuseFence==0,"never launched buffer set can reuse without a draw fence");
            auto& buffer=retired.buffers[2];
            check(facecontinuous::BufferUploadComplete(buffer,12,12)&&!facecontinuous::BufferUploadComplete(buffer,12,11)
                &&!facecontinuous::BufferUploadComplete(buffer,12,UINT64_MAX),"reuse requires finite completed upload stamp");
            setMemory(backings[2],0x80,13,8);
            check(!facecontinuous::BufferUploadComplete(buffer,12,13),"reuse rejects changed upload stamp");
            setMemory(backings[2],0x80,UINT64_MAX,8);setMemory(wrappers[2],0x48,0,4);
            check(!facecontinuous::BufferUploadComplete(buffer,12,12),"reuse rejects broken buffer contract");
            setMemory(wrappers[2],0x48,1,4);
            // A set reused after an earlier draw drain must retain that proof,
            // even when the next binding is cancelled before its first launch.
            retired.reuseFence=55;retired.reusable=false;
            check(retired.adapter->Capture({0x10000,0x21000,0x31000,0x40000},0x31000)&&retired.adapter->PublishResources(handles),"replacement resources published before launch");
            retired.adapter->Invalidate(0,"equipment_part_model_replaced");drawing.completed=54;
            bool rejectedReuse=false;try{facecontinuous::EndPart(rebindLog,retired,upload,drawing);}catch(const std::exception&){rejectedReuse=true;}
            check(rejectedReuse&&retired.epoch==2&&!retired.reusable&&!retired.adapter->Live(),"incomplete earlier draw proof cannot rearm buffers");
            drawing.completed=55;
            facecontinuous::EndPart(rebindLog,retired,upload,drawing);
            check(retired.epoch==3&&retired.reusable&&retired.reuseFence==55&&retired.adapter->Live(),"completed earlier proof permits repeated prelaunch rebind");
        }
        MhmWeightState states[2]{};uint32_t count=0;uint64_t requested=0,applied=0;
        check(api.read_weights(healthy,states,2,&count,&requested,&applied)==MHM_OK&&states[0].requested==.25f&&states[0].source==healthySource
            &&facecontinuous::armed,"healthy arm source and global runtime remain active");
    }
    check(api.close_source(healthySource)==MHM_OK&&Core().Detach(healthy)==MHM_OK,"rebind regression targets released");
    facecontinuous::LaunchAdapter launched;
    const facecontinuous::Frame cycle{0x10000,0x20000,0x30000,100,100,1,0,1};
    check(launched.Arm()&&launched.Capture({0x40000,0x50000,0x60000,0x70000},0x60000)
        &&launched.PublishResources({0x80000,0x90000,0xA0000})&&!launched.HasLaunched(),"resources published does not establish a launch");
    check(launched.Launch(cycle,0,0xB0000)&&launched.HasLaunched(),"first native launch recorded");
    launched.Invalidate(0,"equipment_part_model_replaced");
    check(!launched.Started()&&launched.HasLaunched()&&launched.Fixed().Stable(),"invalidation preserves prior launch evidence for draining");
    facecontinuous::DrainBoundary drain;
    check(drain.Start(launched.Fixed(),102,12)&&drain.Completed(12,103)&&!drain.Ready(),"drawn buffers require CPU retirement proof despite GPU completion");
    const facecontinuous::Frame retirementCycle{0x10000,0x20000,0x30000,102,102,1,0,1};
    check(drain.Launch(retirementCycle,0,0xB0000)&&drain.Wait(retirementCycle,0,0xC0000,0)&&drain.Join(retirementCycle)
        &&drain.Signal(0x20000,0x30000,103,103,103,true)&&!drain.Ready(),"first retirement signal alone cannot permit reuse");
    const facecontinuous::Frame successor{0x10000,0x20000,0x30000,103,103,1,0,1};
    check(drain.Launch(successor,0,0xB0000)&&drain.Wait(successor,0,0xC0000,0)&&drain.Join(successor)
        &&drain.Ready()&&drain.CoveredSignal()==103,"drawn buffers require successor join and both completions");
    facecontinuous::armed=false;
    privateprobe::Log weightLog;
    const auto weightLogPath=std::filesystem::absolute(argv[0]).parent_path()/"weight-log-regression.ndjson";
    weightLog.file=CreateFileW(weightLogPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(weightLog.file!=INVALID_HANDLE_VALUE,"weight log regression output opened");
    auto readLog=[](HANDLE file){
        LARGE_INTEGER size{},zero{};DWORD count=0;
        if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>1024*1024||!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Log read setup");
        std::string text(static_cast<size_t>(size.QuadPart),'\0');
        if(!ReadFile(file,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)||count!=text.size()
            ||!SetFilePointerEx(file,zero,nullptr,FILE_END))throw std::runtime_error("Log read");return text;
    };
    auto manyKeys=normals;manyKeys.keys.resize(64);
    for(unsigned i=0;i<64;i++){auto& k=manyKeys.keys[i];sprintf_s(k.info.id,"Key_%u",i);k.info.maximum=1;}
    check(mhm::Validate(manyKeys),"64-key logging asset is valid");
    facecontinuous::Part logged;logged.asset=std::make_shared<const mhm::Asset>(manyKeys);
    logged.epoch=logged.target=logged.updates=logged.replacements=UINT64_MAX-1;
    check(logged.adapter->Arm()&&logged.adapter->Capture({0x10000,0x20000,0x30000,0x40000},0x30000)
        &&logged.adapter->PublishResources({0x50000,0x60000,0x70000}),"64-key logging adapter ready");
    launchpool::MorphVersion loggedVersion;loggedVersion.weights.fill(.13724681f);loggedVersion.frame=UINT64_MAX-1;loggedVersion.revision=UINT64_MAX-2;
    check(logged.adapter->Request(loggedVersion)&&logged.adapter->Launch({0x10000,0x20000,0x30000,10,10,1,0,1},0,0x40000)
        &&logged.adapter->Wait({0x10000,0x20000,0x30000,10,10,1,0,1},0,0x41000,0)
        &&logged.adapter->Boundary({0x10000,0x20000,0x30000,10,10,1,0,1})
        &&logged.adapter->Signal(0x20000,0x30000,11,11,11,true),"logging adapter completed first cycle");
    const auto logToken=logged.adapter->Prepare();
    check(logToken&&logged.adapter->BeginUpload(*logToken)&&logged.adapter->QueueUpload(*logToken,20)&&logged.adapter->Completed(20,11)
        &&logged.adapter->Launch({0x10000,0x20000,0x30000,11,11,1,0,1},0,0x40000),"64-key weights published for state logging");
    facecontinuous::WriteState(weightLog,logged,"part_state");
    const auto loggedState=readLog(weightLog.file);
    check(loggedState.ends_with("}\n")&&loggedState.find("\"replacements\":18446744073709551614")!=std::string::npos,
        "64-key state log retains closing fields and newline");
    const auto weightText=facecontinuous::Weights(loggedVersion.weights,64);
    check(loggedState.find("\"requested\":"+weightText)!=std::string::npos&&loggedState.find("\"applied\":"+weightText)!=std::string::npos,
        "state log preserves both complete 64-key arrays");
    check(logged.adapter->Wait({0x10000,0x20000,0x30000,11,11,1,0,1},0,0x41000,0)
        &&logged.adapter->Boundary({0x10000,0x20000,0x30000,11,11,1,0,1})&&logged.adapter->Signal(0x20000,0x30000,12,12,12,true),"logged decimal frame completed");
    loggedVersion.weights.fill(std::numeric_limits<float>::denorm_min());loggedVersion.frame=UINT64_MAX;loggedVersion.revision=UINT64_MAX-1;
    check(logged.adapter->Request(loggedVersion),"scientific notation weights requested");
    const auto scientificToken=logged.adapter->Prepare();
    check(scientificToken&&logged.adapter->BeginUpload(*scientificToken)&&logged.adapter->QueueUpload(*scientificToken,21)
        &&logged.adapter->Completed(21,12)&&logged.adapter->Launch({0x10000,0x20000,0x30000,12,12,1,0,1},0,0x40000),"scientific notation weights published");
    facecontinuous::WriteState(weightLog,logged,"part_state");
    launchpool::Slot logPrevious;logPrevious.retirementFence=logPrevious.coverageFence=UINT64_MAX-1;logPrevious.joinedAfterUse=true;
    facecontinuous::WriteUpload(weightLog,logged,2,loggedVersion,UINT64_MAX-1,std::string(64,'f'),logPrevious);
    auto records=readLog(weightLog.file);const auto scientificText=facecontinuous::Weights(loggedVersion.weights,64);
    check(records.find("\"requested\":"+scientificText)!=std::string::npos&&records.find("\"applied\":"+scientificText)!=std::string::npos
        &&records.find("\"weights\":"+scientificText)!=std::string::npos&&records.ends_with("\"previous_joined\":true}\n"),"state and upload logs retain full scientific notation arrays");
    facecontinuous::Part oneKey;auto oneAsset=normals;oneAsset.keys.resize(1);oneKey.asset=std::make_shared<const mhm::Asset>(oneAsset);
    facecontinuous::WriteState(weightLog,oneKey,"part_state");
    launchpool::MorphVersion zeroVersion;
    facecontinuous::WriteUpload(weightLog,logged,0,zeroVersion,1,std::string(64,'0'),{});
    facecontinuous::WriteUpload(weightLog,oneKey,0,zeroVersion,1,std::string(64,'0'),{});
    records=readLog(weightLog.file);
    check(records.find("\"requested\":[0],\"applied\":[0]")!=std::string::npos
        &&records.find("\"weights\":"+facecontinuous::Weights(zeroVersion.weights,64))!=std::string::npos&&records.find("\"weights\":[0]")!=std::string::npos,
        "zero and one-key records remain complete");
    const auto completeBytes=weightLog.bytes;
    check(!weightLog.Write("")&&!weightLog.Write("{\"event\":\"unfinished\"}")&&weightLog.bytes==completeBytes&&readLog(weightLog.file)==records,
        "incomplete records are rejected without changing file or counters");
    bool truncatedRejected=false;const std::string longEvent(5000,'a');
    try{facecontinuous::WriteState(weightLog,logged,longEvent.c_str());}catch(const std::exception&){truncatedRejected=true;}
    check(truncatedRejected&&weightLog.bytes==completeBytes&&readLog(weightLog.file)==records,"format overflow never writes a partial state record");
    const std::string boundaryPrefix="{\"event\":\"record_boundary\",\"padding\":\"";
    const auto boundaryLine=boundaryPrefix+std::string(65536-boundaryPrefix.size()-3,'x')+"\"}\n";
    check(boundaryLine.size()==65536&&weightLog.Write(boundaryLine.c_str()),"bounded writer accepts a complete 65536-byte record");
    const auto boundaryBytes=weightLog.bytes;const auto oversizedLine=boundaryPrefix+std::string(65537-boundaryPrefix.size()-3,'x')+"\"}\n";
    check(!weightLog.Write(oversizedLine.c_str())&&weightLog.bytes==boundaryBytes,"bounded writer rejects oversized complete records");
    check(weightLog.Write("{\"event\":\"after_rejected_record\"}\n")&&weightLog.Flush(),"subsequent record writes normally after rejected records");
    records=readLog(weightLog.file);
    check(std::count(records.begin(),records.end(),'\n')==8&&records.ends_with("{\"event\":\"after_rejected_record\"}\n"),"eight independent complete records retained");
    {
    mhm::configuration::ParseIni("[runtime]\nformat=2\n");check(true,"automatic sidecar configuration accepted");
    bool oldIniRejected=false;try{mhm::configuration::ParseIni("[runtime]\nformat=1\n[packages]\nbody=old\n");}catch(const std::exception&){oldIniRejected=true;}
    check(oldIniRejected,"single-package configuration rejected without compatibility branch");
    mhm::configuration::Collection catalogue;
    auto secondAsset=asset;secondAsset.model="pl/example/second";secondAsset.basis.resize(144);strcpy_s(secondAsset.keys[0].info.id,"Example_B");
    auto secondProfile=p;secondProfile.model=secondAsset.model;secondProfile.vertexBytes=144;
    catalogue.parts[MHM_BODY].push_back(std::make_shared<const mhm::configuration::Loaded>(mhm::configuration::Loaded{p,asset}));
    catalogue.parts[MHM_BODY].push_back(std::make_shared<const mhm::configuration::Loaded>(mhm::configuration::Loaded{secondProfile,secondAsset}));
    check(catalogue.Find(MHM_BODY,"PL\\EXAMPLE\\BODY")&&catalogue.Find(MHM_BODY,"pl/example/second"),"two models in one part resolve independently");
    check(!catalogue.Find(MHM_ARMS,p.model)&&!catalogue.Find(MHM_BODY,"pl/example/missing"),"wrong part and unregistered models have no package");
    equipmentnative::Initialize(catalogue);facecontinuous::Part cycling;cycling.index=MHM_BODY;cycling.cached.resize(2);cycling.wanted=0;facecontinuous::armed=true;
    facecontinuous::ActivateWanted(cycling);check(cycling.asset->model==p.model&&cycling.adapter->Live(),"first model activates from catalogue");
    cycling.buffers[0].wrapper=0xABC;cycling.allocated=true;cycling.reusable=false;cycling.reuseFence=77;cycling.wanted=1;
    bool unsafeRejected=false;try{facecontinuous::ActivateWanted(cycling);}catch(const std::exception&){unsafeRejected=true;}
    check(unsafeRejected&&cycling.asset->model==p.model,"incomplete GPU retirement blocks model switch");
    cycling.reusable=true;cycling.lastSample={.8f};cycling.lastRevision=42;
    facecontinuous::ActivateWanted(cycling);
    check(cycling.asset->model==secondAsset.model&&cycling.buffers[0].wrapper==0&&cycling.payload.empty()
        &&cycling.lastSample.empty()&&cycling.lastRevision==0,"different-sized model activates with fresh buffers and no old weights");
    check(cycling.cached[0].buffers[0].wrapper==0xABC&&cycling.cached[0].reuseFence==77,"retired model retains its own buffer and fence proof");
    cycling.binding=std::make_unique<equipmentmorph::PartBinding>();cycling.binding->Attach(*cycling.asset);const auto oldHandle=cycling.binding->target;
    MhmApi switchApi{};MhwMorphGetApi(MHM_API_VERSION,sizeof(switchApi),&switchApi);uint64_t sourceId=0,switchRevision=0;
    check(switchApi.open_source("catalogue-test",200,2000,&sourceId)==MHM_OK,"switch test control source");
    MhmUpdate requested{oldHandle,0,.7f};check(switchApi.submit(sourceId,&requested,1,&switchRevision)==MHM_OK,"old model accepts its own weight");
    cycling.wanted.reset();unsafeRejected=false;try{facecontinuous::ActivateWanted(cycling);}catch(const std::exception&){unsafeRejected=true;}
    check(unsafeRejected,"live public binding blocks model replacement");
    cycling.binding.reset();cycling.target=0;facecontinuous::ActivateWanted(cycling);
    MhmWeightState state{};uint32_t count=0;uint64_t req=0,app=0;
    check(!cycling.asset&&!cycling.adapter->Live()&&switchApi.read_weights(oldHandle,&state,1,&count,&req,&app)==MHM_STALE,"unregistered equipment has no override and retires old handle");
    cycling.wanted=0;facecontinuous::ActivateWanted(cycling);
    check(cycling.buffers[0].wrapper==0xABC&&cycling.reuseFence==77&&cycling.reusable,"return to first model restores only its own cached pool");
    cycling.binding=std::make_unique<equipmentmorph::PartBinding>();cycling.binding->Attach(*cycling.asset);
    check(switchApi.read_weights(cycling.binding->target,&state,1,&count,&req,&app)==MHM_OK&&state.requested==0,"new target starts at zero without control replay");
    check(switchApi.submit(sourceId,&requested,1,&switchRevision)==MHM_STALE,"old target update cannot reach newly selected equipment");
    switchApi.close_source(sourceId);cycling.binding.reset();facecontinuous::armed=false;
    }
    // Logging may block. A concurrent callback must still acquire the native gate,
    // while the log record keeps the state captured before that callback.
    {
        facecontinuous::Part loggingPart;loggingPart.asset=std::make_shared<const mhm::Asset>(asset);
        loggingPart.adapter->Arm();loggingPart.target=42;loggingPart.updates=7;
        struct BlockingLog {
            facecontinuous::Part& part;bool callbackRan=false;std::string record;
            bool Write(const char* line){
                std::thread callback([&]{
                    if(facecontinuous::gate.try_lock()){
                        callbackRan=true;part.adapter->Invalidate(0,"logging_test_callback");facecontinuous::gate.unlock();
                    }
                });callback.join();record=line;return true;
            }
        } log{loggingPart,false,{}};
        facecontinuous::WriteState(log,loggingPart,"part_state");
        check(log.callbackRan,"callback can acquire gate during state logging");
        check(!loggingPart.adapter->Live()&&log.record.find("\"live\":true")!=log.record.npos
              &&log.record.find("\"reason\":\"none\"")!=log.record.npos,"log retains consistent snapshot when callback changes adapter");
        check(log.record.find("\"target\":42")!=log.record.npos&&log.record.find("\"updates\":7")!=log.record.npos
              &&log.record.back()=='\n',"state logging keeps worker fields and complete record");
    }
    std::printf("PASS: %u runtime native checks; synthetic model, no game/GPU calls\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
