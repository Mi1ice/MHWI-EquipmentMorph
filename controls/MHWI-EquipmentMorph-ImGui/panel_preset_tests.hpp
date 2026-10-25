#pragma once
#include "Preset.hpp"
namespace paneltest {
inline morphpreset::Document BigPreset(){
    morphpreset::Document doc{std::string(32,'a'),"五部位 \"精度\"",{}};
    for(uint32_t p=0;p<5;++p){morphpreset::Part part;part.part=p;part.model="synthetic/part"+std::to_string(p);part.modelSha=std::string(64,'a');part.packSha=std::string(64,'b');
        for(uint32_t k=0;k<1024;++k){const auto suffix=std::to_string(k);const auto id=std::string(63-suffix.size(),'\\')+suffix;const float value=k%4==0?0:k%4==1?std::numeric_limits<float>::denorm_min():k%4==2?.123456791f:1.f;part.keys.push_back({id,value});}doc.parts.push_back(std::move(part));
    }return doc;
}
template<class F>inline void Refused(F&& action,const char* name){bool refused=false;try{action();}catch(const std::exception&){refused=true;}Check(refused,name);}
inline void TestPreset5120RoundTrip(){
    const auto doc=BigPreset();const auto text=morphpreset::Encode(doc);Check(text.size()<=1024*1024,"worst length escaped preset within 1 MiB");const auto decoded=morphpreset::Decode(text);
    Check(decoded.name==doc.name&&decoded.parts.size()==5,"Unicode name and five parts");for(uint32_t p=0;p<5;++p)Check(decoded.parts[p].keys==doc.parts[p].keys,"all IDs and float32 values exact");
    std::printf("Preset worst-length fixture: %zu bytes\n",text.size());
}
inline void TestSchema2Only(){auto doc=BigPreset();doc.parts.resize(1);doc.parts[0].keys.resize(1);const auto encoded=morphpreset::Encode(doc);Check(encoded.find("\"schema\": 2")!=std::string::npos,"encode schema 2");
    for(auto schema:{0,1,3}){auto bad=encoded;bad.replace(bad.find("\"schema\": 2"),11,"\"schema\": "+std::to_string(schema));Refused([&]{morphpreset::Decode(bad);},"other schema rejected");}
}
inline void TestReaderBudgetsIndependent(){
    std::string array="[0";for(uint32_t i=1;i<321;++i)array+=",0";array+=']';Refused([&]{morphpreset::json::Reader(array).Parse();},"default 320 array unchanged");
    Refused([&]{morphpreset::json::Reader(std::string(256*1024+1,' ')).Parse();},"default bytes unchanged");
    Refused([&]{morphpreset::Decode(std::string(1024*1024+1,' '));},"1MiB plus one rejected");
    auto big=BigPreset();big.parts[0].keys.push_back({"extra",.1f});Refused([&]{morphpreset::Encode(big);},"1025 keys rejected");
    const auto valid=morphpreset::Encode(BigPreset());auto bad=valid;bad.insert(1,"\"unknown\":0,");Refused([&]{morphpreset::Decode(bad);},"unknown fields rejected");
    bad=valid;bad.insert(1,"\"schema\":2,");Refused([&]{morphpreset::Decode(bad);},"duplicate fields rejected");
    bad=valid;bad.replace(bad.find("五"),3,std::string(1,static_cast<char>(0xff)));Refused([&]{morphpreset::Decode(bad);},"invalid UTF8 rejected");
    Refused([&]{morphpreset::Decode("[[[[[[[[[[0]]]]]]]]]]");},"deep nesting rejected");
    std::string wide="[0";for(unsigned i=1;i<1025;++i)wide+=",0";wide+=']';
    Refused([&]{morphpreset::json::Reader(wide,{1024*1024,32768,1024,8,1024}).Parse();},"1025 array items rejected");
    std::string nodes="[";for(unsigned a=0;a<32;++a){if(a)nodes+=',';nodes+="[0";for(unsigned i=1;i<1023;++i)nodes+=",0";nodes+=']';}nodes+=']';
    Refused([&]{morphpreset::json::Reader(nodes,{1024*1024,32768,1024,8,1024}).Parse();},"32769 nodes rejected");
    Refused([&]{morphpreset::json::Reader(nodes).Parse();},"default node budget remains bounded");
}
inline void PresetTests(){TestPreset5120RoundTrip();TestSchema2Only();TestReaderBudgetsIndependent();}
}
