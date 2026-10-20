#pragma once
#include "../EquipmentMorphPerKeyCore/asset_pack.hpp"
#include <set>
#include <string_view>
#include <optional>
#include "mesh_contract.hpp"

// File access happens before any input hook is enabled. No package can supply
// addresses, owner routes or a normalization mask to the game adapter.
namespace mhm::configuration {
inline constexpr char adapterId[]="mhw-c2ebbbd2-equipment-v1";
inline constexpr char diskAdapterId[]="mhw-c2ebbbd2-equipment-v2";
inline constexpr std::array<std::string_view,5> partNames={"head","body","arms","waist","legs"};
inline std::string Fold(std::string value){for(char& c:value){if(c=='\\')c='/';if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');}return value;}
inline void ParseIni(std::string_view text){
    std::string clean;
    while(!text.empty()){
        const auto end=text.find('\n');auto raw=text.substr(0,end);text=end==text.npos?std::string_view{}:text.substr(end+1);
        std::string line;for(char c:raw)if(c!='\r'&&c!=' '&&c!='\t')line+=c;
        if(!line.empty()&&line.front()!=';'&&line.front()!='#')clean+=line+'\n';
    }
    if(clean!="[runtime]\nformat=2\n")throw std::runtime_error("Expected runtime format=2; model sidecars are discovered automatically");
}
inline std::vector<unsigned char> ReadFile(const std::filesystem::path& path,size_t bound){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Package/model file missing");
    const auto count=file.tellg();if(count<=0||static_cast<uint64_t>(count)>bound)throw std::runtime_error("Package/model file size");
    std::vector<unsigned char> raw(static_cast<size_t>(count));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(raw.data()),static_cast<std::streamsize>(raw.size())))throw std::runtime_error("Package/model file read");return raw;
}
inline std::string Utf8(pack::Reader& input,size_t bound){
    const auto count=input.U32();if(!count||count>bound)throw std::runtime_error("Profile text bound");
    const auto raw=input.Take(count);std::string value(raw.begin(),raw.end());
    if(value.find('\0')!=value.npos||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0))throw std::runtime_error("Profile UTF8");return value;
}
inline std::string Sha(pack::Reader& input){
    auto value=input.Identifier(65);if(value.size()!=64||!std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::runtime_error("Profile digest");return value;
}
inline bool ModelId(std::string_view value){
    if(!value.starts_with("pl/")||value.size()>=80)return false;
    while(!value.empty()){const auto end=value.find('/');const auto item=value.substr(0,end);if(item.empty()||item=="."||item=="..")return false;
        for(char c:item)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
        value=end==value.npos?std::string_view{}:value.substr(end+1);if(end!=std::string_view::npos&&value.empty())return false;
    }return true;
}
struct SourceValue {std::string name;float weight=0;uint32_t flags=0;};
struct SourceObject {std::string name,fingerprint;std::vector<SourceValue> values;};
struct Profile {
    uint32_t part=0,vertexBytes=0,meshCount=0,modelBytes=0;
    std::string model,modelSha,packSha,meshSha,captureSha;
    std::vector<SourceObject> objects;
    bool diskContract=false;std::vector<uint32_t> diskFormats;
};
inline Profile DecodeProfile(std::span<const unsigned char> raw){
    if(raw.size()>512*1024)throw std::runtime_error("Profile size");
    pack::Reader in(raw);if(std::memcmp(in.Take(8).data(),"MHMPROF1",8)||in.U32()!=1)throw std::runtime_error("Profile version");
    const auto adapter=in.Identifier(64);
    if(adapter!=diskAdapterId)throw std::runtime_error("Profile game adapter");
    Profile p;p.diskContract=true;p.part=in.U32();p.vertexBytes=in.U32();p.meshCount=in.U32();p.modelBytes=in.U32();
    if(p.part>MHM_LEGS||!p.vertexBytes||p.vertexBytes>64*1024*1024||!p.meshCount||p.meshCount>4096||!p.modelBytes||p.modelBytes>128*1024*1024)throw std::runtime_error("Profile bounds");
    p.model=in.Identifier(80);if(!ModelId(p.model))throw std::runtime_error("Profile model path");
    p.modelSha=Sha(in);p.packSha=Sha(in);p.meshSha=Sha(in);p.captureSha=Sha(in);
    const auto objects=in.U32();if(!objects||objects>128)throw std::runtime_error("Profile object count");
    std::set<std::string> names;
    for(uint32_t i=0;i<objects;i++){
        SourceObject object;object.name=Utf8(in,1024);object.fingerprint=Sha(in);
        if(!names.insert(object.name).second)throw std::runtime_error("Profile duplicate object");
        const auto values=in.U32();if(!values||values>256)throw std::runtime_error("Profile baseline keys");std::set<std::string> ids;
        for(uint32_t j=0;j<values;j++){
            SourceValue value;value.name=Utf8(in,1024);auto bits=in.U32();std::memcpy(&value.weight,&bits,4);value.flags=in.U32();
            if(!ids.insert(value.name).second||!std::isfinite(value.weight)||std::abs(value.weight)>1000000||value.flags>3)throw std::runtime_error("Profile baseline value");
            if((value.flags&2)&&(value.weight!=0||(value.flags&1)))throw std::runtime_error("Dynamic source key nonzero or muted");
            object.values.push_back(std::move(value));
        }p.objects.push_back(std::move(object));
    }
    if(!in.Done())throw std::runtime_error("Profile trailing data");return p;
}
inline uint64_t UInt(std::span<const unsigned char> raw,size_t at,size_t count){
    if(at>raw.size()||count>raw.size()-at||count>8)throw std::runtime_error("MOD3 header bounds");
    uint64_t n=0;for(size_t i=0;i<count;i++)n|=static_cast<uint64_t>(raw[at+i])<<(i*8);return n;
}
inline unsigned Stride(uint32_t format){
    switch(format){
    case 0xa756f2f9:return 24;case 0xa5104ca0:return 28;case 0xc3ac03a1:return 36;case 0xc9690ab8:return 40;
    case 0x818904dc:return 28;case 0x0f06033f:return 32;case 0xf637401c:return 32;case 0xf471fe45:return 36;
    case 0x3c730760:return 36;case 0xb2fc0083:return 40;case 0x81f58067:return 40;case 0x83b33e3e:return 44;
    case 0x366995a7:return 44;case 0xb8e69244:return 48;default:throw std::runtime_error("Unsupported MOD3 vertex format");
    }
}
inline void ValidateModel(Profile& p,const Asset& asset,std::span<const unsigned char> raw){
    if(raw.size()!=p.modelBytes||pack::Digest(raw)!=p.modelSha||raw.size()<88||std::memcmp(raw.data(),"MOD\0",4)||UInt(raw,4,2)!=237)throw std::runtime_error("Installed MOD3 identity/version");
    if(asset.model!=p.model||asset.part!=p.part||asset.basis.size()!=p.vertexBytes||UInt(raw,8,2)!=p.meshCount||UInt(raw,24,4)!=p.vertexBytes)throw std::runtime_error("MOD3 profile/asset contract");
    const auto vertex=UInt(raw,80,8),mesh=UInt(raw,72,8);
    if(vertex>raw.size()||p.vertexBytes>raw.size()-vertex||mesh>raw.size()||static_cast<uint64_t>(p.meshCount)*80>raw.size()-mesh)throw std::runtime_error("MOD3 section bounds");
    if(!std::equal(asset.basis.begin(),asset.basis.end(),raw.begin()+static_cast<size_t>(vertex)))throw std::runtime_error("Pack basis differs from exported MOD3 baseline");
    std::set<uint32_t> positions,shading;
    std::vector<uint32_t> diskFormats;
    for(uint32_t i=0;i<p.meshCount;i++){
        const auto row=static_cast<size_t>(mesh)+i*80;const auto format=static_cast<uint32_t>(UInt(raw,row+24,4));const auto stride=Stride(format);
        if(p.diskContract){const auto resolved=meshcontract::Resolve(format);if(!resolved||resolved->stride!=stride)throw std::runtime_error("Disk contract vertex format not yet supported");diskFormats.push_back(format);}
        if(UInt(raw,row+14,1)!=stride)throw std::runtime_error("MOD3 vertex stride");
        const auto count=UInt(raw,row+2,2),start=UInt(raw,row+20,4)+stride*(UInt(raw,row+16,4)+UInt(raw,row+36,4));
        if(start>p.vertexBytes||count*stride>p.vertexBytes-start)throw std::runtime_error("MOD3 vertex range");
        for(uint64_t j=0;j<count;j++){const auto at=static_cast<uint32_t>(start+j*stride);positions.insert(at);shading.insert(at+12);}
    }
    for(const auto& key:asset.keys)for(const auto& delta:key.deltas)if(!positions.contains(delta.offset))throw std::runtime_error("Delta is not a MOD3 xyz field");
    for(const auto& rotation:asset.rotations)if(!shading.contains(rotation.offset))throw std::runtime_error("Rotation is not a MOD3 normal/tangent field");
    for(const auto& key:asset.keys)for(const auto& normal:key.normals)if(!shading.contains(normal.offset))throw std::runtime_error("Per-key normal is not a MOD3 normal/tangent field");
    std::set<std::string> dynamic;
    for(const auto& object:p.objects)for(const auto& value:object.values)if(value.flags&2)dynamic.insert(value.name);
    std::set<std::string> expected;for(const auto& key:asset.keys)expected.insert(key.info.id);
    if(dynamic!=expected)throw std::runtime_error("Source baseline dynamic key coverage");
    if(p.diskContract){
        std::vector<unsigned char> rows(raw.begin()+static_cast<size_t>(mesh),raw.begin()+static_cast<size_t>(mesh)+p.meshCount*80);
        if(!meshcontract::Normalize(rows,true)||pack::Digest(rows)!=p.meshSha)throw std::runtime_error("Disk-derived mesh contract differs");
        p.diskFormats.swap(diskFormats);
    }
}
struct Loaded {Profile profile;Asset asset;};
struct Failure {uint32_t part;std::string package,reason;};
inline constexpr size_t maxPackages=64,maxBasisBytes=128*1024*1024,maxInputBytes=512*1024*1024;
struct Collection {
    std::array<std::vector<std::shared_ptr<const Loaded>>,5> parts;
    std::vector<Failure> failures;
    size_t basisBytes=0,inputBytes=0;
    size_t Count()const{size_t count=0;for(const auto& part:parts)count+=part.size();return count;}
    std::shared_ptr<const Loaded> Find(unsigned part,std::string_view path)const{
        if(part>=parts.size())return {};
        const auto wanted=Fold(std::string(path));
        for(const auto& item:parts[part])if(Fold(item->profile.model)==wanted)return item;
        return {};
    }
};
inline void NoLinks(const std::filesystem::path& path,const std::filesystem::path& root){
    for(auto current=path;;current=current.parent_path()){
        const auto flags=GetFileAttributesW(current.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES||flags&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Missing file or linked model/sidecar path");
        if(current==root)break;
        if(current==current.parent_path())throw std::runtime_error("Sidecar escapes nativePC");
    }
}
inline Collection Load(const std::filesystem::path& root,const std::filesystem::path& nativePc){
    Collection collection;const auto ini=ReadFile(root/"runtime.ini",4096);
    ParseIni(std::string_view(reinterpret_cast<const char*>(ini.data()),ini.size()));
    const auto base=std::filesystem::absolute(nativePc).lexically_normal(),scan=base/"pl";
    if(!std::filesystem::exists(scan))return collection;
    NoLinks(scan,base);std::set<std::string> identities;size_t visited=0,attempted=0;
    for(auto it=std::filesystem::recursive_directory_iterator(scan);it!=std::filesystem::recursive_directory_iterator();++it){
        if(++visited>100000||it.depth()>16)throw std::runtime_error("Sidecar scan bound");
        const auto path=it->path();const auto flags=GetFileAttributesW(path.c_str());
        if(flags&FILE_ATTRIBUTE_REPARSE_POINT){it.disable_recursion_pending();collection.failures.push_back({5,path.generic_string(),"Linked model/sidecar path"});continue;}
        if(!it->is_directory()||path.extension()!=L".morph")continue;
        it.disable_recursion_pending();const auto name=path.lexically_relative(base).generic_string();
        uint32_t part=5;
        try{
            if(++attempted>maxPackages)throw std::runtime_error("Catalogue exceeds 64 models");
            for(const auto file:{"model.mhmprofile","asset.mhmpack","profile.json"})NoLinks(path/file,base);
            auto profileRaw=ReadFile(path/"model.mhmprofile",512*1024);auto profile=DecodeProfile(profileRaw);part=profile.part;
            if(Fold(profile.model)+".morph"!=Fold(name))throw std::runtime_error("Sidecar directory differs from embedded model identity");
            if(!identities.insert(Fold(profile.model)).second)throw std::runtime_error("Duplicate model resource identity");
            const auto modelPath=base/(profile.model+".mod3");NoLinks(modelPath,base);
            const auto packBytes=std::filesystem::file_size(path/"asset.mhmpack"),modelBytes=std::filesystem::file_size(modelPath);
            if(profile.vertexBytes>maxBasisBytes-collection.basisBytes||packBytes>maxInputBytes-collection.inputBytes
                ||modelBytes>maxInputBytes-collection.inputBytes-packBytes
                ||profileRaw.size()>maxInputBytes-collection.inputBytes-packBytes-modelBytes)throw std::runtime_error("Catalogue memory budget");
            auto asset=pack::Load(path/"asset.mhmpack",profile.packSha);
            ValidateModel(profile,asset,ReadFile(modelPath,128*1024*1024));
            asset.modelSha=profile.modelSha;asset.packSha=profile.packSha;
            collection.basisBytes+=profile.vertexBytes;collection.inputBytes+=packBytes+modelBytes+profileRaw.size();
            collection.parts[part].push_back(std::make_shared<const Loaded>(Loaded{std::move(profile),std::move(asset)}));
        }catch(const std::exception& e){collection.failures.push_back({part,name,e.what()});}
    }
    for(auto& part:collection.parts)std::sort(part.begin(),part.end(),[](const auto& a,const auto& b){return a->profile.model<b->profile.model;});
    return collection;
}
}
