#pragma once
#include "../../runtime/EquipmentMorphControlBridge/Protocol.hpp"
#include "../../runtime/EquipmentMorphPerKeyCore/include/mhw_morph_identity.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <map>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace morphpreset {
inline const char* PartName(uint32_t part){const char* names[]={"头部","身体","腕部","腰部","腿部"};return part<5?names[part]:"装备";}
inline bool Hex(std::string_view text,size_t size){return text.size()==size&&std::all_of(text.begin(),text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline bool Ascii(std::string_view text,size_t maximum){return !text.empty()&&text.size()<=maximum&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<=126;});}
inline bool Utf8(std::string_view text){return !text.empty()&&text.size()<=128&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)>0
    &&text.find_first_not_of(" \t\r\n")!=std::string_view::npos&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<32;});}
struct Key {std::string id;float weight=0;bool operator==(const Key&)const=default;};
struct Part {uint32_t part=0;std::string model,modelSha,packSha;std::vector<Key> keys;};
struct Document {std::string id,name;std::vector<Part> parts;};
inline void Validate(const Document& doc){
    if(!Hex(doc.id,32)||!Utf8(doc.name)||doc.parts.empty()||doc.parts.size()>5)throw std::runtime_error("预设名称、编号或部位数量无效");
    std::set<uint32_t> parts;
    for(const auto& part:doc.parts){
        if(part.part>4||!parts.insert(part.part).second||!Ascii(part.model,127)||!Hex(part.modelSha,64)||!Hex(part.packSha,64)||part.keys.empty()||part.keys.size()>64)
            throw std::runtime_error("预设装备身份无效或重复");
        std::set<std::string> keys;
        for(const auto& key:part.keys)if(!Ascii(key.id,63)||!keys.insert(key.id).second||!std::isfinite(key.weight)||key.weight<0||key.weight>1)
            throw std::runtime_error("预设形态键重复或权重无效");
    }
}
inline std::string Quote(std::string_view text){
    std::string out="\"";for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32)throw std::runtime_error("预设文本包含控制字符");else out+=static_cast<char>(c);}return out+'"';
}
// Small bounded JSON reader for the preset schema; no external dependency or
// file access. Object order is irrelevant; duplicate properties are rejected.
namespace json {
struct Value {enum Kind{String,Number,Object,Array} kind=String;std::string text;double number=0;std::map<std::string,Value> object;std::vector<Value> array;};
class Reader {
    std::string_view input;size_t at=0,nodes=0;
    [[noreturn]] static void Fail(){throw std::runtime_error("预设 JSON 格式无效");}
    void Space(){while(at<input.size()&&(input[at]==' '||input[at]=='\n'||input[at]=='\r'||input[at]=='\t'))at++;}
    bool Take(char c){Space();if(at<input.size()&&input[at]==c){at++;return true;}return false;}
    unsigned Code(){unsigned code=0;for(unsigned i=0;i<4;i++){if(at==input.size())Fail();const char c=input[at++];unsigned d=16;if(c>='0'&&c<='9')d=unsigned(c-'0');else if(c>='a'&&c<='f')d=unsigned(c-'a')+10;else if(c>='A'&&c<='F')d=unsigned(c-'A')+10;if(d>15)Fail();code=code*16+d;}return code;}
    std::string String(){
        if(!Take('"'))Fail();std::string out;
        while(at<input.size()){
            const unsigned char c=static_cast<unsigned char>(input[at++]);if(c=='"')return out;if(c<32)Fail();
            if(c!='\\'){out+=static_cast<char>(c);continue;}
            if(at==input.size())Fail();const char e=input[at++];
            if(e=='"'||e=='\\'||e=='/')out+=e;
            else if(e=='n')out+='\n';else if(e=='r')out+='\r';else if(e=='t')out+='\t';else if(e=='b')out+='\b';else if(e=='f')out+='\f';
            else if(e=='u'){
                unsigned code=Code();if(code>=0xd800&&code<=0xdbff){if(at+2>input.size()||input.substr(at,2)!="\\u")Fail();at+=2;const auto low=Code();if(low<0xdc00||low>0xdfff)Fail();code=0x10000+((code-0xd800)<<10)+(low-0xdc00);}else if(code>=0xdc00&&code<=0xdfff)Fail();
                if(code<0x80)out+=static_cast<char>(code);
                else if(code<0x800){out+=static_cast<char>(0xc0|(code>>6));out+=static_cast<char>(0x80|(code&63));}
                else if(code<0x10000){out+=static_cast<char>(0xe0|(code>>12));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
                else{out+=static_cast<char>(0xf0|(code>>18));out+=static_cast<char>(0x80|((code>>12)&63));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
            }else Fail();
            if(out.size()>1024)Fail();
        }Fail();
    }
    Value Read(unsigned depth){
        if(depth>8||++nodes>4096)Fail();Space();if(at==input.size())Fail();Value value;
        if(input[at]=='"'){value.text=String();return value;}
        if(Take('{')){value.kind=Value::Object;if(Take('}'))return value;do{auto key=String();if(!Take(':')||value.object.size()>=16)Fail();auto item=Read(depth+1);if(!value.object.emplace(std::move(key),std::move(item)).second)Fail();if(Take('}'))return value;}while(Take(','));Fail();}
        if(Take('[')){value.kind=Value::Array;if(Take(']'))return value;do{if(value.array.size()>=320)Fail();value.array.push_back(Read(depth+1));if(Take(']'))return value;}while(Take(','));Fail();}
        const auto start=at;if(input[at]=='-')at++;if(at==input.size())Fail();
        if(input[at]=='0')at++;else{if(input[at]<'1'||input[at]>'9')Fail();while(at<input.size()&&input[at]>='0'&&input[at]<='9')at++;}
        if(at<input.size()&&input[at]=='.'){at++;const auto begin=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')at++;if(at==begin)Fail();}
        if(at<input.size()&&(input[at]=='e'||input[at]=='E')){at++;if(at<input.size()&&(input[at]=='+'||input[at]=='-'))at++;const auto begin=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')at++;if(at==begin)Fail();}
        value.kind=Value::Number;const auto result=std::from_chars(input.data()+start,input.data()+at,value.number);
        if(result.ec!=std::errc{}||result.ptr!=input.data()+at||!std::isfinite(value.number))Fail();return value;
    }
public:
    explicit Reader(std::string_view text):input(text){if(input.size()>256*1024)Fail();}
    Value Parse(){auto value=Read(0);Space();if(at!=input.size())Fail();return value;}
};
inline const Value& Field(const Value& value,const char* name,Value::Kind kind){
    if(value.kind!=Value::Object)throw std::runtime_error("预设对象类型错误");const auto found=value.object.find(name);
    if(found==value.object.end()||found->second.kind!=kind)throw std::runtime_error("预设字段缺失或类型错误");return found->second;
}
inline void Fields(const Value& value,size_t count){if(value.kind!=Value::Object||value.object.size()!=count)throw std::runtime_error("预设包含未知字段");}
}
inline Document Decode(std::string_view text){
    const auto root=json::Reader(text).Parse();json::Fields(root,4);
    if(json::Field(root,"schema",json::Value::Number).number!=1)throw std::runtime_error("不支持的预设版本");
    Document doc;doc.id=json::Field(root,"id",json::Value::String).text;doc.name=json::Field(root,"name",json::Value::String).text;
    for(const auto& entry:json::Field(root,"parts",json::Value::Array).array){
        json::Fields(entry,5);const auto number=json::Field(entry,"part",json::Value::Number).number;if(number<0||number>4||std::floor(number)!=number)throw std::runtime_error("预设部位无效");
        Part part;part.part=static_cast<uint32_t>(number);part.model=json::Field(entry,"model",json::Value::String).text;part.modelSha=json::Field(entry,"model_sha256",json::Value::String).text;part.packSha=json::Field(entry,"pack_sha256",json::Value::String).text;
        for(const auto& item:json::Field(entry,"keys",json::Value::Array).array){json::Fields(item,2);const auto weight=json::Field(item,"weight",json::Value::Number).number;if(weight<0||weight>1)throw std::runtime_error("预设权重越界");part.keys.push_back({json::Field(item,"id",json::Value::String).text,static_cast<float>(weight)});}
        doc.parts.push_back(std::move(part));
    }Validate(doc);return doc;
}
inline std::string Encode(const Document& doc){
    Validate(doc);std::string out="{\n  \"schema\": 1,\n  \"id\": "+Quote(doc.id)+",\n  \"name\": "+Quote(doc.name)+",\n  \"parts\": [";bool firstPart=true;
    for(const auto& part:doc.parts){if(!firstPart)out+=',';firstPart=false;out+="\n    {\"part\": "+std::to_string(part.part)+", \"model\": "+Quote(part.model)+", \"model_sha256\": "+Quote(part.modelSha)+", \"pack_sha256\": "+Quote(part.packSha)+", \"keys\": [";bool firstKey=true;
        for(const auto& key:part.keys){if(!firstKey)out+=',';firstKey=false;char number[64]{};const auto result=std::to_chars(number,number+sizeof(number),key.weight,std::chars_format::general,std::numeric_limits<float>::max_digits10);if(result.ec!=std::errc{})throw std::runtime_error("权重编码失败");out+="{\"id\": "+Quote(key.id)+", \"weight\": "+std::string(number,result.ptr)+'}';}out+="]}";
    }return out+"\n  ]\n}\n";
}
using Identities=std::array<MhmAssetIdentity,5>;
inline bool ValidIdentity(const MhmAssetIdentity& identity,uint64_t handle){return identity.handle==handle&&identity.model_sha256[64]==0&&identity.pack_sha256[64]==0&&Hex(std::string_view(identity.model_sha256,64),64)&&Hex(std::string_view(identity.pack_sha256,64),64);}
struct Match {bool compatible=false;std::string message;std::vector<MhmUpdate> updates;};
inline Match Check(const Document& doc,const mhmcontrol::Response& data,const Identities& identities){
    Match result;unsigned matched=0;std::string reason;
    for(const auto& part:doc.parts){
        const mhmcontrol::Target* target=nullptr;for(uint32_t i=0;i<data.targetCount;i++)if(data.targets[i].info.part==part.part)target=&data.targets[i];
        std::string failure;
        if(!target)failure="未穿戴或未绑定";
        else if(part.model!=target->info.model_id)failure="装备模型不同";
        else if(!ValidIdentity(identities[part.part],target->info.handle))failure="无法确认 MOD 身份";
        else if(part.modelSha!=identities[part.part].model_sha256||part.packSha!=identities[part.part].pack_sha256)failure="模型或形态键数据已更换";
        else for(const auto& key:part.keys){
            const mhmcontrol::Key* found=nullptr;for(uint32_t k=0;k<target->count;k++){const auto& candidate=data.keys[target->first+k];if(key.id==candidate.info.id)found=&candidate;}
            if(!found||key.weight<found->info.minimum||key.weight>found->info.maximum){failure="形态键缺失或权重越界";break;}
            result.updates.push_back({target->info.handle,found->index,key.weight});
        }
        if(failure.empty())matched++;else{if(!reason.empty())reason+="；";reason+=std::string(PartName(part.part))+"："+failure;}
    }
    result.compatible=matched==doc.parts.size();result.message=result.compatible?"全部匹配（"+std::to_string(matched)+" 个部位）":"匹配 "+std::to_string(matched)+"／"+std::to_string(doc.parts.size())+" · "+reason;
    if(!result.compatible)result.updates.clear();return result;
}
inline Document Capture(const mhmcontrol::Response& data,const Identities& identities,std::string id,std::string name){
    Document doc;doc.id=std::move(id);doc.name=std::move(name);
    for(uint32_t i=0;i<data.targetCount;i++){
        const auto& target=data.targets[i];Part part;part.part=target.info.part;part.model=target.info.model_id;
        for(uint32_t k=0;k<target.count;k++){const auto& key=data.keys[target.first+k];if(key.owned)part.keys.push_back({key.info.id,key.contribution});}
        if(part.keys.empty())continue;
        if(!ValidIdentity(identities[part.part],target.info.handle))throw std::runtime_error(std::string(PartName(part.part))+"：无法确认 MOD 身份，请使用配套核心");
        part.modelSha=identities[part.part].model_sha256;part.packSha=identities[part.part].pack_sha256;doc.parts.push_back(std::move(part));
    }
    if(doc.parts.empty())throw std::runtime_error("请先接管至少一个形态键；已接管的零权重也可保存");Validate(doc);return doc;
}
inline bool SameContributions(const Document& doc,const mhmcontrol::Response& data,const Identities& identities){
    const auto match=Check(doc,data,identities);if(!match.compatible)return false;size_t owned=0;
    for(uint32_t i=0;i<data.keyCount;i++)if(data.keys[i].owned){owned++;const auto& key=data.keys[i];const auto found=std::find_if(match.updates.begin(),match.updates.end(),[&](const auto& v){return v.target==key.target&&v.key==key.index&&v.weight==key.contribution;});if(found==match.updates.end())return false;}
    return owned==match.updates.size();
}
}
