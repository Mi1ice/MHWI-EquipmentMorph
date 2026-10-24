// Capacity boundaries, wire decoding and whole-source atomic replacement.
#include "../runtime/EquipmentMorphPerKeyCore/asset_pack.hpp"
#include <iostream>
#include <functional>

namespace {
unsigned checks=0, failures=0;
void Check(bool ok,const std::string& name){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
template<class F> void Reject(F&& action,const char* name){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Check(rejected,name);}
std::vector<unsigned char> Bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("missing fixture");return {std::istreambuf_iterator<char>(file),{}};}
}

// Synthetic fixture: unique public IDs, finite basis and a valid sparse edit.
mhm::Asset MakeCapacityAsset(uint32_t part,uint32_t count){
    mhm::Asset asset;asset.part=part;asset.model="pl/synthetic/body";asset.basis.resize(24);
    const float basis[]{1,2,3};std::memcpy(asset.basis.data(),basis,12);asset.basis[12]=127;
    for(uint32_t i=0;i<count;++i){mhm::Key key;const auto id="Key_"+std::to_string(i);
        std::memcpy(key.info.id,id.c_str(),id.size()+1);key.info.maximum=1;key.deltas.push_back({0,.25f,.5f,.75f});asset.keys.push_back(std::move(key));}
    return asset;
}

void TestPackageCounts(const std::filesystem::path& folder){
    for(unsigned count:{0u,1u,64u,65u,489u,512u,1024u,1025u})for(const char* format:{"mhm1","mhm2"}){
        const auto bytes=Bytes(folder/("count-"+std::to_string(count)+"."+format));
        bool accepted=false;try{const auto asset=mhm::pack::Decode(bytes);accepted=asset.keys.size()==count;}catch(const std::exception&){}
        Check(accepted==(count>0&&count<=1024),"package "+std::to_string(count)+" "+format);
    }
    for(const char* format:{"mhm1","mhm2"}){
        auto bytes=Bytes(folder/(std::string("count-1.")+format));
        auto bad=bytes;bad.pop_back();Reject([&]{mhm::pack::Decode(bad);},"truncated payload");
        bad=bytes;bad.push_back(0);Reject([&]{mhm::pack::Decode(bad);},"trailing payload");
        bad=bytes;bad.resize(20);Reject([&]{mhm::pack::Decode(bad);},"truncated header");
        bad=bytes;const uint32_t huge=UINT32_MAX;std::memcpy(bad.data()+16,&huge,4);Reject([&]{mhm::pack::Decode(bad);},"huge basis declaration");
        bad=bytes;const uint32_t modelBytes=17;const size_t keyAt=32+modelBytes+24;
        const uint32_t tooMany=1000001;std::memcpy(bad.data()+keyAt+20,&tooMany,4);Reject([&]{mhm::pack::Decode(bad);},"position record bound");
        if(std::string(format)=="mhm2"){bad=bytes;std::memcpy(bad.data()+keyAt+24,&tooMany,4);Reject([&]{mhm::pack::Decode(bad);},"normal record declaration bound");}
        bad=bytes;const size_t deltaAt=keyAt+(std::string(format)=="mhm2"?28:24)+8;
        std::memcpy(bad.data()+deltaAt,&huge,4);Reject([&]{mhm::pack::Decode(bad);},"offset addition cannot wrap");
        Reject([&]{mhm::pack::Load(folder/(std::string("count-1.")+format),std::string(64,'0'));},"wrong pack digest");
    }
    auto bad=MakeCapacityAsset(1,1);bad.keys[0].deltas[0].offset=UINT32_MAX;Check(!mhm::Validate(bad),"invalid offset");
    bad=MakeCapacityAsset(1,2);bad.keys[1].info=bad.keys[0].info;Check(!mhm::Validate(bad),"duplicate public ID");
    bad=MakeCapacityAsset(1,1);bad.keys[0].deltas[0].x=std::numeric_limits<float>::infinity();Check(!mhm::Validate(bad),"nonfinite delta");
    // Total normal record guard is independent of the larger key count.
    bad=MakeCapacityAsset(1,2);bad.perKeyNormals=true;
    bad.basis.resize(24+500001*8);
    for(uint32_t i=0;i<500001;++i)bad.basis[24+i*8]=127;
    for(auto& key:bad.keys){key.info.flags=MHM_PER_KEY_SHADING;for(uint32_t i=0;i<500001;++i)key.normals.push_back({24+i*8,0,0,0});}
    Check(!mhm::Validate(bad),"normal record bound");
}

