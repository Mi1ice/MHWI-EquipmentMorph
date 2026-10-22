#pragma once
#include "include/mhw_morph_host.h"
#include "include/mhw_morph_identity.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace mhm {
inline constexpr MhmLimits limits{5,64,32,320,256};
struct Key { MhmKeyInfo info{}; std::vector<MhmDelta> deltas,normals; };
struct Asset {
    uint32_t part=0;
    std::string model;
    std::string modelSha,packSha;
    std::vector<unsigned char> basis;
    std::vector<Key> keys;
    std::vector<MhmRotation> rotations;
    bool perKeyNormals=false;
};
inline bool Identifier(const char* text, size_t maximum) {
    if(!text||!text[0])return false;
    for(size_t i=0;i<maximum;i++) {
        const auto c=static_cast<unsigned char>(text[i]);
        if(c==0)return true;
        if(c<32||c>126)return false;
    }
    return false;
}
inline bool Validate(const Asset& a) {
    if(a.part>MHM_LEGS||!Identifier(a.model.c_str(),128)||a.basis.empty()||a.basis.size()>64*1024*1024||
       a.keys.empty()||a.keys.size()>limits.keys_per_target)return false;
    // All edits must fit a declared field. Identical position fields may overlap
    // across keys; partial byte overlap or overlap with shading is never allowed.
    std::map<uint32_t,uint32_t> fields;
    for(size_t i=0;i<a.keys.size();i++) {
        const auto& k=a.keys[i];
        if(!Identifier(k.info.id,64)||k.info.minimum!=0||k.info.maximum!=1
                ||(!a.perKeyNormals&&(k.info.flags>1||!k.normals.empty()))
                ||(a.perKeyNormals&&k.info.flags!=0&&k.info.flags!=MHM_PER_KEY_SHADING))return false;
        for(size_t j=0;j<i;j++)if(std::strcmp(a.keys[j].info.id,k.info.id)==0)return false;
        uint64_t previous=0;
        for(size_t j=0;j<k.deltas.size();j++) {
            const auto& d=k.deltas[j];
            if(static_cast<uint64_t>(d.offset)+12>a.basis.size()||(j&&d.offset<previous)||
               !std::isfinite(d.x)||!std::isfinite(d.y)||!std::isfinite(d.z))return false;
            float base[3]{};std::memcpy(base,a.basis.data()+d.offset,12);
            for(float v:base)if(!std::isfinite(v))return false;
            previous=static_cast<uint64_t>(d.offset)+12;
            fields[d.offset]=12;
        }
    }
    uint64_t previous=0;
    if(a.perKeyNormals&&!a.rotations.empty())return false;
    for(size_t j=0;j<a.rotations.size();j++) {
        const auto& r=a.rotations[j];
        const double n=static_cast<double>(r.x)*r.x+static_cast<double>(r.y)*r.y+static_cast<double>(r.z)*r.z;
        if(static_cast<uint64_t>(r.offset)+8>a.basis.size()||(j&&r.offset<previous)||
           !std::isfinite(n)||n<.9999||n>1.0001||!std::isfinite(r.angle)||r.angle<0||r.angle>3.141593f)return false;
        if(fields.contains(r.offset))return false;
        fields[r.offset]=8;previous=static_cast<uint64_t>(r.offset)+8;
    }
    size_t normalCount=0;
    for(const auto& key:a.keys){
        if(!key.normals.empty()&&key.info.flags!=MHM_PER_KEY_SHADING)return false;
        normalCount+=key.normals.size();if(normalCount>1000000)return false;uint64_t next=0;
        for(const auto& d:key.normals){
            if(d.offset<next||static_cast<uint64_t>(d.offset)+8>a.basis.size()
                    ||!std::isfinite(d.x)||!std::isfinite(d.y)||!std::isfinite(d.z))return false;
            int8_t packed[3]{};std::memcpy(packed,a.basis.data()+d.offset,3);
            const double length=std::sqrt(double(packed[0])*packed[0]+double(packed[1])*packed[1]+double(packed[2])*packed[2]);
            if(length<1e-8)return false;
            const double base[3]={packed[0]/length,packed[1]/length,packed[2]/length};
            const double target[3]={base[0]+d.x,base[1]+d.y,base[2]+d.z};
            const double magnitude=target[0]*target[0]+target[1]*target[1]+target[2]*target[2];
            const double dot=base[0]*target[0]+base[1]*target[1]+base[2]*target[2];
            if(!std::isfinite(magnitude)||magnitude<.9999||magnitude>1.0001||dot<-.999999)return false;
            if(fields.contains(d.offset)&&fields[d.offset]!=8)return false;
            fields[d.offset]=8;next=static_cast<uint64_t>(d.offset)+8;
        }
    }
    uint64_t end=0;
    for(auto [start,size]:fields){if(start<end)return false;end=static_cast<uint64_t>(start)+size;}
    return true;
}
inline bool Compose(const Asset& a, std::span<const float> weights, std::vector<unsigned char>& output) {
    if(weights.size()!=a.keys.size())return false;
    for(float w:weights)if(!std::isfinite(w)||w<0||w>1)return false;
    auto work=a.basis;
    float shading=0;
    // Canonical key order, basis copied once. Use float32 arithmetic to retain
    // the validated single-key payload; client batch order does not affect it.
    for(size_t i=0;i<weights.size();i++) {
        const auto weight=weights[i];if(weight==0)continue;
        if(a.keys[i].info.flags&MHM_AUTHOR_SHADING)shading=std::max(shading,weight);
        for(const auto& d:a.keys[i].deltas) {
            float p[3]{};std::memcpy(p,work.data()+d.offset,12);
            p[0]+=weight*d.x;p[1]+=weight*d.y;p[2]+=weight*d.z;
            if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))return false;
            std::memcpy(work.data()+d.offset,p,12);
        }
    }
    // This asset's shared author-intent field is applied ONCE at max participating
    // weight, not once per morph. MHMPACK2 uses the separate branch below.
    if(shading!=0)for(const auto& r:a.rotations) {
        const double x=r.x,y=r.y,z=r.z,angle=static_cast<double>(r.angle)*shading;
        const double cosine=std::cos(angle),sine=std::sin(angle);
        for(unsigned add:{0u,4u}) {
            int8_t packed[3]{};std::memcpy(packed,a.basis.data()+r.offset+add,3);
            const double aa=packed[0],bb=packed[1],cc=packed[2],dot=x*aa+y*bb+z*cc;
            const double rotated[3]={aa*cosine+(y*cc-z*bb)*sine+x*dot*(1-cosine),
                bb*cosine+(z*aa-x*cc)*sine+y*dot*(1-cosine),cc*cosine+(x*bb-y*aa)*sine+z*dot*(1-cosine)};
            for(unsigned k=0;k<3;k++)packed[k]=static_cast<int8_t>(std::clamp(std::nearbyint(rotated[k]),-128.0,127.0));
            std::memcpy(work.data()+r.offset+add,packed,3);
        }
    }
    if(a.perKeyNormals){
        // Add endpoint deltas in canonical key order, normalize once. Client
        // batch order never selects a sequence of noncommuting rotations.
        std::map<uint32_t,std::array<double,3>> sums;
        for(size_t i=0;i<weights.size();i++)if(weights[i]!=0)for(const auto& d:a.keys[i].normals){
            auto& sum=sums[d.offset];sum[0]+=weights[i]*double(d.x);sum[1]+=weights[i]*double(d.y);sum[2]+=weights[i]*double(d.z);
        }
        for(const auto& [offset,sum]:sums){
            int8_t normal[3]{},tangent[3]{};std::memcpy(normal,a.basis.data()+offset,3);std::memcpy(tangent,a.basis.data()+offset+4,3);
            const double length=std::sqrt(double(normal[0])*normal[0]+double(normal[1])*normal[1]+double(normal[2])*normal[2]);
            if(length<1e-8)return false;
            const std::array<double,3> base={normal[0]/length,normal[1]/length,normal[2]/length};
            std::array<double,3> target={base[0]+sum[0],base[1]+sum[1],base[2]+sum[2]};
            const double magnitude=std::sqrt(target[0]*target[0]+target[1]*target[1]+target[2]*target[2]);
            if(!std::isfinite(magnitude))return false;
            // Valid endpoints may cancel in combination. There is no normal
            // direction (or unique antipodal tangent transport) in these cases;
            // retain this field's baseline shading while keeping composed xyz.
            if(magnitude<1e-8)continue;
            for(auto& v:target)v/=magnitude;
            const double cosine=std::clamp(base[0]*target[0]+base[1]*target[1]+base[2]*target[2],-1.0,1.0);
            if(cosine<-.999999)continue;
            std::array<double,3> axis={base[1]*target[2]-base[2]*target[1],base[2]*target[0]-base[0]*target[2],base[0]*target[1]-base[1]*target[0]};
            const double sine=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
            if(sine>1e-12)for(auto& v:axis)v/=sine;
            const double dot=axis[0]*tangent[0]+axis[1]*tangent[1]+axis[2]*tangent[2];
            const double cross[3]={axis[1]*tangent[2]-axis[2]*tangent[1],axis[2]*tangent[0]-axis[0]*tangent[2],axis[0]*tangent[1]-axis[1]*tangent[0]};
            for(unsigned k=0;k<3;k++){
                normal[k]=static_cast<int8_t>(std::clamp(std::nearbyint(target[k]*length),-128.0,127.0));
                const double transported=sine>1e-12?tangent[k]*cosine+cross[k]*sine+axis[k]*dot*(1-cosine):tangent[k];
                work[offset+4+k]=static_cast<unsigned char>(static_cast<int8_t>(std::clamp(std::nearbyint(transported),-128.0,127.0)));
            }
            std::memcpy(work.data()+offset,normal,3);
        }
    }
    output.swap(work);return true;
}
struct Target { uint64_t handle=0,appliedRevision=0,appliedFrame=0; std::shared_ptr<const Asset> asset; std::vector<float> applied; };
struct Source { uint64_t id=0,deadline=0; uint32_t priority=0,lease=0; std::vector<MhmUpdate> values; };
struct FrameTarget { uint64_t handle=0;std::shared_ptr<const Asset> asset;std::vector<float> weights; };
struct Frame { uint64_t id=0,revision=0;std::vector<FrameTarget> targets; };
class Runtime {
    mutable std::mutex gate;
    uint64_t now=0,nextHandle=0,nextSource=0,nextFrame=0,revision=0,nextEvent=0;
    std::map<uint64_t,Target> targets;
    std::map<uint64_t,Source> sources;
    std::map<uint64_t,std::shared_ptr<const Frame>> frames;
    std::deque<MhmEvent> events;
    // Called under gate. A snapshot belongs to its own members, not to a
    // global target-set epoch. Full snapshots still fail if ANY member retires.
    bool FrameLive(const Frame& frame) const {
        for(const auto& member:frame.targets) {
            const auto it=targets.find(member.handle);
            if(it==targets.end()||it->second.asset!=member.asset)return false;
        }
        return !frame.targets.empty();
    }
    void Event(uint32_t type,uint64_t handle) {
        events.push_back({++nextEvent,handle,revision,type,0});
        if(events.size()>limits.events)events.pop_front();
    }
    std::vector<MhmWeightState> Effective(const Target& target) const {
        std::vector<MhmWeightState> result(target.asset->keys.size());
        for(const auto& [id,s]:sources)for(const auto& v:s.values)if(v.target==target.handle) {
            auto& current=result[v.key];
            // Map order gives older source deterministic precedence on ties.
            if(!current.source||s.priority>sources.at(current.source).priority) {
                current.requested=v.weight;current.source=id;
            }
        }
        // Releasing a masking source can reveal incompatible lower layers.
        // Resolve exclusive groups deterministically on every read, not only
        // when accepting a batch. Priority, older source, then key index wins.
        std::map<uint32_t,size_t> winners;
        for(size_t i=0;i<result.size();i++) {
            const auto group=target.asset->keys[i].info.exclusive_group;
            if(!group||result[i].requested==0)continue;
            auto [it,inserted]=winners.emplace(group,i);
            if(!inserted) {
                const auto previous=it->second;
                const auto a=result[i].source,b=result[previous].source;
                if(sources.at(a).priority>sources.at(b).priority||
                   (sources.at(a).priority==sources.at(b).priority&&a<b))it->second=i;
            }
        }
        for(size_t i=0;i<result.size();i++) {
            const auto group=target.asset->keys[i].info.exclusive_group;
            if(group&&result[i].requested>0&&winners.at(group)!=i){result[i].requested=0;result[i].source=0;}
            result[i].applied=target.applied[i];
        }
        return result;
    }
    bool Conflicts(const std::map<uint64_t,Source>& candidate) const {
        for(const auto& [id,s]:candidate) {
            (void)id;std::map<std::pair<uint64_t,uint32_t>,unsigned> groups;
            for(const auto& v:s.values) {
                const auto group=targets.at(v.target).asset->keys[v.key].info.exclusive_group;
                if(group&&v.weight>0&&++groups[{v.target,group}]>1)return true;
            }
        }
        return false;
    }
public:
    MhmResult Attach(Asset asset,uint64_t& handle) {
        if(!Validate(asset))return MHM_INVALID;
        auto data=std::make_shared<const Asset>(std::move(asset));
        std::lock_guard lock(gate);
        if(targets.size()>=limits.targets)return MHM_CAPACITY;
        for(const auto& [id,t]:targets){(void)id;if(t.asset->part==data->part)return MHM_CONFLICT;}
        if(nextHandle==UINT64_MAX)return MHM_CAPACITY;
        Target t;t.handle=++nextHandle;t.asset=data;t.applied.resize(data->keys.size());
        targets.emplace(t.handle,t);handle=t.handle;++revision;Event(MHM_TARGET_ADDED,handle);return MHM_OK;
    }
    MhmResult Detach(uint64_t handle) {
        std::lock_guard lock(gate);if(!targets.erase(handle))return MHM_STALE;
        for(auto& [id,s]:sources){(void)id;std::erase_if(s.values,[&](const auto& v){return v.target==handle;});}
        ++revision;Event(MHM_TARGET_REMOVED,handle);return MHM_OK;
    }
    MhmResult Tick(uint64_t value) {
        std::lock_guard lock(gate);if(value<now)return MHM_INVALID;
        now=value;
        for(auto it=sources.begin();it!=sources.end();) {
            if(it->second.deadline<=now) {const auto id=it->first;it=sources.erase(it);++revision;Event(MHM_SOURCE_EXPIRED,id);}else ++it;
        }
        return MHM_OK;
    }
    MhmResult Open(const char* name,uint32_t priority,uint32_t lease,uint64_t& id) {
        if(!Identifier(name,64)||priority>255||lease<100||lease>60000)return MHM_INVALID;
        std::lock_guard lock(gate);
        if(sources.size()>=limits.sources||nextSource==UINT64_MAX)return MHM_CAPACITY;
        if(now>UINT64_MAX-lease)return MHM_INVALID;
        Source s;s.id=nextSource+1;s.priority=priority;s.lease=lease;s.deadline=now+lease;
        sources.emplace(s.id,s);nextSource=s.id;id=s.id;return MHM_OK;
    }
    MhmResult Close(uint64_t id) {
        std::lock_guard lock(gate);if(!sources.erase(id))return MHM_STALE;++revision;return MHM_OK;
    }
    MhmResult Heartbeat(uint64_t id) {
        std::lock_guard lock(gate);const auto it=sources.find(id);if(it==sources.end())return MHM_STALE;
        if(now>UINT64_MAX-it->second.lease)return MHM_INVALID;
        it->second.deadline=now+it->second.lease;return MHM_OK;
    }
    MhmResult Submit(uint64_t id,std::span<const MhmUpdate> batch,uint64_t& receipt) {
        if(batch.size()>limits.batch_updates)return MHM_CAPACITY;
        std::lock_guard lock(gate);const auto it=sources.find(id);if(it==sources.end())return MHM_STALE;
        if(now>UINT64_MAX-it->second.lease)return MHM_INVALID;
        std::map<std::pair<uint64_t,uint32_t>,bool> seen;
        for(const auto& v:batch) {
            const auto target=targets.find(v.target);if(target==targets.end())return MHM_STALE;
            if(v.key>=target->second.asset->keys.size()||!std::isfinite(v.weight)||v.weight<0||v.weight>1)return MHM_INVALID;
            if(!seen.emplace(std::pair{v.target,v.key},true).second)return MHM_INVALID;
        }
        auto candidate=sources;
        candidate.at(id).values.assign(batch.begin(),batch.end());
        if(Conflicts(candidate))return MHM_CONFLICT;
        sources.swap(candidate);sources.at(id).deadline=now+sources.at(id).lease;
        receipt=++revision;return MHM_OK;
    }
    MhmResult TargetAt(uint32_t index,MhmTargetInfo& out) const {
        std::lock_guard lock(gate);if(index>=targets.size())return MHM_NOT_READY;
        auto it=targets.begin();std::advance(it,index);MhmTargetInfo t{};
        t.handle=it->first;t.part=it->second.asset->part;t.key_count=static_cast<uint32_t>(it->second.asset->keys.size());
        std::memcpy(t.model_id,it->second.asset->model.c_str(),it->second.asset->model.size()+1);out=t;return MHM_OK;
    }
    MhmResult KeyAt(uint64_t handle,uint32_t index,MhmKeyInfo& out) const {
        std::lock_guard lock(gate);auto it=targets.find(handle);if(it==targets.end())return MHM_STALE;
        if(index>=it->second.asset->keys.size())return MHM_INVALID;out=it->second.asset->keys[index].info;return MHM_OK;
    }
    MhmResult Identity(uint64_t handle,MhmAssetIdentity& out) const {
        std::lock_guard lock(gate);const auto it=targets.find(handle);if(it==targets.end())return MHM_STALE;
        const auto& a=*it->second.asset;
        const auto digest=[](const std::string& text){return text.size()==64&&std::all_of(text.begin(),text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});};
        if(!digest(a.modelSha)||!digest(a.packSha))return MHM_NOT_READY;
        MhmAssetIdentity value{};value.handle=handle;std::memcpy(value.model_sha256,a.modelSha.c_str(),65);std::memcpy(value.pack_sha256,a.packSha.c_str(),65);
        out=value;return MHM_OK;
    }
    MhmResult Read(uint64_t handle,std::span<MhmWeightState> out,uint32_t& needed,uint64_t& rev,uint64_t& applied) const {
        std::lock_guard lock(gate);auto it=targets.find(handle);if(it==targets.end())return MHM_STALE;
        needed=static_cast<uint32_t>(it->second.asset->keys.size());
        if(out.size()<needed)return MHM_BUFFER_SMALL;
        auto values=Effective(it->second);std::copy(values.begin(),values.end(),out.begin());
        rev=revision;applied=it->second.appliedRevision;return MHM_OK;
    }
    MhmResult PollEvent(uint64_t cursor,MhmEvent& out) const {
        std::lock_guard lock(gate);if(events.empty()||cursor>=nextEvent)return MHM_NOT_READY;
        if(cursor!=0&&cursor<events.front().sequence-1){out=events.front();return MHM_EVENTS_LOST;}
        for(const auto& e:events)if(e.sequence>cursor){out=e;return MHM_OK;}return MHM_NOT_READY;
    }
    MhmResult Capture(uint64_t& id,uint64_t& rev) {
        std::lock_guard lock(gate);if(targets.empty())return MHM_NOT_READY;
        if(frames.size()>=4||nextFrame==UINT64_MAX)return MHM_CAPACITY;
        auto f=std::make_shared<Frame>();f->id=nextFrame+1;f->revision=revision;
        for(const auto& [handle,t]:targets) {
            FrameTarget ft;ft.handle=handle;ft.asset=t.asset;
            for(const auto& value:Effective(t))ft.weights.push_back(value.requested);
            f->targets.push_back(std::move(ft));
        }
        frames.emplace(f->id,f);nextFrame=f->id;id=f->id;rev=f->revision;return MHM_OK;
    }
    MhmResult ComposeFrame(uint64_t id,uint64_t target,std::span<unsigned char> output) const {
        std::shared_ptr<const Frame> f;
        {std::lock_guard lock(gate);const auto it=frames.find(id);if(it==frames.end())return MHM_STALE;f=it->second;
         if(!FrameLive(*f))return MHM_STALE;}
        const auto it=std::find_if(f->targets.begin(),f->targets.end(),[&](const auto& t){return t.handle==target;});
        if(it==f->targets.end())return MHM_STALE;
        if(output.size()!=it->asset->basis.size())return MHM_BUFFER_SMALL;
        std::vector<unsigned char> work;if(!Compose(*it->asset,it->weights,work))return MHM_INVALID;
        // Lifecycle may change during CPU composition. Never acknowledge or hand
        // out the result of an instance removed while the lock was released.
        {std::lock_guard lock(gate);if(!FrameLive(*f)||!frames.contains(id))return MHM_STALE;
         std::copy(work.begin(),work.end(),output.begin());}
        return MHM_OK;
    }
    MhmResult CaptureInterpolated(uint64_t target,std::span<const float> weights,uint64_t expected,uint64_t& id) {
        for(float w:weights)if(!std::isfinite(w)||w<0||w>1)return MHM_INVALID;
        std::lock_guard lock(gate);
        const auto it=targets.find(target);if(it==targets.end()||expected!=revision)return MHM_STALE;
        if(weights.size()!=it->second.asset->keys.size())return MHM_INVALID;
        if(frames.size()>=4||nextFrame==UINT64_MAX)return MHM_CAPACITY;
        auto f=std::make_shared<Frame>();f->id=nextFrame+1;f->revision=revision;
        FrameTarget ft;ft.handle=target;ft.asset=it->second.asset;ft.weights.assign(weights.begin(),weights.end());
        f->targets.push_back(std::move(ft));frames.emplace(f->id,f);nextFrame=f->id;id=f->id;return MHM_OK;
    }
    MhmResult CaptureInterpolatedGroup(std::span<const MhmUpdate> values,uint64_t expected,uint64_t& id) {
        if(values.size()>limits.batch_updates)return MHM_CAPACITY;
        std::lock_guard lock(gate);
        if(expected!=revision)return MHM_STALE;
        if(targets.empty())return MHM_NOT_READY;
        size_t count=0;
        for(const auto& [handle,t]:targets){(void)handle;count+=t.asset->keys.size();}
        if(values.size()!=count)return MHM_INVALID;
        std::map<std::pair<uint64_t,uint32_t>,float> complete;
        for(const auto& v:values) {
            const auto it=targets.find(v.target);
            if(it==targets.end())return MHM_STALE;
            if(v.key>=it->second.asset->keys.size()||!std::isfinite(v.weight)||v.weight<0||v.weight>1||
               !complete.emplace(std::pair{v.target,v.key},v.weight).second)return MHM_INVALID;
        }
        if(frames.size()>=4||nextFrame==UINT64_MAX)return MHM_CAPACITY;
        auto f=std::make_shared<Frame>();f->id=nextFrame+1;f->revision=revision;
        for(const auto& [handle,t]:targets) {
            FrameTarget ft;ft.handle=handle;ft.asset=t.asset;ft.weights.reserve(t.asset->keys.size());
            for(uint32_t k=0;k<t.asset->keys.size();k++)ft.weights.push_back(complete.at({handle,k}));
            f->targets.push_back(std::move(ft));
        }
        frames.emplace(f->id,f);nextFrame=f->id;id=f->id;return MHM_OK;
    }
    MhmResult Acknowledge(uint64_t id) {
        std::lock_guard lock(gate);auto it=frames.find(id);if(it==frames.end()||!FrameLive(*it->second))return MHM_STALE;
        const auto& f=*it->second;
        for(const auto& t:f.targets)if(f.revision<targets.at(t.handle).appliedRevision||f.id<targets.at(t.handle).appliedFrame)return MHM_STALE;
        for(const auto& t:f.targets){auto& live=targets.at(t.handle);live.applied=t.weights;live.appliedRevision=f.revision;live.appliedFrame=f.id;}
        return MHM_OK;
    }
    MhmResult ReleaseFrame(uint64_t id){std::lock_guard lock(gate);return frames.erase(id)?MHM_OK:MHM_STALE;}
};
}
