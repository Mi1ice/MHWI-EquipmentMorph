#pragma once
#include "asset_pack.hpp"

namespace mhm::pack {
struct CatalogAsset {Asset asset;std::string modelSha256,packSha256;};
inline std::vector<CatalogAsset> LoadCatalog(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Catalog file");
    const auto size=file.tellg();if(size<=0||size>4096)throw std::runtime_error("Catalog bound");
    std::vector<unsigned char> raw(static_cast<size_t>(size));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(raw.data()),static_cast<std::streamsize>(raw.size())))throw std::runtime_error("Catalog read");
    Reader input(raw);const auto magic=input.Take(8);if(std::memcmp(magic.data(),"MHMCAT01",8)!=0)throw std::runtime_error("Catalog version");
    const auto count=input.U32();if(!count||count>limits.targets)throw std::runtime_error("Catalog count");
    const auto root=std::filesystem::canonical(path.parent_path());
    std::vector<CatalogAsset> result;std::array<bool,5> seen{};
    for(uint32_t i=0;i<count;i++) {
        const auto part=input.U32();if(part>MHM_LEGS||seen[part])throw std::runtime_error("Catalog part");seen[part]=true;
        const auto model=input.Identifier(128);
        auto hash=[&](){const auto bytes=input.Take(64);std::string value(bytes.begin(),bytes.end());
            if(!std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::runtime_error("Catalog hash");return value;};
        const auto modelSha=hash(),packSha=hash(),name=input.Identifier(64);
        if(name=="."||name==".."||name.find_first_of("/\\:")!=name.npos)throw std::runtime_error("Catalog local pack name");
        const auto source=std::filesystem::canonical(root/name),relative=source.lexically_relative(root);
        if(relative.empty()||*relative.begin()=="..")throw std::runtime_error("Catalog escaped root");
        auto asset=Load(source,packSha);
        if(asset.part!=part||asset.model!=model)throw std::runtime_error("Catalog asset identity");
        result.push_back({std::move(asset),modelSha,packSha});
    }
    if(!input.Done())throw std::runtime_error("Catalog trailing data");return result;
}
}
