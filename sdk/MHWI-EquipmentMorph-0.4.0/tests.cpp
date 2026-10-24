#include "../../runtime/EquipmentMorphPerKeyCore/api.cpp"
#include "examples/named_control.hpp"
#include <cstdio>
#include <stdexcept>
#include "capacity_tests.hpp"
int main(){try{
    capacitytests::Run();
    unsigned checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);checks++;};MhmApi api{};MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api);
    auto attach=[](uint32_t part,const char* model){mhm::Asset a;a.part=part;a.model=model;a.basis.resize(64);for(const char* id:{"Example_B","Example_A"}){mhm::Key k;strcpy_s(k.info.id,id);k.info.maximum=1;k.deltas.push_back({0,1,0,0});a.keys.push_back(k);}uint64_t h=0;if(Core().Attach(std::move(a),h)!=MHM_OK)throw std::runtime_error("attach");return h;};
    const auto body=attach(MHM_BODY,"pl/example/body");auto arms=attach(MHM_ARMS,"pl/example/arms");uint64_t revision=0,other=0;
    api.open_source("underlying",100,60000,&other);const MhmUpdate underneath[]{{body,0,.8f},{body,1,.2f}};api.submit(other,underneath,2,&revision);
    mhwi::KeyClient client;check(client.Connect(api)==MHM_OK&&client.Refresh()==MHM_OK&&client.Keys().size()==4,"API-only generic discovery");
    check(client.Set({{MHM_BODY,"pl/example/body","Example_A",.13724681f},{MHM_ARMS,"pl/example/arms","Example_B",.731f}},revision)==MHM_OK,"arbitrary named cross-part batch");
    std::vector<MhmWeightState> values;uint64_t applied=0;client.Read(body,values,revision,applied);
    check(values[0].requested==.8f&&values[1].requested==.13724681f,"name order and unowned key preserve underlying source");
    check(client.Set({{MHM_BODY,"pl/example/body","Example_A",.4f}},revision)==MHM_OK&&client.Read(arms,values,revision,applied)==MHM_OK&&values[0].requested==.731f,"partial change retains other part");
    check(client.Set({{MHM_BODY,"pl/example/body","missing",.5f}},revision)==MHM_NOT_READY&&client.Set({{MHM_BODY,"wrong/model","Example_A",.5f}},revision)==MHM_NOT_READY,"wrong model or key refused");
    check(client.Set({{MHM_BODY,"pl/example/body","Example_A",.2f},{MHM_BODY,"pl/example/body","Example_A",.3f}},revision)==MHM_INVALID&&client.Set({{MHM_BODY,"pl/example/body","Example_A",NAN}},revision)==MHM_INVALID,"duplicate and invalid weights refused");
    check(client.Read(body,values,revision,applied)==MHM_OK&&values[1].requested==.4f,"rejected batch preserves contribution");
    check(client.ReleaseKey(MHM_BODY,"pl/example/body","Example_A",revision)==MHM_OK&&client.Read(body,values,revision,applied)==MHM_OK&&values[1].requested==.2f&&client.Read(arms,values,revision,applied)==MHM_OK&&values[0].requested==.731f,"one-key release preserves healthy other part");
    client.Set({{MHM_BODY,"pl/example/body","Example_A",0}},revision);client.Read(body,values,revision,applied);check(values[1].requested==0&&values[1].source==client.Source(),"zero retains ownership");
    Core().Detach(arms);arms=attach(MHM_ARMS,"pl/example/arms");check(client.Refresh()==MHM_OK&&client.Read(arms,values,revision,applied)==MHM_OK&&values[0].requested==0&&client.Read(body,values,revision,applied)==MHM_OK&&values[1].source==client.Source(),"new target no replay, healthy part retained");
    Core().Tick(2001);check(client.Heartbeat()==MHM_STALE&&!client.Source()&&client.Read(body,values,revision,applied)==MHM_OK&&values[1].requested==.2f,"lease loss clears contribution cache");
    morphpartclient::Client full;full.Connect(api);
    check(mhwi_example::ApplyFullParts(full,{{MHM_BODY,"pl/example/body",{{"Example_A",.6f}}}},revision)==MHM_OK&&client.Read(body,values,revision,applied)==MHM_OK&&values[0].requested==0&&values[1].requested==.6f,"generic full-part example explicitly zeros extra keys");
    check(mhwi_example::ApplyFullParts(full,{{MHM_BODY,"pl/example/body",{{"Example_A",.2f},{"Example_A",.3f}}}},revision)==MHM_INVALID,"full-part duplicate names refused");
    full.Release();check(client.Release()==MHM_OK,"safe release after expiry");
    std::printf("PASS: %u generic SDK checks; synthetic data only\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
