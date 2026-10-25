#include "Catalog.hpp"
#include <cstdio>
#include <functional>
#include <numeric>

using namespace mhmvalidation;
namespace {
unsigned checks=0;
void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;}
std::string Fixture(){
    std::string out=R"({"schema":"mhwi-sdk-validation-catalog-v1","part":1,"model_id":"pl/synthetic/body","expected_key_count":489,"modes":[)";
    out+="{\"name\":\"BASELINE\",\"model_sha256\":\""+std::string(64,'a')+"\",\"pack_sha256\":\""+std::string(64,'b')+"\"},";
    out+="{\"name\":\"GEOMETRIC\",\"model_sha256\":\""+std::string(64,'c')+"\",\"pack_sha256\":\""+std::string(64,'d')+"\"}],\"labels\":[";
    for(unsigned i=0;i<30;++i){if(i)out+=',';out+="{\"id\":\"key_"+std::to_string(i)+"\",\"source\":\"眉と目_完全な名前_"+std::to_string(i)+"\"}";}
    out+="],\"cases\":[{\"id\":\"S01\",\"title\":\"单键\",\"fixed\":[],\"varying\":[\"key_0\"]},{\"id\":\"C01\",\"title\":\"组合\",\"fixed\":[";
    for(unsigned i=0;i<25;++i){if(i)out+=',';out+="{\"id\":\"key_"+std::to_string(i)+"\",\"weight\":0.5}";}
    return out+"],\"varying\":[\"key_25\",\"key_26\",\"key_27\",\"key_28\",\"key_29\"]}],\"steps\":[0,0.25,0.5,0.75,1,0.75,0.5,0.25,0]}";
}
std::vector<std::string> Ids(size_t count){std::vector<std::string> ids;for(size_t i=0;i<count;++i)ids.push_back("key_"+std::to_string(i));std::reverse(ids.begin(),ids.end());return ids;}
void Reject(std::string value){bool rejected=false;try{(void)LoadCatalog(value);}catch(const std::exception&){rejected=true;}Check(rejected,"invalid catalog rejected");}
void TestSourceLabelDiffersFromId(){const auto c=LoadCatalog(Fixture());Check(c.labels.at("key_0")=="眉と目_完全な名前_0","Unicode source label distinct from ID");Check(c.modes.size()==2&&c.expectedKeyCount==489,"both modes and actual count");}
void TestFullWeightsAndCaseIsolation(){
    auto c=LoadCatalog(Fixture());for(size_t count:{489u,1024u}){c.expectedKeyCount=static_cast<uint32_t>(count);const auto ids=Ids(count);
        auto single=BuildWeights(c,0,2,ids);Check(single.size()==count&&std::count(single.begin(),single.end(),.5f)==1,"one nonzero in full actual target");
        auto combo=BuildWeights(c,1,2,ids);Check(combo.size()==count&&std::count(combo.begin(),combo.end(),.5f)==30,"25 fixed plus 5 varying");
        Check(std::count(combo.begin(),combo.end(),0.f)==static_cast<std::ptrdiff_t>(count-30),"unused keys explicitly zero");
        single=BuildWeights(c,0,4,ids);Check(std::count(single.begin(),single.end(),1.f)==1&&std::count(single.begin(),single.end(),0.f)==static_cast<std::ptrdiff_t>(count-1),"case isolation independent live order");
    }
}
void TestReverseSteps(){const auto c=LoadCatalog(Fixture());Check(c.steps==std::vector<float>({0,.25f,.5f,.75f,1,.75f,.5f,.25f,0}),"complete reverse sequence");auto w=BuildWeights(c,1,8,Ids(489));Check(std::count(w.begin(),w.end(),.5f)==25,"combination tail retains fixed keys");}
void TestMalformed(){
    auto change=[](std::string a,const std::string& from,const std::string& to){const auto p=a.find(from);if(p==a.npos)throw std::runtime_error("fixture text absent");a.replace(p,from.size(),to);return a;};
    const auto good=Fixture();Reject(change(good,"\"part\":1","\"part\":1,\"extra\":0"));Reject(change(good,"\"part\":1","\"part\":1,\"part\":1"));
    Reject(change(good,"\"id\":\"key_1\"","\"id\":\"key_0\""));Reject(change(good,"\"id\":\"C01\"","\"id\":\"S01\""));
    Reject(change(good,"\"key_25\",\"key_26\"","\"key_0\",\"key_26\""));Reject(change(good,"\"key_29\"]}","\"unknown\"]}"));
    Reject(change(good,"\"weight\":0.5","\"weight\":1.5"));Reject(change(good,"\"weight\":0.5","\"weight\":1e999"));
    Reject(change(good,"\"weight\":0.5","\"weight\":0.50000001"));
    Reject(change(good,"GEOMETRIC","OTHER"));Reject(change(good,std::string(64,'c'),"abc"));Reject(change(good,"眉と目",std::string("\xc0\xaf",2)));
    Reject(good.substr(0,good.size()-1));Reject(std::string(256*1024+1,' '));
    Reject(change(good,"\"source\":\"眉と目_完全な名前_0\"","\"source\":\"\""));
    Reject(change(good,"\"source\":\"眉と目_完全な名前_0\"","\"source\":\""+std::string(4097,'x')+"\""));
    Reject(change(good,"\"model_sha256\":", "\"unexpected\":0,\"model_sha256\":"));
    Reject(change(good,"[0,0.25,0.5,0.75,1,0.75,0.5,0.25,0]","[0,0.5,1]"));
    auto c=LoadCatalog(good);auto ids=Ids(489);ids.back()="absent";bool rejected=false;try{(void)BuildWeights(c,0,1,ids);}catch(const std::exception&){rejected=true;}Check(rejected,"missing live ID rejected");
}
}
int main(){try{TestSourceLabelDiffersFromId();TestFullWeightsAndCaseIsolation();TestReverseSteps();TestMalformed();std::printf("PASS: %u catalog checks\n",checks);return 0;}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
