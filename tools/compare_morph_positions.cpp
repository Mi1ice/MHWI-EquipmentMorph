// Offline validator: production wire decoder and Compose, streamed float64 references.
#include "../runtime/EquipmentMorphPerKeyCore/asset_pack.hpp"
#include "../runtime/EquipmentMorphPerKeyCore/json_log.hpp"
#include "../runtime/EquipmentMorphDiskConfiguration/loader.hpp"
#include "morph_reference_json.hpp"
#include <iomanip>
#include <iostream>
#include <set>

namespace {
using J=morphreference::json::Value;
using morphreference::json::Field;
const std::string& Text(const J& v,const char* name){return Field(v,name,J::String).text;}
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint64_t Unsigned(const J& v,uint64_t maximum){
    Require(v.kind==J::Number&&v.number>=0&&v.number<=double(maximum)&&v.number<9007199254740992.0&&std::floor(v.number)==v.number,"Invalid integer/offset");return static_cast<uint64_t>(v.number);
}
std::filesystem::path Local(const std::filesystem::path& folder,const std::string& name){
    Require(!name.empty()&&name.find_first_of("/\\:")==name.npos&&name!="."&&name!="..","Reference filename must be local");return folder/std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
}
std::vector<unsigned char> Read(const std::filesystem::path& path){
    const auto size=std::filesystem::file_size(path);Require(size>0&&size<=128*1024*1024,"Input file bound");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));std::ifstream input(path,std::ios::binary);
    Require(bool(input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size))),"Input read");return bytes;
}
std::string DigestFile(const std::filesystem::path& path){
    struct Hash {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} h;
    Require(BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(h.algorithm,&h.hash,nullptr,0,nullptr,0,0)>=0,"SHA256 initialization");
    std::ifstream input(path,std::ios::binary);Require(bool(input),"Digest file missing");std::array<unsigned char,64*1024> block{};
    while(input){input.read(reinterpret_cast<char*>(block.data()),block.size());const auto count=input.gcount();if(count)Require(BCryptHashData(h.hash,block.data(),static_cast<ULONG>(count),0)>=0,"SHA256 update");}
    Require(input.eof(),"Digest read failure");std::array<unsigned char,32> raw{};Require(BCryptFinishHash(h.hash,raw.data(),static_cast<ULONG>(raw.size()),0)>=0,"SHA256 final");
    constexpr char hex[]="0123456789abcdef";std::string result;for(auto b:raw){result+=hex[b>>4];result+=hex[b&15];}return result;
}
template<class T>T At(const std::vector<unsigned char>& bytes,uint64_t at){Require(at<=bytes.size()&&sizeof(T)<=bytes.size()-at,"Model field out of bounds");T value{};std::memcpy(&value,bytes.data()+at,sizeof(T));return value;}
std::vector<uint32_t> ModelOffsets(const std::vector<unsigned char>& model,const mhm::Asset& asset){
    Require(model.size()>=96&&std::memcmp(model.data(),"MOD\0",4)==0&&At<uint16_t>(model,4)==237,"Model header");
    const auto start=At<uint64_t>(model,80);const auto bytes=At<uint32_t>(model,24);
    Require(bytes==asset.basis.size()&&start<=model.size()&&bytes<=model.size()-start,"Model basis bounds");
    Require(std::equal(asset.basis.begin(),asset.basis.end(),model.begin()+static_cast<size_t>(start)),"Model and pack basis mismatch");
    const auto meshes=At<uint16_t>(model,8);const auto table=At<uint64_t>(model,72);std::vector<uint32_t> positions;
    Require(table<=model.size()&&uint64_t(meshes)*80<=model.size()-table,"Model mesh table bounds");
    for(uint32_t mesh=0;mesh<meshes;++mesh){
        const auto row=table+uint64_t(mesh)*80;const auto count=At<uint16_t>(model,row+2);const auto stride=At<unsigned char>(model,row+14);
        const uint64_t offset=At<uint32_t>(model,row+20)+uint64_t(stride)*(uint64_t(At<uint32_t>(model,row+16))+At<uint32_t>(model,row+36));
        Require(stride>=24&&offset<=asset.basis.size()&&uint64_t(count)*stride<=asset.basis.size()-offset,"Model vertex bounds");
        for(uint32_t i=0;i<count;++i)positions.push_back(static_cast<uint32_t>(offset+uint64_t(i)*stride));
    }
    std::sort(positions.begin(),positions.end());Require(!positions.empty()&&std::adjacent_find(positions.begin(),positions.end())==positions.end(),"Overlapping model vertices");return positions;
}
struct Stats {
    std::vector<double> errors;double sum=0,squares=0,max=0;size_t worst=0;
    void Add(double error){Require(std::isfinite(error),"Nonfinite error");if(error>max){max=error;worst=errors.size();}errors.push_back(error);sum+=error;squares+=error*error;}
    void Write(std::ostream& out,const std::vector<uint32_t>& offsets){
        auto sorted=errors;std::sort(sorted.begin(),sorted.end());Require(!sorted.empty(),"Empty measurement");
        const auto quantile=[&](double p){return sorted[static_cast<size_t>(p*double(sorted.size()-1))];};
        out<<"{\"max\":"<<max<<",\"mean\":"<<sum/double(errors.size())<<",\"rms\":"<<std::sqrt(squares/double(errors.size()))
           <<",\"p50\":"<<quantile(.5)<<",\"p95\":"<<quantile(.95)<<",\"p99\":"<<quantile(.99)<<",\"worst_serialized_vertex\":"<<worst<<",\"worst_position_offset\":"<<offsets[worst]<<'}';
    }
};
std::vector<double> Positions(std::ifstream& input,uint64_t offset,size_t vertices,uint64_t fileSize){
    const uint64_t bytes=uint64_t(vertices)*24;
    Require(offset<=fileSize&&bytes<=fileSize-offset&&offset<=uint64_t(INT64_MAX),"Reference segment outside file");
    std::vector<double> values(vertices*3);input.clear();input.seekg(static_cast<std::streamoff>(offset));
    Require(bool(input.read(reinterpret_cast<char*>(values.data()),static_cast<std::streamsize>(bytes))),"Truncated reference segment");
    for(const auto v:values)Require(std::isfinite(v),"Nonfinite reference position");return values;
}

