// Candidate only. Build does not install or run game hooks.
#define NOMINMAX
#define OBSERVER_TEST
#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include "../../runtime/EquipmentMorphPerKeyCore/group_smoothing.hpp"
#include "../../runtime/EquipmentMorphPerKeyCore/asset_pack.hpp"
#include "../../runtime/EquipmentMorphPerKeyCore/json_log.hpp"
#include "../../runtime/EquipmentMorphAutoBinding/LocalPlayer.hpp"
#include "InputSnapshot.cpp"
#include "EquipmentLifecycleBuild.hpp"
#include "../SubmissionRetirementWorkerObserver/FrameBuild.hpp"
#include "../SubmissionRetirementObserver/FenceReader.hpp"
#include "../PrivateDynamicBufferProbe/Payload.hpp"
#include "../PrivateDynamicBufferProbe/CodeBuild.hpp"
#include "LaunchAdapter.hpp"
#include "BufferedLog.hpp"
#include "HighResolutionClock.hpp"
#include "NativeBuffer.hpp"
#include "SlotContract.hpp"
#include "RuntimeInput.hpp"
#include "PartBinding.hpp"
#include "LaunchContext.hpp"
#include "ManualArm.hpp"
#include "DrainBoundary.hpp"
#include <tuple>

namespace facecontinuous {
struct BufferPool {
    std::array<privateprobe::Buffer,3> buffers{};
    std::array<std::uint64_t,3> stamps{},drainStamps{};
    std::uint64_t reuseFence=0;
    bool allocated=false,reusable=false;
};
struct Part : BufferPool {
    unsigned index=0;std::uint64_t epoch=1,target=0,updates=0,replacements=0,drainDeadline=0;
    std::unique_ptr<LaunchAdapter> adapter=std::make_unique<LaunchAdapter>();
    std::unique_ptr<equipmentmorph::PartBinding> binding;
    std::shared_ptr<const mhm::Asset> asset;
    std::vector<BufferPool> cached;
    std::optional<size_t> active,wanted;
    DrainBoundary drain;bool retiring=false,blocked=false;
    mhm::GroupSmoothing smoother;facesmooth::SampleGate cadence;
    std::vector<unsigned char> payload;
    std::vector<float> requested,lastSample;std::uint64_t smoothingRevision=0,lastRevision=0;
};
std::array<Part,5> parts;
std::mutex gate;
bool armed=false,enabled=false;
std::uintptr_t anchorPlayer=0,resourceRenderer=0,resourceUploadFence=0,resourceDrawingFence=0;
mhm::localplayer::Selection mainPlayerSelection{};
std::array<std::uintptr_t,5> mainPlayerOwners{};
std::array<safetyhook::MidHook*,10> hooks{};
std::array<std::array<unsigned char,32>,10> patches{};
constexpr std::array<std::uintptr_t,10> sites={observed::computeSiteRva,observed::drawSiteRva,observed::dispatchSiteRva,
    frameobserver::frameRva,frameobserver::waitRva,frameobserver::syncRva,facelife::removeRva,facelife::destructorRva,launchRva,facelife::modelRva};
const std::array<std::span<const unsigned char>,10> windows={observed::computeBytes,observed::drawBytes,observed::dispatchBytes,
    frameobserver::frameBytes,frameobserver::waitBytes,frameobserver::syncBytes,facelife::removeBytes,facelife::destructorBytes,launchBytes,facelife::modelBytes};
std::uint64_t Qpc(){LARGE_INTEGER q{};::QueryPerformanceCounter(&q);return static_cast<std::uint64_t>(q.QuadPart);}
#include "NativeFacts.inc"

Identity Anchor() noexcept {
    Identity body{};body.player=body.owner=anchorPlayer;std::uintptr_t bridge=0,table=0,linked=0;
    if(!anchorPlayer||!observer::Read(anchorPlayer,table)||table!=observer::imageBase+observed::ownerVtableRva
            ||!observer::Read(anchorPlayer+0x126C8,bridge)||!observer::Read(bridge,table)||table!=observer::imageBase+0x324A830
            ||!observer::Read(bridge+0x60,linked)||linked!=anchorPlayer||!observer::Read(bridge+0xF0,linked)||linked!=anchorPlayer
            ||!observer::Read(anchorPlayer+0x2A0,body.model)||!observer::Read(body.model+0x110,body.source))return {};
    return body;
}
bool Matches(unsigned index,Identity id,bool mesh,bool selectionChecked=false) {
    return id.player==anchorPlayer&&(selectionChecked||mhm::localplayer::StillSelected(
        [](std::uintptr_t address,auto& value){return observer::Read(address,value);},observer::imageBase,mainPlayerSelection)
        )&&SlotMatches(index,id,mesh);
}
bool RebindReason(const char* reason) {
    for(const auto* allowed:{"equipment_player_removed","equipment_player_destroyed","equipment_part_model_replaced","equipment_part_identity_changed","target_identity_changed","equipment_main_player_changed"})
        if(std::strcmp(reason,allowed)==0)return true;
    return false;
}
void DisableAll(const char* reason) noexcept {armed=false;for(auto& part:parts){part.adapter->Invalidate(0,reason);if(part.retiring)part.drain.Refuse();}}
bool Watching() {for(const auto& part:parts)if(part.adapter->Ready()||part.drain.Observing())return true;return false;}
void ArmConfiguredParts(){
    std::lock_guard lock(gate);
    for(auto& part:parts)if(part.asset&&!part.adapter->Arm())throw std::runtime_error("Automatic part arm");
    armed=true;
}
bool RefreshMainPlayer(){
    auto current=mhm::localplayer::Select([](std::uintptr_t address,auto& value){return observer::Read(address,value);},observer::imageBase);
    if(current.status!=mhm::localplayer::Status::Ready||!PlayerAnchorMatches(current.player))current.player=0;
    std::array<std::uintptr_t,5> owners{};std::uintptr_t bridge=0;
    if(current.player&&observer::Read(current.player+0x126C8,bridge)){
        for(unsigned i=0;i<owners.size();i++)if(!equipmentnative::catalogue.parts[i].empty()){
            if(i==MHM_BODY)owners[i]=current.player;
            else observer::Read(bridge+equipmentnative::profiles[i].ownerField,owners[i]);
        }
    }
    std::lock_guard lock(gate);const bool changed=anchorPlayer!=current.player;
    if(changed&&anchorPlayer)for(auto& part:parts)part.adapter->Invalidate(0,"equipment_main_player_changed");
    mainPlayerSelection=current;anchorPlayer=current.player;mainPlayerOwners=owners;
    for(unsigned i=0;i<parts.size();i++){
        auto& part=parts[i];std::optional<size_t> wanted;std::uintptr_t model=0,table=0;std::array<char,80> path{};
        if(owners[i]&&observer::Read(owners[i]+0x2A0,model)&&observer::Read(model,table)
            &&table==observer::imageBase+observed::modelVtableRva&&observer::Read(model+0xC,path)
            &&std::find(path.begin(),path.end(),'\0')!=path.end()){
            const auto id=mhm::configuration::Fold(path.data());const auto& entries=equipmentnative::catalogue.parts[i];
            for(size_t j=0;j<entries.size();j++)if(mhm::configuration::Fold(entries[j]->profile.model)==id){wanted=j;break;}
        }
        part.wanted=wanted;
        if(part.asset&&part.active!=wanted)part.adapter->Invalidate(0,"equipment_part_identity_changed");
    }
    return changed;
}
void Input(safetyhook::Context& context,unsigned kind) noexcept {
    std::lock_guard lock(gate);if(!armed)return;
    if(!anchorPlayer)return;
    std::uintptr_t owner=kind==1?context.rdi:context.r15;
    if(kind==3&&!observer::Read(context.rbp+0xD0,owner)){DisableAll("dispatch_owner_unreadable");return;}
    // Reject other scene owners with a cached pointer comparison before any
    // selector/snapshot reads. Discovery refreshes these routes every 50 ms.
    if(std::find(mainPlayerOwners.begin(),mainPlayerOwners.end(),owner)==mainPlayerOwners.end())return;
    Part* selected=nullptr;for(auto& part:parts)if(part.adapter->Live()&&part.adapter->Target().owner==owner)selected=&part;
    // This cycle publishes only native original inputs. Leave registers and
    // bindings untouched; lifecycle discovery and worker/Launch checks remain.
    if(selected&&!selected->retiring&&selected->adapter->PassOriginalInput())return;
    if(!mhm::localplayer::StillSelected([](std::uintptr_t address,auto& value){return observer::Read(address,value);},observer::imageBase,mainPlayerSelection)){
        anchorPlayer=0;for(auto& part:parts)part.adapter->Invalidate(0,"equipment_main_player_changed");return;
    }
    // Only the independently selected main player can own a captured part.
    InputFacts e{};std::uintptr_t renderer=0,fence=0;std::uint64_t before=0,after=0;
    if(!RendererFacts(renderer,fence,before)||!ReadInputFacts(context,kind,anchorPlayer,e)||e.owner!=owner
            ||(kind==2&&e.auxEnabled!=0)||e.ownerStreams[0]||e.ownerStreams[1]||e.ownerStreams[2]
            ||!observer::Read(renderer+0x1E0,after)) {
        if(selected){const auto id=selected->adapter->Target();selected->adapter->Invalidate(0,Matches(selected->index,id,false,true)?"part_input_snapshot_failed":"equipment_part_identity_changed");}
        return;
    }
    if(e.part>=parts.size()||!parts[e.part].asset)return;
    auto& part=parts[e.part];auto& adapter=*part.adapter;
    if(!adapter.Live())return;
    if(!adapter.Target().Valid()) {
        if(kind==3||e.input!=e.originalInput)return;
        const Identity candidate{e.owner,e.model,e.originalInput,anchorPlayer};
        adapter.Capture(candidate,e.input);
        return;
    }
    if(!adapter.Ready()||owner!=adapter.Target().owner)return;
    const auto actual=adapter.Target();
    if(actual!=Identity{e.owner,e.model,e.originalInput,anchorPlayer}){adapter.Invalidate(0,"equipment_part_identity_changed");return;}
    if(adapter.Started()&&(renderer!=adapter.Fixed().renderer||fence!=adapter.Fixed().fence)){DisableAll("renderer_changed");return;}
    const Identity seen{e.owner,e.model,e.originalInput,actual.player};
    const auto replacement=adapter.Input(seen,e.input,before,after,kind==3,kind);
    if(replacement!=e.input){
        bool matches=false;for(auto& buffer:part.buffers)if(buffer.wrapper==replacement)matches=privateprobe::Contract(buffer);
        if(!matches){adapter.Invalidate(0,"part_private_buffer_contract");return;}
        context.r8=replacement;part.replacements++;
    }
}
void Source(safetyhook::Context& c){Input(c,1);}
void Stream(safetyhook::Context& c){Input(c,2);}
void Dispatch(safetyhook::Context& c){Input(c,3);}
void WaitReturned(safetyhook::Context& c) noexcept {
    std::lock_guard lock(gate);if(!Watching())return;Frame f{};std::uintptr_t event=0;const auto index=static_cast<unsigned>(c.rbx);
    if(!EngineFacts(c.rdi,f)||index>=f.workers||!observer::Read(c.rdi+0x7AED8+index*8,event)){DisableAll("wait_snapshot");return;}
    for(auto& part:parts){if(part.drain.Observing())part.drain.Wait(f,index,event,static_cast<unsigned>(c.rax));if(part.adapter->Ready())part.adapter->Wait(f,index,event,static_cast<unsigned>(c.rax));}
}
void SyncReturned(safetyhook::Context& c) noexcept {
    std::lock_guard lock(gate);if(!Watching())return;Frame f{};
    if(!EngineFacts(c.rbx,f)){DisableAll("sync_snapshot");return;}
    for(auto& part:parts){if(part.drain.Observing())part.drain.Join(f);if(part.adapter->Ready())part.adapter->Boundary(f);}
}
void SignalReturned(safetyhook::Context& c) noexcept {
    std::lock_guard lock(gate);if(!Watching())return;std::uintptr_t renderer=0,fence=0;std::uint64_t before=0,after=0;
    if(!RendererFacts(renderer,fence,before)||renderer!=c.rbx||!observer::Read(renderer+0x1E0,after)){DisableAll("signal_snapshot");return;}
    for(auto& part:parts){if(part.drain.Observing())part.drain.Signal(renderer,fence,before,after,c.rsi,static_cast<std::int32_t>(c.rax)>=0);
        if(part.adapter->Started())part.adapter->Signal(renderer,fence,before,after,c.rsi,static_cast<std::int32_t>(c.rax)>=0);}
}
void LaunchStarting(safetyhook::Context& c) noexcept {
    std::lock_guard lock(gate);if(!Watching())return;const auto args=LaunchArguments(c);Frame f{};std::uintptr_t event=0;
    if(!args.valid||!EngineFacts(args.engine,f)||args.index>=f.workers||!observer::Read(args.engine+0x7AE98+args.index*8,event)){DisableAll("launch_snapshot");return;}
    for(auto& part:parts){if(part.drain.Observing())part.drain.Launch(f,args.index,event);
        if(part.adapter->Ready()){
            // Only worker zero can publish a new input. Validate identity there
            // and for unstarted bindings; later workers still prove the same
            // frame/event/order in Launch. Lifecycle and nonzero Input guard
            // identity changes within the batch without a cross-cycle cache.
            if((args.index==0||!part.adapter->Started())&&!Matches(part.index,part.adapter->Target(),false))part.adapter->Invalidate(0,"equipment_part_identity_changed");
            else part.adapter->Launch(f,args.index,event);
        }}
}
void PlayerGone(std::uintptr_t owner,const char* reason) noexcept {
    std::lock_guard lock(gate);if(owner==anchorPlayer){anchorPlayer=0;for(auto& part:parts)part.adapter->Invalidate(0,reason);}
}
void Remove(safetyhook::Context& c){PlayerGone(c.rcx,"equipment_player_removed");}
void Destructor(safetyhook::Context& c){PlayerGone(c.rcx,"equipment_player_destroyed");}
void ModelChanging(safetyhook::Context& c){
    std::lock_guard lock(gate);for(auto& part:parts){const auto id=part.adapter->Target();
        if(id.Valid()&&c.rcx==id.owner&&c.rdx!=id.model)part.adapter->Invalidate(0,"equipment_part_model_replaced");}
}
bool CanSubmit(const launchpool::View& view){
    if(!view.live||view.requested!=view.applied)return false;
    for(const auto& slot:view.slots)if(slot.phase==launchpool::Phase::Preparing||slot.phase==launchpool::Phase::AwaitingStamp||slot.phase==launchpool::Phase::Uploading||slot.phase==launchpool::Phase::Ready)return false;
    return true;
}
std::string Weights(const std::array<float,64>& values,unsigned count){
    std::string result="[";char text[40]{};for(unsigned i=0;i<count;i++){if(i)result+=',';std::snprintf(text,sizeof(text),"%.9g",double(values[i]));result+=text;}return result+"]";
}
template<class Log> void WriteState(Log& log,Part& part,const char* event){
    // Two 64-float arrays plus bounded integer/identity fields fit in 4096 bytes.
    const auto [v,id,live,ready,started,reason,replacements,originalBypasses]=[&]{
        std::lock_guard lock(gate);const auto& a=*part.adapter;
        return std::tuple{part.adapter->View(),a.Target(),a.Live(),a.Ready(),a.Started(),a.Reason(),part.replacements,a.OriginalBypasses()};
    }();
    // Remaining part fields belong to this worker. Formatting and disk IO must
    // not hold the gate also used by game callbacks.
    char line[4096]{};
    const auto requested=Weights(v.requested.weights,static_cast<unsigned>(part.asset->keys.size())),applied=Weights(v.applied.weights,static_cast<unsigned>(part.asset->keys.size()));
    const auto count=std::snprintf(line,sizeof(line),"{\"event\":\"%s\",\"part\":%u,\"epoch\":%llu,\"target\":%llu,\"live\":%s,\"ready\":%s,\"started\":%s,\"reason\":\"%s\",\"player\":\"0x%llx\",\"owner\":\"0x%llx\",\"model\":\"0x%llx\",\"requested\":%s,\"applied\":%s,\"requested_frame\":%llu,\"applied_frame\":%llu,\"requested_revision\":%llu,\"applied_revision\":%llu,\"last_signal\":%llu,\"publication_counter\":%llu,\"active_slot\":%d,\"updates\":%llu,\"replacements\":%llu,\"original_input_bypasses\":%llu}\n",event,part.asset->part,part.epoch,part.target,live?"true":"false",ready?"true":"false",started?"true":"false",reason,id.player,id.owner,id.model,requested.c_str(),applied.c_str(),v.requested.frame,v.applied.frame,v.requested.revision,v.applied.revision,v.lastSignal,v.publicationCounter,v.active,part.updates,replacements,originalBypasses);
    if(count<0||static_cast<size_t>(count)>=sizeof(line)||!log.Write(line))throw std::runtime_error("Part state log");
}

#include "PartWorker.inc"
#include "Startup.inc"
}
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){HANDLE worker=::CreateThread(nullptr,0,facecontinuous::Worker,nullptr,0,nullptr);if(worker)::CloseHandle(worker);}return TRUE;
}
