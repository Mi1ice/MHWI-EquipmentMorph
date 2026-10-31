#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include "C07Benchmark.hpp"
#include <numeric>

namespace {
uint64_t Clock(){LARGE_INTEGER q{};if(!QueryPerformanceCounter(&q)||q.QuadPart<=0)throw std::runtime_error("benchmark QPC");return uint64_t(q.QuadPart);}
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
double Percentile(std::vector<uint64_t> x,double p,double frequency){std::sort(x.begin(),x.end());const auto at=(x.size()-1)*p;const auto i=size_t(at);return (x[i]+(double(x[std::min(i+1,x.size()-1)])-x[i])*(at-i))*1000/frequency;}
uint64_t PrivateBytes(){PROCESS_MEMORY_COUNTERS_EX c{};c.cb=sizeof(c);return K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c))?uint64_t(c.PrivateUsage):0;}
}
int wmain(int argc,wchar_t** argv){try{
    Require(argc==4,"usage: benchmark PACK KEY_COUNT OUTPUT_DIRECTORY");
    const auto path=std::filesystem::path(argv[1]);const auto count=unsigned(std::stoul(argv[2]));const auto folder=std::filesystem::path(argv[3]);
    Require(!std::filesystem::exists(folder),"output exists, preserve prior attempt");std::filesystem::create_directories(folder);
    LARGE_INTEGER f{};Require(QueryPerformanceFrequency(&f)&&f.QuadPart>0,"QPC frequency");const auto frequency=uint64_t(f.QuadPart);
    std::ifstream file(path,std::ios::binary|std::ios::ate);Require(bool(file),"pack open");const auto size=file.tellg();Require(size>0&&size<=128*1024*1024,"pack bound");std::vector<unsigned char> raw(size_t(size),0);file.seekg(0);Require(bool(file.read(reinterpret_cast<char*>(raw.data()),size)),"pack read");
    const auto sha=mhm::pack::Digest(raw);const auto baselinePrivate=PrivateBytes();
    const auto decodeStart=Clock();auto original=mhm::pack::Decode(raw);const auto decodeEnd=Clock();
    const auto originalKeyCount=original.keys.size();
    Require(count>=original.keys.size()&&count<=1024,"key count bound");
    const auto constructStart=Clock();auto asset=count==original.keys.size()?std::move(original):c07bench::Expand(original,count);const auto constructEnd=Clock();
    const auto logical=c07bench::Logical(asset);Require(logical<256*1024*1024,"offline memory guard");
    std::ofstream report(folder/L"result.json");report<<"{\"schema\":\"mhwi-c07-offline-v1\",\"pack_sha256\":\""<<sha<<"\",\"source_pack_bytes\":"<<raw.size()<<",\"source_key_count\":"<<originalKeyCount<<",\"keys\":"<<asset.keys.size()<<",\"synthetic\":"<<(count>originalKeyCount?"true":"false")<<",\"fixture_method\":\"repeat canonical real-scale records with unique appended IDs\",\"basis_bytes\":"<<asset.basis.size()<<",\"logical_decoded_asset_bytes\":"<<logical<<",\"decode_ms\":"<<1000.*(decodeEnd-decodeStart)/frequency<<",\"construct_ms\":"<<1000.*(constructEnd-constructStart)/frequency<<",\"process_private_before_decode\":"<<baselinePrivate<<",\"process_private_after_construct\":"<<PrivateBytes()<<",\"qpc_frequency\":"<<frequency<<",\"record_size_MhmDelta\":"<<sizeof(MhmDelta)<<",\"position_records\":";
    uint64_t positions=0,directions=0;for(const auto& k:asset.keys){positions+=k.deltas.size();directions+=k.normals.size();}
    report<<positions<<",\"direction_records\":"<<directions<<",\"rotation_records\":"<<asset.rotations.size()<<",\"rounds\":[\n";
    bool first=true;unsigned caseIndex=0;
    for(unsigned mode=0;mode<4;mode++)for(unsigned round=0;round<3;round++){
        const auto active=mode==0?0u:mode==1?6u:std::min(30u,count);const bool dynamic=mode==3;
        std::array<std::vector<float>,60> sequence;for(unsigned i=0;i<60;i++)sequence[i]=c07bench::Weights(asset,active,dynamic,i);
        uint64_t activePositions=0,activeDirections=0;for(unsigned i=0;i<count;i++)if(sequence[0][i]){activePositions+=asset.keys[i].deltas.size();activeDirections+=asset.keys[i].normals.size();}
        std::vector<unsigned char> out;const auto begin=Clock();Require(mhm::Compose(asset,sequence[0],out),"first composition failed");const auto firstEnd=Clock();
        if(!active)Require(out==asset.basis,"zero basis mismatch");
        unsigned step=0;const auto warmEnd=Clock()+frequency;
        do{Require(mhm::Compose(asset,sequence[step++%60],out),"warm composition");}while(Clock()<warmEnd);
        std::vector<uint64_t> samples;samples.reserve(2097152);const auto start=Clock();const auto deadline=start+frequency*2;
        do{const auto a=Clock();Require(mhm::Compose(asset,sequence[step++%60],out),"steady composition");const auto b=Clock();Require(b>=a,"QPC backwards");Require(samples.size()<2097152,"raw sample limit reached");samples.push_back(b-a);}while(Clock()<deadline);
        const auto end=Clock();const auto sum=std::accumulate(samples.begin(),samples.end(),uint64_t(0));const auto rawName="samples-"+std::to_string(caseIndex++)+".qpc-u64le";
        std::ofstream sampleFile(folder/rawName,std::ios::binary);sampleFile.write(reinterpret_cast<const char*>(samples.data()),std::streamsize(samples.size()*8));Require(bool(sampleFile),"raw sample write");
        const auto outputSha=mhm::pack::Digest(out);
        if(!first)report<<",\n";first=false;
        report<<"{\"active_keys\":"<<active<<",\"varying_keys\":"<<(dynamic?std::min(active,5u):0)<<",\"active_position_records\":"<<activePositions<<",\"active_direction_records\":"<<activeDirections<<",\"round\":"<<round<<",\"first_ms\":"<<1000.*(firstEnd-begin)/frequency<<",\"calls\":"<<samples.size()<<",\"elapsed_ms\":"<<1000.*(end-start)/frequency<<",\"sum_qpc\":"<<sum<<",\"mean_ms\":"<<1000.*sum/frequency/samples.size()<<",\"p95_ms\":"<<Percentile(samples,.95,double(frequency))<<",\"max_ms\":"<<1000.*(*std::max_element(samples.begin(),samples.end()))/frequency<<",\"raw_samples\":\""<<rawName<<"\",\"output_sha256\":\""<<outputSha<<"\"}";
        Require(bool(report),"report write");
    }
    report<<"\n],\"game_tested\":false,\"memory_scope\":\"logical decoded object/records only, excludes allocator overhead, compose temporary map nodes and GPU\",\"timing_scope\":\"production mhm::Compose QPC elapsed including allocation; no host wrapper or GPU\"}\n";
    Require(bool(report),"report final write");printf("PASS offline benchmark %u keys\n",count);return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}}
