#define MHM_BUILD
#include "core.hpp"
#include <exception>
namespace {
mhm::Runtime& Core(){static mhm::Runtime instance;return instance;}
template<class F>MhmResult Safe(F&& f) noexcept {try{return f();}catch(...){return MHM_INTERNAL;}}
MhmResult MHM_CALL Limits(MhmLimits* out){if(!out)return MHM_INVALID;*out=mhm::limits;return MHM_OK;}
MhmResult MHM_CALL TargetAt(uint32_t i,MhmTargetInfo* out){return Safe([&]{return out?Core().TargetAt(i,*out):MHM_INVALID;});}
MhmResult MHM_CALL KeyAt(uint64_t h,uint32_t i,MhmKeyInfo* out){return Safe([&]{return out?Core().KeyAt(h,i,*out):MHM_INVALID;});}
MhmResult MHM_CALL Open(const char* name,uint32_t priority,uint32_t lease,uint64_t* out){return Safe([&]{return out?Core().Open(name,priority,lease,*out):MHM_INVALID;});}
MhmResult MHM_CALL Close(uint64_t id){return Safe([&]{return Core().Close(id);});}
MhmResult MHM_CALL Heartbeat(uint64_t id){return Safe([&]{return Core().Heartbeat(id);});}
MhmResult MHM_CALL Submit(uint64_t id,const MhmUpdate* values,uint32_t count,uint64_t* rev){
    if(!rev||(!values&&count))return MHM_INVALID;
    if(count>mhm::limits.batch_updates)return MHM_CAPACITY;
    return Safe([&]{return Core().Submit(id,{values,count},*rev);});
}
MhmResult MHM_CALL Read(uint64_t target,MhmWeightState* values,uint32_t capacity,uint32_t* count,uint64_t* rev,uint64_t* applied){
    if((!values&&capacity)||!count||!rev||!applied)return MHM_INVALID;
    return Safe([&]{return Core().Read(target,{values,capacity},*count,*rev,*applied);});
}
MhmResult MHM_CALL Poll(uint64_t cursor,MhmEvent* out){return Safe([&]{return out?Core().PollEvent(cursor,*out):MHM_INVALID;});}
}
extern "C" MhmResult MHM_CALL MhwMorphGetApi(uint32_t version,uint32_t bytes,MhmApi* out){
    if(!out)return MHM_INVALID;
    if(version!=MHM_API_VERSION||bytes!=sizeof(MhmApi))return MHM_VERSION;
    MhmApi table{MHM_API_VERSION,sizeof(MhmApi),MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE|MHM_CAP_EVENTS|MHM_CAP_CPU_COMPOSE|MHM_CAP_PART_LIFECYCLE|MHM_CAP_PER_KEY_SHADING,0,
                 Limits,TargetAt,KeyAt,Open,Close,Heartbeat,Submit,Read,Poll};
    *out=table;return MHM_OK;
}
extern "C" MhmResult MHM_CALL MhwMorphHostAttach(const MhmHostTarget* input,uint64_t* handle){
    if(!input||!handle||!input->model_id||!input->basis||!input->keys||
       !input->basis_bytes||input->basis_bytes>64*1024*1024||!input->key_count||input->key_count>mhm::limits.keys_per_target||
       input->rotation_count>1000000||(!input->rotations&&input->rotation_count)||!mhm::Identifier(input->model_id,128))return MHM_INVALID;
    return Safe([&]() -> MhmResult {
        mhm::Asset a;a.part=input->part;a.model=input->model_id;
        a.basis.assign(input->basis,input->basis+input->basis_bytes);
        for(uint32_t i=0;i<input->key_count;i++){
            const auto& k=input->keys[i];if(k.count>1000000||(!k.deltas&&k.count))return MHM_INVALID;
            mhm::Key key;key.info=k.info;if(k.count)key.deltas.assign(k.deltas,k.deltas+k.count);a.keys.push_back(std::move(key));
        }
        if(input->rotation_count)a.rotations.assign(input->rotations,input->rotations+input->rotation_count);
        return Core().Attach(std::move(a),*handle);
    });
}
extern "C" MhmResult MHM_CALL MhwMorphGetAssetIdentity(uint32_t version,uint32_t bytes,uint64_t handle,MhmAssetIdentity* out){
    if(!out)return MHM_INVALID;if(version!=MHM_IDENTITY_VERSION||bytes!=sizeof(MhmAssetIdentity))return MHM_VERSION;
    return Safe([&]{return Core().Identity(handle,*out);});
}
extern "C" MhmResult MHM_CALL MhwMorphHostDetach(uint64_t handle){return Safe([&]{return Core().Detach(handle);});}
extern "C" MhmResult MHM_CALL MhwMorphHostTick(uint64_t now){return Safe([&]{return Core().Tick(now);});}
extern "C" MhmResult MHM_CALL MhwMorphHostCapture(uint64_t* id,uint64_t* rev){return Safe([&]{return id&&rev?Core().Capture(*id,*rev):MHM_INVALID;});}
extern "C" MhmResult MHM_CALL MhwMorphHostCaptureInterpolated(uint64_t target,const float* weights,uint32_t count,uint64_t expected,uint64_t* id){
    if(!weights||!id||!count||count>mhm::limits.keys_per_target)return MHM_INVALID;
    return Safe([&]{return Core().CaptureInterpolated(target,{weights,count},expected,*id);});
}
extern "C" MhmResult MHM_CALL MhwMorphHostCaptureInterpolatedGroup(const MhmUpdate* values,uint32_t count,uint64_t expected,uint64_t* id){
    if(!values||!id||!count)return MHM_INVALID;
    if(count>mhm::limits.batch_updates)return MHM_CAPACITY;
    return Safe([&]{return Core().CaptureInterpolatedGroup({values,count},expected,*id);});
}
extern "C" MhmResult MHM_CALL MhwMorphHostCompose(uint64_t frame,uint64_t target,unsigned char* out,uint32_t bytes){
    if(!out||!bytes)return MHM_INVALID;return Safe([&]{return Core().ComposeFrame(frame,target,{out,bytes});});
}
extern "C" MhmResult MHM_CALL MhwMorphHostAcknowledge(uint64_t id){return Safe([&]{return Core().Acknowledge(id);});}
extern "C" MhmResult MHM_CALL MhwMorphHostReleaseFrame(uint64_t id){return Safe([&]{return Core().ReleaseFrame(id);});}
