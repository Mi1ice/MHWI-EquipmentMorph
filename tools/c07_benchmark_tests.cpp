#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <stdexcept>
#if __has_include("C07Benchmark.hpp")
#include "C07Benchmark.hpp"
#endif
int main(){try{
#if !__has_include("C07Benchmark.hpp")
    volatile bool missing=true;if(missing)throw std::runtime_error("C07 benchmark unavailable");
#else
    mhm::Asset a;a.model="test";a.basis.resize(24);mhm::Key k{};strcpy_s(k.info.id,"one");k.info.maximum=1;k.deltas.push_back({0,1,0,0});a.keys.push_back(k);
    auto expanded=c07bench::Expand(a,1024);
    if(a.keys.size()!=1||expanded.keys.size()!=1024||!mhm::Validate(expanded)||c07bench::Logical(expanded)<24+1024*16)throw std::runtime_error("synthetic expansion bounds/identity");
    for(unsigned i=0;i<1024;i++)for(unsigned j=0;j<i;j++)if(!std::strcmp(expanded.keys[i].info.id,expanded.keys[j].info.id))throw std::runtime_error("duplicate synthetic IDs");
    bool rejected=false;try{c07bench::Expand(a,1025);}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("1025 must reject");
    auto weights=c07bench::Weights(expanded,30,true,30);if(std::count_if(weights.begin(),weights.end(),[](float w){return w!=0;})!=30)throw std::runtime_error("active key count");
    if(std::count_if(weights.begin(),weights.end(),[](float w){return std::abs(w-.15f)<1e-6f;})!=5)throw std::runtime_error("five deterministic varying keys");
    std::vector<unsigned char> out;if(!mhm::Compose(expanded,c07bench::Weights(expanded,0,false,0),out)||out!=a.basis)throw std::runtime_error("zero basis exact");
    printf("PASS 6 C07 benchmark checks (microfixture only for correctness, never performance evidence)\n");
#endif
    return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}}
