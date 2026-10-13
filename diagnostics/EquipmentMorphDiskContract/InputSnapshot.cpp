// Candidate Stracker DLL. Loading it installs three call-site observers: it is not
// an external read-only probe. No vertex replacement, GPU calls or model writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "safetyhook.hpp"
#include "ObservedBuild.hpp"
#include "NativeProfiles.hpp"

namespace observer {
constexpr unsigned capacity=256;
std::uintptr_t imageBase=0;
std::atomic<bool> capturing=false;
std::atomic<unsigned> inFlight=0,next=0,rejected=0;
struct Event {
    unsigned part=0,kind=0,thread=0,meshIndex=0,lod=0xFFFFFFFF,auxEnabled=0;
    std::uint64_t qpc=0;
    std::uintptr_t owner=0,model=0,context=0,mesh=0,input=0,originalInput=0,backing=0;
    std::uintptr_t ownerStreams[3]{},drawStreams[3]{};
    unsigned inputBytes=0,bindingIndex=0,bindingAux=0,contextFlags=0;
    unsigned groups[3]{};
    std::uintptr_t contextObject=0;
    unsigned contextObjectFlags=0;
    std::array<unsigned char,80> meshBytes{};
};
struct Slot {Event event{};std::atomic<bool> ready=false;};
std::array<Slot,capacity> slots{};
// Intentionally retained for process lifetime. No hot unload or trampoline free
// while another thread may be returning from a callback.
safetyhook::MidHook* computeHook=nullptr;
safetyhook::MidHook* drawHook=nullptr;
safetyhook::MidHook* dispatchHook=nullptr;

bool Read(std::uintptr_t address,void* destination,std::size_t bytes) noexcept {
    SIZE_T read=0;
    return address>0xFFFF&&::ReadProcessMemory(::GetCurrentProcess(),reinterpret_cast<void*>(address),
        destination,bytes,&read)&&read==bytes;
}
template<class T> bool Read(std::uintptr_t address,T& value) noexcept {return Read(address,&value,sizeof(value));}
bool Snapshot(const safetyhook::Context& ctx,unsigned kind,Event& e) noexcept {
    e.kind=kind;e.owner=kind==1?ctx.rdi:ctx.r15;e.mesh=kind==2?ctx.rsi:ctx.r13;
    e.context=ctx.rcx;e.input=ctx.r8;e.bindingIndex=static_cast<unsigned>(ctx.rdx);
    e.bindingAux=static_cast<unsigned>(ctx.r9);
    if(kind==3) {
        // Entry saves RCX at [entry RSP+8]; decoded prologue puts that at RBP+D0.
        // This frame slot is also read as owner after dispatch at RVA222D917.
        if(!Read(ctx.rbp+0xD0,e.owner)||!Read(imageBase+observed::sourceBindingIdRva,e.bindingIndex)||
            e.bindingIndex==0xFFFFFFFF||!Read(e.context+0x710+(e.bindingIndex&0xFFF)*8,e.input))return false;
        e.groups[0]=static_cast<unsigned>(ctx.rdx);e.groups[1]=static_cast<unsigned>(ctx.r8);e.groups[2]=static_cast<unsigned>(ctx.r9);
    }
    std::uintptr_t table=0,meshBase=0;
    unsigned count=0,bytes=0;
    std::array<char,80> path{};
    if(!Read(e.owner,table)||!Read(e.owner+0x2A0,e.model)||!Read(e.model+0xC,path))return false;
    e.part=5;
    for(const auto& profile:equipmentnative::profiles){
        const auto expectedOwner=profile.part==MHM_BODY?observed::ownerVtableRva:0x34A7560;
        if(profile.configured&&table==imageBase+expectedOwner
           &&profile.path.size()<path.size()
           &&std::memcmp(path.data(),profile.path.c_str(),profile.path.size()+1)==0){e.part=profile.part;break;}
    }
    if(e.part>=equipmentnative::profiles.size())return false;
    const auto& profile=equipmentnative::profiles[e.part];
    const unsigned expectedBytes=profile.vertexBytes,expectedCount=profile.meshCount;
    const char* expectedPath=profile.path.c_str();
    if(
        !Read(e.owner+0x2A0,e.model)||!Read(e.model,table)||table!=imageBase+observed::modelVtableRva||
        !Read(e.model+0xC,path)||std::memcmp(path.data(),expectedPath,std::strlen(expectedPath)+1)!=0||
        !Read(e.model+0x100,bytes)||bytes!=expectedBytes||
        !Read(e.model+0xC8,meshBase)||!Read(e.model+0xD0,count)||count!=expectedCount||
        e.mesh<meshBase||(e.mesh-meshBase)%80!=0||(e.mesh-meshBase)/80>=count||
        !Read(e.mesh,e.meshBytes)||!Read(e.model+0x110,e.originalInput)||
        !Read(e.input,table)||table!=imageBase+observed::vertexVtableRva||
        !Read(e.input+0x5C,e.inputBytes)||e.inputBytes!=expectedBytes||!Read(e.input+0x60,e.backing)||
        !Read(e.owner+0x430,e.ownerStreams))return false;
    std::uintptr_t bridge=0,linkedOwner=0,bodyUnit=0,cachedModel=0;
    if(e.part==MHM_BODY&&(!Read(e.owner+0x126C8,bridge)||!Read(bridge,table)||table!=imageBase+0x324A830||
        !Read(bridge+0x60,linkedOwner)||linkedOwner!=e.owner||!Read(bridge+0xF0,bodyUnit)||bodyUnit!=e.owner||
        !Read(bridge+0x480,cachedModel)||cachedModel!=e.model))return false;
    e.meshIndex=static_cast<unsigned>((e.mesh-meshBase)/80);
    if(!Read(e.context+0x150,e.contextFlags)||!Read(e.context+0x1D0,e.contextObject))return false;
    unsigned char contextFlag=0;
    if(e.contextObject&&!Read(e.contextObject+0x58,contextFlag))return false;
    e.contextObjectFlags=contextFlag;
    if(kind==2) {
        unsigned char flag=0;
        if(!Read(ctx.rdi+0x58,e.lod)||!Read(ctx.rdi+0x87,flag)||
            !Read(ctx.rdi+0x18,e.drawStreams))return false;
        e.auxEnabled=flag;
    }
    std::uintptr_t after=0;
    if(!Read(e.owner+0x2A0,after)||after!=e.model||!Read(e.model+0x110,after)||after!=e.originalInput)return false;
    if(e.part==MHM_BODY&&(!Read(e.owner+0x126C8,after)||after!=bridge||!Read(bridge+0x480,after)||after!=e.model))return false;
    LARGE_INTEGER q{};::QueryPerformanceCounter(&q);e.qpc=static_cast<std::uint64_t>(q.QuadPart);
    e.thread=::GetCurrentThreadId();return true;
}
void Capture(const safetyhook::Context& ctx,unsigned kind) noexcept {
    if(!capturing.load(std::memory_order_acquire))return;
    inFlight.fetch_add(1,std::memory_order_acq_rel);
    if(capturing.load(std::memory_order_acquire)) {
        Event e{};
        if(Snapshot(ctx,kind,e)) {
            unsigned index=next.fetch_add(1,std::memory_order_relaxed);
            if(index<capacity){slots[index].event=e;slots[index].ready.store(true,std::memory_order_release);}
        } else rejected.fetch_add(1,std::memory_order_relaxed);
    }
    inFlight.fetch_sub(1,std::memory_order_release);
}
void Compute(safetyhook::Context& ctx) {Capture(ctx,1);}
void Draw(safetyhook::Context& ctx) {Capture(ctx,2);}
void Dispatch(safetyhook::Context& ctx) {Capture(ctx,3);}

std::expected<safetyhook::MidHook,safetyhook::MidHook::Error> CreateDisabled(
    void* target,safetyhook::MidHookFn callback,std::span<const unsigned char> expected) {
    std::vector<unsigned char> bytes(expected.size());
    if(expected.size()<5||expected.size()>32||!Read(reinterpret_cast<std::uintptr_t>(target),bytes.data(),bytes.size())||
        !std::equal(bytes.begin(),bytes.end(),expected.begin()))
        return std::unexpected(safetyhook::MidHook::Error::bad_inline_hook(safetyhook::InlineHook::Error::unsupported_instruction_in_trampoline(nullptr)));
    return safetyhook::MidHook::create(target,callback,safetyhook::MidHook::StartDisabled);
}

}
