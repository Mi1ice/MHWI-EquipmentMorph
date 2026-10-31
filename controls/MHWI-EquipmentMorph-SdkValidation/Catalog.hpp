#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../MHWI-EquipmentMorph-ImGui/Preset.hpp"
#include <fstream>
#include <filesystem>
#include <iterator>

namespace mhmvalidation {
struct NamedValue {std::string id;float weight=0;};
struct Case {std::string id,title;std::vector<NamedValue> fixed;std::vector<std::string> varying;};
struct Mode {std::string name,modelSha,packSha;};
struct Catalog {uint32_t part=1,expectedKeyCount=0;std::string modelId;std::vector<Mode> modes;std::map<std::string,std::string> labels;std::vector<Case> cases;std::vector<float> steps;};
inline void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline bool Text(std::string_view text){return !text.empty()&&text.size()<=4096&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<32;})&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)>0;}
inline bool Identifier(std::string_view value,size_t bound=63){return morphpreset::Ascii(value,bound);}
using JsonValue=morphpreset::json::Value;
using morphpreset::json::Field;
inline std::string String(const JsonValue& v,const char* name){return Field(v,name,JsonValue::String).text;}
inline void Fields(const JsonValue& v,std::initializer_list<const char*> names){
    morphpreset::json::Fields(v,names.size());for(auto name:names)Require(v.object.contains(name),"Unknown or missing catalog field");
}
inline float Weight(double value){Require(std::isfinite(value)&&value>=0&&value<=1,"Weight outside 0..1");return static_cast<float>(value);}
inline Catalog LoadCatalog(std::string_view input){
    Require(!input.empty()&&input.size()<=256*1024&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),static_cast<int>(input.size()),nullptr,0)>0,"Invalid catalog encoding or size");
    const auto root=morphpreset::json::Reader(input).Parse();Fields(root,{"schema","part","model_id","expected_key_count","modes","labels","cases","steps"});
    Require(String(root,"schema")=="mhwi-sdk-validation-catalog-v1","Unsupported catalog schema");
    Catalog c;const auto part=Field(root,"part",JsonValue::Number).number,count=Field(root,"expected_key_count",JsonValue::Number).number;
    Require(part==1&&count>=1&&count<=1024&&std::floor(count)==count,"Invalid body target count");c.part=1;c.expectedKeyCount=static_cast<uint32_t>(count);c.modelId=String(root,"model_id");Require(Identifier(c.modelId,127),"Invalid model ID");
    std::set<std::string> modes;
    for(const auto& v:Field(root,"modes",JsonValue::Array).array){Fields(v,{"name","model_sha256","pack_sha256"});Mode m{String(v,"name"),String(v,"model_sha256"),String(v,"pack_sha256")};
        Require((m.name=="BASELINE"||m.name=="GEOMETRIC")&&modes.insert(m.name).second&&morphpreset::Hex(m.modelSha,64)&&morphpreset::Hex(m.packSha,64),"Incomplete mode identity");c.modes.push_back(std::move(m));}
    Require(c.modes.size()==2&&std::pair{c.modes[0].modelSha,c.modes[0].packSha}!=std::pair{c.modes[1].modelSha,c.modes[1].packSha},"Distinct two-mode identities required");
    for(const auto& v:Field(root,"labels",JsonValue::Array).array){Fields(v,{"id","source"});auto id=String(v,"id"),source=String(v,"source");Require(Identifier(id)&&Text(source)&&c.labels.emplace(id,source).second,"Invalid or repeated label");}
    Require(!c.labels.empty(),"Missing labels");std::set<std::string> caseIds,used;
    for(const auto& v:Field(root,"cases",JsonValue::Array).array){Fields(v,{"id","title","fixed","varying"});Case item;item.id=String(v,"id");item.title=String(v,"title");Require(Identifier(item.id)&&Text(item.title)&&caseIds.insert(item.id).second,"Invalid or repeated case");std::set<std::string> keys;
        for(const auto& f:Field(v,"fixed",JsonValue::Array).array){Fields(f,{"id","weight"});auto id=String(f,"id");const auto value=Field(f,"weight",JsonValue::Number).number;auto w=Weight(value);Require(c.labels.contains(id)&&keys.insert(id).second&&value==.5,"Invalid fixed case key");used.insert(id);item.fixed.push_back({id,w});}
        for(const auto& f:Field(v,"varying",JsonValue::Array).array){Require(f.kind==JsonValue::String&&c.labels.contains(f.text)&&keys.insert(f.text).second,"Invalid or repeated varying key");used.insert(f.text);item.varying.push_back(f.text);}
        Require((item.fixed.empty()&&item.varying.size()==1)||(item.fixed.size()==25&&item.varying.size()==5),"Expected single or 25+5 case");c.cases.push_back(std::move(item));}
    Require(!c.cases.empty()&&used.size()==c.labels.size(),"Missing case or unused label");
    for(const auto& v:Field(root,"steps",JsonValue::Array).array){Require(v.kind==JsonValue::Number,"Invalid step");c.steps.push_back(Weight(v.number));}
    Require(c.steps==std::vector<float>({0,.25f,.5f,.75f,1,.75f,.5f,.25f,0}),"Expected complete nine-step curve");return c;
}
inline Catalog LoadCatalogFile(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);Require(bool(stream),"Cannot open catalog");std::string raw(256*1024+1,'\0');stream.read(raw.data(),static_cast<std::streamsize>(raw.size()));raw.resize(static_cast<size_t>(stream.gcount()));return LoadCatalog(raw);
}
inline std::vector<float> BuildWeights(const Catalog& c,size_t caseIndex,size_t stepIndex,const std::vector<std::string>& liveIds){
    Require(caseIndex<c.cases.size()&&stepIndex<c.steps.size()&&liveIds.size()==c.expectedKeyCount,"Case, step or live count mismatch");
    std::map<std::string,size_t> indices;for(size_t i=0;i<liveIds.size();++i)Require(Identifier(liveIds[i])&&indices.emplace(liveIds[i],i).second,"Invalid live ID list");
    for(const auto& [id,label]:c.labels){(void)label;Require(indices.contains(id),"Required key absent from live target");}
    std::vector<float> result(liveIds.size(),0);const auto& item=c.cases[caseIndex];for(const auto& value:item.fixed)result.at(indices.at(value.id))=value.weight;for(const auto& id:item.varying)result.at(indices.at(id))=c.steps[stepIndex];return result;
}
}