void Compare(const std::filesystem::path& packPath,const std::filesystem::path& referencePath,const std::filesystem::path& output){
    const auto raw=Read(referencePath);const std::string text(raw.begin(),raw.end());const auto ref=morphreference::json::Reader(text).Parse();
    Require(Text(ref,"schema")=="mhwi-morph-position-reference-v1","Reference version");const auto folder=referencePath.parent_path();
    const auto asset=mhm::pack::Load(packPath,Text(ref,"pack_sha256"));Require(asset.model==Text(ref,"model_id"),"Model ID mismatch");
    const auto model=Read(Local(folder,Text(ref,"model_file")));Require(mhm::pack::Digest(model)==Text(ref,"model_sha256"),"Model digest mismatch");
    const auto profileBytes=Read(Local(folder,Text(ref,"profile_file")));Require(mhm::pack::Digest(profileBytes)==Text(ref,"profile_sha256"),"Profile digest mismatch");
    auto profile=mhm::configuration::DecodeProfile(profileBytes);Require(profile.packSha==Text(ref,"pack_sha256"),"Profile pack digest mismatch");mhm::configuration::ValidateModel(profile,asset,model);
    Require(DigestFile(Local(folder,Text(ref,"trace_file")))==Text(ref,"trace_sha256"),"Trace digest mismatch");
    const auto offsets=ModelOffsets(model,asset);const auto& declared=Field(ref,"position_offsets",J::Array).array;
    Require(declared.size()==offsets.size(),"Reference position coverage");for(size_t i=0;i<offsets.size();++i)Require(Unsigned(declared[i],UINT32_MAX)==offsets[i],"Reference offsets differ from serialized model");
    std::map<std::string,size_t> ids;const auto& mapping=Field(ref,"key_mapping",J::Array).array;Require(mapping.size()==asset.keys.size(),"Reference key coverage");
    std::vector<bool> allowed(asset.basis.size());for(const auto offset:offsets)for(unsigned i=0;i<12;++i)allowed[offset+i]=true;
    std::set<uint32_t> shadingFields;uint64_t deltaCount=0,normalCount=0;
    for(size_t i=0;i<asset.keys.size();++i){
        const auto& key=asset.keys[i];Require(Text(mapping[i],"id")==key.info.id&&!Text(mapping[i],"source_key").empty()&&ids.emplace(key.info.id,i).second,"Public ID mapping mismatch");
        for(const auto& d:key.deltas){Require(std::binary_search(offsets.begin(),offsets.end(),d.offset),"Delta outside serialized position");++deltaCount;}
        for(const auto& n:key.normals){Require(n.offset>=12&&std::binary_search(offsets.begin(),offsets.end(),n.offset-12),"Normal outside serialized field");shadingFields.insert(n.offset);++normalCount;}
    }
    for(const auto& r:asset.rotations){Require(r.offset>=12&&std::binary_search(offsets.begin(),offsets.end(),r.offset-12),"Rotation outside serialized shading");shadingFields.insert(r.offset);}
    for(const auto offset:shadingFields)for(unsigned i:{0u,1u,2u,4u,5u,6u})allowed[offset+i]=true;
    const auto positionPath=Local(folder,Text(ref,"positions_file"));Require(DigestFile(positionPath)==Text(ref,"positions_sha256"),"Position digest mismatch");
    const auto fileSize=std::filesystem::file_size(positionPath);std::ifstream input(positionPath,std::ios::binary);
    const auto& cases=Field(ref,"cases",J::Array).array;Require(!cases.empty()&&cases.size()<=asset.keys.size()*5+4096,"Reference case bound");
    const uint64_t segment=uint64_t(offsets.size())*24;Require(segment&&cases.size()<=UINT64_MAX/segment&&fileSize==cases.size()*segment,"Reference segment count/length");
    std::vector<std::vector<float>> weights;std::vector<uint64_t> segments;std::set<std::string> names;std::vector<std::set<double>> coverage(asset.keys.size());
    for(size_t i=0;i<cases.size();++i){
        const auto& item=cases[i];Require(names.insert(Text(item,"id")).second,"Duplicate case ID");
        const auto offset=Unsigned(Field(item,"byte_offset",J::Number),fileSize);Require(offset==i*segment,"Reference segment order/overlap");segments.push_back(offset);
        std::vector<float> values(asset.keys.size());const auto& sparse=Field(item,"weights",J::Object).object;
        for(const auto& [name,weight]:sparse){const auto it=ids.find(name);Require(it!=ids.end()&&weight.kind==J::Number&&weight.number>=0&&weight.number<=1,"Invalid sparse reference weight");values[it->second]=static_cast<float>(weight.number);}
        if(i==0)Require(sparse.empty()&&Text(item,"kind")=="baseline","Missing zero baseline");
        if(Text(item,"kind")=="single"){
            const auto it=ids.find(Text(item,"key_id"));Require(it!=ids.end()&&sparse.size()==1&&sparse.contains(it->first),"Single case key mismatch");coverage[it->second].insert(sparse.at(it->first).number);
        }weights.push_back(std::move(values));
    }
    for(const auto& samples:coverage)Require(samples==std::set<double>{0,.25,.5,.75,1},"Incomplete single-key sweep");
    const auto baseline=Positions(input,segments[0],offsets.size(),fileSize);
    std::ostringstream report;report<<std::setprecision(17)<<"{\"schema\":\"mhwi-morph-position-comparison-v1\",\"status\":\"measurement_completed_threshold_not_set\",\"key_count\":"<<asset.keys.size()
        <<",\"position_count\":"<<offsets.size()<<",\"delta_records\":"<<deltaCount<<",\"normal_records\":"<<normalCount<<",\"pack_sha256\":"<<mhm::JsonString(Text(ref,"pack_sha256"))<<",\"cases\":[";
    double maxError=0,minNormalDot=1;bool zeroRestored=true;
    for(size_t c=0;c<cases.size();++c){
        const auto expected=Positions(input,segments[c],offsets.size(),fileSize);std::vector<unsigned char> composed;
        Require(mhm::Compose(asset,weights[c],composed),"Production Compose failed");
        const bool zero=std::all_of(weights[c].begin(),weights[c].end(),[](float v){return v==0;});if(zero){zeroRestored&=composed==asset.basis;Require(zeroRestored,"Zero did not restore basis bytes");}
        for(size_t b=0;b<composed.size();++b)Require(allowed[b]||composed[b]==asset.basis[b],"Unexpected non-position byte change");
        for(const auto offset:shadingFields){
            double dot=0,a2=0,b2=0,t2=0;for(unsigned k=0;k<3;++k){const auto a=static_cast<int8_t>(asset.basis[offset+k]),b=static_cast<int8_t>(composed[offset+k]),t=static_cast<int8_t>(composed[offset+4+k]);dot+=double(a)*b;a2+=double(a)*a;b2+=double(b)*b;t2+=double(t)*t;}
            Require(a2>0&&b2>0&&t2>0,"Invalid composed normal/tangent direction");minNormalDot=std::min(minNormalDot,dot/std::sqrt(a2*b2));
        }
        Stats errors;for(size_t i=0;i<offsets.size();++i){double square=0;for(unsigned k=0;k<3;++k){const auto actual=At<float>(composed,offsets[i]+k*4);const double d=double(actual)-expected[i*3+k];square+=d*d;}errors.Add(std::sqrt(square));}
        maxError=std::max(maxError,errors.max);if(c)report<<',';report<<"{\"id\":"<<mhm::JsonString(Text(cases[c],"id"))<<",\"composition_error\":";errors.Write(report,offsets);
        if(c==0){report<<",\"baseline_error\":";errors.Write(report,offsets);}
        if(Text(cases[c],"kind")=="single"&&Field(cases[c],"weights",J::Object).object.begin()->second.number==1){
            const auto& key=asset.keys[ids.at(Text(cases[c],"key_id"))];Stats deltaErrors;
            for(size_t i=0;i<offsets.size();++i){const auto found=std::lower_bound(key.deltas.begin(),key.deltas.end(),offsets[i],[](const auto& d,uint32_t offset){return d.offset<offset;});const double delta[3]={found!=key.deltas.end()&&found->offset==offsets[i]?found->x:0,found!=key.deltas.end()&&found->offset==offsets[i]?found->y:0,found!=key.deltas.end()&&found->offset==offsets[i]?found->z:0};double square=0;for(unsigned k=0;k<3;++k){const auto d=delta[k]-(expected[i*3+k]-baseline[i*3+k]);square+=d*d;}deltaErrors.Add(std::sqrt(square));}
            report<<",\"single_delta_error\":";deltaErrors.Write(report,offsets);
        }report<<'}';
    }
    report<<"],\"max_composition_error\":"<<maxError<<",\"normal_min_dot_to_basis\":"<<minNormalDot<<",\"zero_restored\":true,\"non_position_valid\":true,\"tolerance_set\":false,\"game_tested\":false}";
    Require(!std::filesystem::exists(output),"Refusing to overwrite comparison output");std::ofstream result(output,std::ios::binary);result<<report.str()<<'\n';Require(bool(result),"Report write");std::cout<<"Measured "<<asset.keys.size()<<" keys, "<<cases.size()<<" cases; max "<<std::setprecision(17)<<maxError<<" (threshold not set)\n";
}
}
int wmain(int argc,wchar_t** argv){try{
    Require(argc==7&&std::wstring_view(argv[1])==L"--pack"&&std::wstring_view(argv[3])==L"--reference"&&std::wstring_view(argv[5])==L"--output","Usage: --pack file --reference reference.json --output result.json");Compare(argv[2],argv[4],argv[6]);return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
