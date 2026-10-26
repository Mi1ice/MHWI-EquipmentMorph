#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../runtime/EquipmentMorphDiskConfiguration/mesh_contract.hpp"
#include "../diagnostics/EquipmentMorphDiskContract/VertexDeclarations.hpp"
#include "fixtures/vertex_declaration_memory.hpp"
#include <cstdio>
#include <functional>
#include <stdexcept>
using captureddeclarations::Memory;
using captureddeclarations::Put;
using captureddeclarations::Address;
struct Reader {
    uintptr_t watch=0;std::function<void()> action;
    bool operator()(uintptr_t address,void* output,size_t bytes){
        SIZE_T copied=0;const bool ok=::ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,bytes,&copied)&&copied==bytes;
        if(ok&&watch==address&&action){auto once=std::move(action);once();}return ok;
    }
};
int main(){try{
    unsigned count=0;auto check=[&](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);count++;};
    Reader read;Memory m;mhm::declarations::Snapshot snapshot;
    check(mhm::declarations::Capture(read,m.Slot(),snapshot),"captured complete semantic templates accepted");
    std::vector<unsigned char> rows(14*80);std::vector<uint32_t> expected;
    for(size_t i=0;i<14;i++){const auto& r=captureddeclarations::records[i];rows[i*80+14]=static_cast<unsigned char>(r.stride);Put(rows,i*80+24,r.index);expected.push_back(r.disk);}
    check(mhm::meshcontract::FormatsMatch(rows,expected,snapshot),"all fourteen resolve through captured directory");
    for(uint32_t bad:{0xffffu,45u,UINT32_MAX,13u}){auto changed=rows;Put(changed,24,bad);check(!mhm::meshcontract::FormatsMatch(changed,expected,snapshot),"unknown and invalid native index rejected");}
    auto changed=rows;Put(changed,80+24,2u);check(!mhm::meshcontract::FormatsMatch(changed,expected,snapshot),"same stride different format refused");
    changed=rows;changed[14]++;check(!mhm::meshcontract::FormatsMatch(changed,expected,snapshot),"wrong model stride refused");
    check(!mhm::meshcontract::FormatsMatch(std::span(rows).first(rows.size()-1),expected,snapshot),"partial model table refused");
    auto reject=[&](auto modify,const char* reason){Memory bad;modify(bad);mhm::declarations::Snapshot out=snapshot;check(!mhm::declarations::Capture(read,bad.Slot(),out)&&out.slot==0,reason);};
    reject([](auto& b){b.pointer=0;},"null root refused");
    reject([](auto& b){b.pointer=UINTPTR_MAX-4;},"overflowing root refused");
    reject([](auto& b){Put(b.root,56,uintptr_t{1});},"bad metadata refused");
    reject([](auto& b){Put(b.metadata,256,4097u);},"excessive count refused");
    reject([](auto& b){Put(b.metadata,256,0u);},"empty count refused");
    reject([](auto& b){Put(b.root,9492,44u);},"inconsistent runtime count refused");
    reject([](auto& b){Put(b.metadata,200,uintptr_t{0x10000});},"unreadable pointer array refused");
    reject([](auto& b){Put(b.array,0,uintptr_t{0});},"null record refused");
    reject([](auto& b){Put(b.nativeArray,0,UINTPTR_MAX-4);},"overflowing runtime record refused");
    reject([](auto& b){Put(b.source[1],12,0xa756f2f9u);},"duplicate hash refused");
    reject([](auto& b){Put(b.native[0],76,0xa5104ca0u);},"native hash disagreement refused");
    reject([](auto& b){Put(b.native[0],116,28u);},"wrong declaration stride refused");
    reject([](auto& b){Put(b.source[0],18,uint16_t{28});},"wrong source declaration stride refused");
    reject([](auto& b){Put(b.source[0],16,uint16_t{12});},"wrong source element count refused");
    reject([](auto& b){Put(b.native[0],112,12u);},"wrong native element count refused");
    reject([](auto& b){Put(b.native[0],104,uintptr_t{0});},"null element pointer refused");
    reject([](auto& b){b.loaded[0][8]^=1;},"different runtime element copy refused");
    reject([](auto& b){b.source[0][32]^=1;b.loaded[0][8]^=1;},"same stride wrong packed layout refused");
    reject([](auto& b){b.source[0][36]=1;b.loaded[0][12]=1;},"reserved fields checked");
    reject([](auto& b){b.names[0][0]='X';},"semantic name checked");
    reject([](auto& b){Put(b.source[0],24,uintptr_t{0x10000});Put(b.loaded[0],0,uintptr_t{0x10000});},"unreadable semantic pointer refused");
    Memory reordered;for(auto* a:{&reordered.array,&reordered.nativeArray}){
        const auto first=mhm::declarations::Value<uintptr_t>(a->data(),0),second=mhm::declarations::Value<uintptr_t>(a->data(),8);Put(*a,0,second);Put(*a,8,first);
    }
    mhm::declarations::Snapshot other;check(mhm::declarations::Capture(read,reordered.Slot(),other),"valid reordered declarations accepted");
    changed=rows;Put(changed,24,1u);Put(changed,104,0u);check(mhm::meshcontract::FormatsMatch(changed,expected,other),"indices are discovered rather than enumerated");
    for(unsigned kind=0;kind<4;kind++){
        Memory switching;Reader mutate;mutate.watch=Address(switching.source[0]);mutate.action=[&]{
            if(kind==0)switching.pointer=0;
            if(kind==1)Put(switching.metadata,256,44u);
            if(kind==2)Put(switching.array,0,Address(switching.source[1]));
            if(kind==3)switching.loaded[0][8]^=1;
        };
        mhm::declarations::Snapshot out;check(!mhm::declarations::Capture(mutate,switching.Slot(),out),"table or element switch during capture refused");
    }
    check(snapshot.Stable(read),"unchanged generation reusable");Put(m.array,0,Address(m.source[1]));check(!snapshot.Stable(read),"cached snapshot invalidated by pointer array switch");
    std::printf("PASS: %u declaration snapshot checks; sanitized captured fixture, no game\n",count);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
