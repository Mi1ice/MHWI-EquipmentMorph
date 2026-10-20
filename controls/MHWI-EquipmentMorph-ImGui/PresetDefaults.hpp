#pragma once
#include "PresetStore.hpp"

namespace morphpreset {
struct DefaultRef {std::string preset;uint32_t part=0;bool operator==(const DefaultRef&)const=default;};
inline std::string DefaultParts(const std::vector<DefaultRef>& refs,const std::string& id){std::string text;for(uint32_t part=0;part<5;part++)if(std::find(refs.begin(),refs.end(),DefaultRef{id,part})!=refs.end()){if(!text.empty())text+="、";text+=PartName(part);}return text;}
inline const Part* FindPart(const Document& doc,uint32_t part){for(const auto& item:doc.parts)if(item.part==part)return &item;return nullptr;}
inline bool SameAsset(const Part& a,const Part& b){return a.part==b.part&&a.model==b.model&&a.modelSha==b.modelSha&&a.packSha==b.packSha;}
inline const Document* FindDocument(const std::vector<Document>& docs,const std::string& id){for(const auto& doc:docs)if(doc.id==id)return &doc;return nullptr;}
// A later explicit registration replaces overlapping asset assignments. Storage
// order is registration order, independent of name sorting in the preset list.
inline std::vector<DefaultRef> ResolveDefaults(const std::vector<DefaultRef>& refs,const std::vector<Document>& docs){
    std::vector<DefaultRef> result;
    for(const auto& ref:refs){
        const auto* doc=FindDocument(docs,ref.preset);const auto* part=doc?FindPart(*doc,ref.part):nullptr;if(!part)continue;
        std::erase_if(result,[&](const auto& prior){const auto* previous=FindDocument(docs,prior.preset);const auto* asset=previous?FindPart(*previous,prior.part):nullptr;return asset&&SameAsset(*asset,*part);});
        result.push_back(ref);
    }return result;
}
class DefaultsStore {
    std::filesystem::path path;
public:
    explicit DefaultsStore(const std::filesystem::path& presets):path(PresetPath(presets).parent_path()/L"auto-presets.json"){}
    std::vector<DefaultRef> Load()const{
        NoPresetLink(path.parent_path());NoPresetLink(path);if(!std::filesystem::exists(path))return {};
        const auto size=std::filesystem::file_size(path);if(!size||size>256*1024)throw std::runtime_error("自动预设配置大小无效");
        std::ifstream file(path,std::ios::binary);std::string text(static_cast<size_t>(size),'\0');if(!file.read(text.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("无法读取自动预设配置");
        const auto root=json::Reader(text).Parse();json::Fields(root,2);
        if(json::Field(root,"schema",json::Value::Number).number!=1)throw std::runtime_error("自动预设配置版本不支持");
        const auto& groups=json::Field(root,"defaults",json::Value::Array).array;if(groups.size()>256)throw std::runtime_error("自动预设数量超限");
        std::set<std::string> ids;std::vector<DefaultRef> result;
        for(const auto& group:groups){
            json::Fields(group,2);const auto& id=json::Field(group,"preset",json::Value::String).text;if(!Hex(id,32)||!ids.insert(id).second)throw std::runtime_error("自动预设编号无效或重复");
            const auto& parts=json::Field(group,"parts",json::Value::Array).array;if(parts.empty()||parts.size()>5)throw std::runtime_error("自动预设部位无效");std::set<uint32_t> used;
            for(const auto& part:parts){if(part.kind!=json::Value::Number||part.number<0||part.number>4||std::floor(part.number)!=part.number||!used.insert(static_cast<uint32_t>(part.number)).second)throw std::runtime_error("自动预设部位无效或重复");result.push_back({id,static_cast<uint32_t>(part.number)});}
        }return result;
    }
    void Save(const std::vector<DefaultRef>& refs)const{
        std::vector<std::string> ids;std::map<std::string,std::set<uint32_t>> parts;
        for(const auto& ref:refs){if(!Hex(ref.preset,32)||ref.part>4)throw std::runtime_error("自动预设关联无效");auto& assigned=parts[ref.preset];if(!assigned.insert(ref.part).second)throw std::runtime_error("自动预设部位重复");if(assigned.size()==1)ids.push_back(ref.preset);}
        if(ids.size()>256)throw std::runtime_error("自动预设数量超限");
        std::string text="{\n  \"schema\": 1,\n  \"defaults\": [";bool first=true;
        for(const auto& id:ids){if(!first)text+=',';first=false;text+="\n    {\"preset\": "+Quote(id)+", \"parts\": [";bool next=false;
            for(auto part:parts[id]){if(next)text+=',';next=true;text+=std::to_string(part);}text+="]}";
        }text+="\n  ]\n}\n";WritePresetText(path,text);
    }
};
}