void TestAtomicFullBatch(){
    mhm::Runtime runtime;std::array<uint64_t,5> handles{};bool attached=true;
    for(uint32_t part=0;part<5;++part)attached&=runtime.Attach(MakeCapacityAsset(part,1024),handles[part])==MHM_OK;
    Check(attached,"five 1024-key assets attach");if(!attached)return;
    uint64_t source=0,receipt=0;Check(runtime.Open("capacity",10,1000,source)==MHM_OK,"source open");
    std::vector<MhmUpdate> batch;for(const auto handle:handles)for(uint32_t key=0;key<1024;++key)batch.push_back({handle,key,.75f});
    Check(runtime.Submit(source,batch,receipt)==MHM_OK,"5120 atomic batch accepted");
    uint64_t revision=0,applied=0;uint32_t needed=0;std::vector<MhmWeightState> weights(1024);
    for(const auto handle:handles){Check(runtime.Read(handle,weights,needed,revision,applied)==MHM_OK&&needed==1024&&weights.back().requested==.75f,"last key visible");}
    const auto before=revision;
    auto unchanged=[&](const std::vector<MhmUpdate>& values,MhmResult expected,const char* name){
        uint64_t rejectedReceipt=0x1234;Check(runtime.Submit(source,values,rejectedReceipt)==expected,name);
        Check(rejectedReceipt==0x1234,"failed receipt untouched");
        for(const auto handle:handles){Check(runtime.Read(handle,weights,needed,revision,applied)==MHM_OK&&revision==before&&std::all_of(weights.begin(),weights.end(),[&](const auto& w){return w.requested==.75f&&w.source==source;}),"rejected batch leaves contribution and revision");}
    };
    auto bad=batch;bad.push_back(batch.back());unchanged(bad,MHM_CAPACITY,"5121 rejected");
    bad=batch;bad.back()=bad.front();unchanged(bad,MHM_INVALID,"duplicate rejected atomically");
    bad=batch;bad.back().key=1024;unchanged(bad,MHM_INVALID,"invalid index rejected atomically");
    bad=batch;bad.back().weight=std::numeric_limits<float>::quiet_NaN();unchanged(bad,MHM_INVALID,"NaN rejected atomically");
    bad=batch;bad.back().weight=1.01f;unchanged(bad,MHM_INVALID,"out-of-range weight rejected atomically");
    bad=batch;bad.back().target=UINT64_MAX;unchanged(bad,MHM_STALE,"stale update rejected atomically");
    Check(runtime.Detach(handles.back())==MHM_OK,"retire target");
    uint64_t newHandle=0;Check(runtime.Attach(MakeCapacityAsset(4,1024),newHandle)==MHM_OK&&newHandle!=handles.back(),"new target generation");
    Check(runtime.Submit(source,batch,receipt)==MHM_STALE,"retired batch rejected");
    Check(runtime.Read(newHandle,weights,needed,revision,applied)==MHM_OK&&weights.back().requested==0,"new target inherits no contribution");
    // Verify API host intake and API-declared bounds, rather than Runtime alone.
    MhmApi api{};Check(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK,"C API available");
    MhmLimits limits{};Check(api.limits(&limits)==MHM_OK&&limits.keys_per_target==1024&&limits.batch_updates==5120,"C API declares trial limits");
    for(uint32_t count:{0u,1u,64u,65u,489u,512u,1024u,1025u}){
        const auto asset=MakeCapacityAsset(1,count);std::vector<MhmHostKey> keys;
        for(const auto& key:asset.keys)keys.push_back({key.info,key.deltas.data(),static_cast<uint32_t>(key.deltas.size())});
        const MhmHostTarget descriptor{1,static_cast<uint32_t>(asset.basis.size()),asset.model.c_str(),asset.basis.data(),keys.data(),count,0,nullptr};
        uint64_t handle=0;const auto status=MhwMorphHostAttach(&descriptor,&handle);
        Check(status==(count>0&&count<=1024?MHM_OK:MHM_INVALID),"host C API intake boundary");
        if(status==MHM_OK)Check(MhwMorphHostDetach(handle)==MHM_OK,"C API intake cleanup");
    }
}

int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{TestPackageCounts(std::filesystem::path(argv[1]));TestAtomicFullBatch();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
    std::cout<<"Native capacity: "<<(checks-failures)<<'/'<<checks<<'\n';return failures?1:0;
}
