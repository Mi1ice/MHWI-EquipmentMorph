#pragma once
// Synthetic-only executable fixture. Never linked into the validation DLL.
#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include "Catalog.hpp"
#include <cstdio>
namespace validationtest {
using mhmvalidation::Catalog;
using mhmvalidation::Case;
inline unsigned checks=0;
inline uint64_t clockMs=0;
inline void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
inline MhmApi Api(){MhmApi a{};Check(MhwMorphGetApi(MHM_API_VERSION,sizeof(a),&a)==MHM_OK,"production public API");return a;}
inline Catalog TestCatalog(uint32_t count=489){
    Catalog c;c.part=1;c.modelId="pl/synthetic/body";c.expectedKeyCount=count;c.modes={{"BASELINE",std::string(64,'a'),std::string(64,'b')},{"GEOMETRIC",std::string(64,'c'),std::string(64,'d')}};c.steps={0,.25f,.5f,.75f,1,.75f,.5f,.25f,0};
    Case single{"S01","单键",{}, {"key_0"}},combo{"C01","组合",{}, {}};
    for(unsigned i=0;i<30;++i){auto id="key_"+std::to_string(i);c.labels[id]="完整源名_目_"+std::to_string(i);if(i<25)combo.fixed.push_back({id,.5f});else combo.varying.push_back(id);}
    c.cases={single,combo};return c;
}
inline uint64_t Attach(uint32_t part=1,uint32_t count=489,const std::string& modelSha=std::string(64,'a'),const std::string& packSha=std::string(64,'b')){
    mhm::Asset a;a.part=part;a.model=part==1?"pl/synthetic/body":"pl/synthetic/arms";a.modelSha=modelSha;a.packSha=packSha;a.basis.resize(24);
    for(uint32_t i=0;i<count;++i){mhm::Key k;const auto id="key_"+std::to_string(i);std::memcpy(k.info.id,id.c_str(),id.size()+1);k.info.maximum=1;k.deltas.push_back({0,1,0,0});a.keys.push_back(std::move(k));}
    uint64_t target=0;Check(Core().Attach(std::move(a),target)==MHM_OK,"attach synthetic identity");return target;
}
inline uint64_t Capture(){uint64_t frame=0,rev=0;Check(Core().Capture(frame,rev)==MHM_OK,"capture production frame");return frame;}
inline void Acknowledge(uint64_t frame){Check(Core().Acknowledge(frame)==MHM_OK,"acknowledge production publication");Check(Core().ReleaseFrame(frame)==MHM_OK,"release production frame");}
inline void Apply(){Acknowledge(Capture());}
inline void Advance(uint64_t ms){clockMs+=ms;Check(Core().Tick(clockMs)==MHM_OK,"advance injected core clock");}
struct Fixture {~Fixture(){MhmTargetInfo t{};while(Core().TargetAt(0,t)==MHM_OK)Core().Detach(t.handle);}};
struct Source {
    uint64_t id=0;
    explicit Source(uint32_t priority=100){Check(Core().Open("synthetic-other",priority,2000,id)==MHM_OK,"open other source");}
    ~Source(){Core().Close(id);}
    uint64_t Set(uint64_t target,float value,uint32_t count=489){std::vector<MhmUpdate> values;for(uint32_t i=0;i<count;++i)values.push_back({target,i,value});uint64_t rev=0;Check(Core().Submit(id,values,rev)==MHM_OK,"other-source full replacement");return rev;}
};
}
