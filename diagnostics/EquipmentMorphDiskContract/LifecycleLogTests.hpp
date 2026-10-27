#pragma once
#include "../../tools/morph_reference_json.hpp"
namespace lifecyclelogtests {
inline unsigned Run(){
    unsigned checks=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    launchvalidationtest::Fixture f;
    auto asset=std::make_shared<mhm::Asset>(*f.Part().asset);
    asset->modelSha=std::string(64,'a');asset->packSha=std::string(64,'b');f.Part().asset=asset;
    struct Log {std::string text;bool Write(const char* line){text=line;return true;}} log;
    std::uint64_t completed=0;
    for(unsigned workers:{8u,1u,3u,8u}){
        f.workers=workers;inputfasttest::Put(f.engine,0x68D14,workers);
        for(unsigned i=0;i<workers;i++)f.Launch(i);
        facecontinuous::WriteState(log,f.Part(),"part_state");
        auto state=morphreference::json::Reader(log.text).Parse();
        using morphreference::json::Field;using J=morphreference::json::Value;
        check(Field(state,"model_id",J::String).text==asset->model
            &&Field(state,"model_sha256",J::String).text==asset->modelSha
            &&Field(state,"pack_sha256",J::String).text==asset->packSha,"state binds target to complete asset identity");
        check(Field(state,"native_workers",J::Number).number==workers
            &&Field(state,"cycle_open",J::Boolean).number==1
            &&Field(state,"completed_signals",J::Number).number==double(completed),"open cycle cannot masquerade as a completed worker batch");
        f.Finish();completed++;
        facecontinuous::WriteState(log,f.Part(),"part_state");state=morphreference::json::Reader(log.text).Parse();
        check(Field(state,"cycle_open",J::Boolean).number==0
            &&Field(state,"completed_signals",J::Number).number==double(completed)
            &&Field(state,"joined_boundaries",J::Number).number==double(completed),"closed cycle logs completed native CPU boundaries");
        check(Field(state,"observed_tick",J::Number).number>0&&log.text.back()=='\n',"worker emits bounded complete timestamped JSON");
    }
    return checks;
}
}
