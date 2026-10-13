#pragma once
namespace privateprobe {
#include "RuntimeHelpers.hpp"
struct Buffer {
    unsigned bytes=0;
    std::uintptr_t wrapper=0,backing=0,view=0,resource=0;
    std::uint64_t initialization=0;
};
bool Contract(Buffer& b,bool initialize=false) noexcept {
    std::uintptr_t table=0,backing=0,view=0,resource=0,mapped=0,poolOffset=0;unsigned bytes=0,mode=9,refs=0;
    unsigned char persistent=1;std::uint64_t stamp=0;
    if(!observer::Read(b.wrapper,table)||table!=observer::imageBase+observed::vertexVtableRva||
       !observer::Read(b.wrapper+0x48,refs)||refs==0||!observer::Read(b.wrapper+0x5C,bytes)||bytes!=b.bytes||
       !observer::Read(b.wrapper+0x58,mode)||mode!=0||!observer::Read(b.wrapper+0x60,backing)||
       !observer::Read(backing,table)||table!=observer::imageBase+0x3539948||
       !observer::Read(backing+0x10,bytes)||bytes!=b.bytes||!observer::Read(backing+0x14,mode)||mode!=0||
       !observer::Read(backing+0x20,resource)||resource<=0xFFFF||!observer::Read(backing+0x28,poolOffset)||poolOffset!=0||
       !observer::Read(backing+0x38,mapped)||mapped!=0||!observer::Read(backing+0x6C,persistent)||persistent!=0||
       !observer::Read(b.wrapper+0x78,view)||view<=0xFFFF||!observer::Read(backing+0x78,stamp)||stamp==~std::uint64_t{0})return false;
    if(initialize){b.backing=backing;b.view=view;b.resource=resource;b.initialization=stamp;return true;}
    return backing==b.backing&&view==b.view&&resource==b.resource&&stamp==b.initialization;
}
bool InterfaceGuard(std::uintptr_t pointer) noexcept {
    const auto module=reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(L"D3D12Core.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};std::uintptr_t table=0;
    if(!module||pointer<=0xFFFF||!observer::Read(module,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>4096||
       !observer::Read(module+static_cast<unsigned>(dos.e_lfanew),nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.OptionalHeader.SizeOfImage<72||nt.OptionalHeader.SizeOfImage>16*1024*1024||!observer::Read(pointer,table)||
       table<module||table-module>nt.OptionalHeader.SizeOfImage-72)return false;
    std::array<std::uintptr_t,9> methods{};if(!observer::Read(table,methods))return false;
    for(const auto address:methods) {
        MEMORY_BASIC_INFORMATION region{};
        if(address<module||address-module>=nt.OptionalHeader.SizeOfImage||
           ::VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))!=sizeof(region)||region.State!=MEM_COMMIT||
           (region.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))==0||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))!=0)return false;
    }
    return true;
}
bool ConfiguredBufferSize(std::size_t bytes) noexcept {
    if(!bytes||bytes>64u*1024u*1024u)return false;
    for(const auto& profile:equipmentnative::profiles)
        if(profile.configured&&bytes==profile.vertexBytes)return true;
    return false;
}
bool CopyMapped(void* destination,const void* source,std::size_t bytes) noexcept {
    const auto start=reinterpret_cast<std::uintptr_t>(destination);auto position=start;
    if(start<=0xFFFF||!ConfiguredBufferSize(bytes)||start>0x7FFFFFFFFFFF-bytes)return false;
    while(position<start+bytes) {
        MEMORY_BASIC_INFORMATION region{};
        if(::VirtualQuery(reinterpret_cast<void*>(position),&region,sizeof(region))!=sizeof(region)||region.State!=MEM_COMMIT||
           (region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))==0||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))!=0)return false;
        const auto end=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
        if(end<=position)return false;position=end;
    }
    __try {std::memcpy(destination,source,bytes);return std::memcmp(destination,source,bytes)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool StopRequested() noexcept {
    return (::GetAsyncKeyState(VK_CONTROL)&0x8000)&&(::GetAsyncKeyState(VK_SHIFT)&0x8000)&&(::GetAsyncKeyState(VK_F11)&0x8000);
}
struct UploadFence {
    frameobserver::FenceReader reader;
    std::uintptr_t renderer=0,pointer=0;
    std::uint64_t completed=0;std::uint64_t polls=0;
    bool Identity() {
        std::uintptr_t actual=0;
        return Renderer()==renderer&&observer::Read(renderer+0x1158,actual)&&actual==pointer;
    }
    void Poll() {
        if(polls==UINT64_MAX||!Identity()||!reader.Poll(completed)||!Identity())throw std::runtime_error("Upload Fence invalid/budget exhausted");
        polls++;
    }
    void Attach() {
        renderer=Renderer();
        if(!observer::Read(renderer+0x1158,pointer)||!InterfaceGuard(pointer)||
           !reader.Attach(reinterpret_cast<ID3D12Fence*>(pointer))||
           !InterfaceGuard(reinterpret_cast<std::uintptr_t>(reader.Retained())))throw std::runtime_error("Upload Fence interface");
        Poll();
    }
    void Wait(std::uint64_t stamp) {
        const auto deadline=::GetTickCount64()+5000;
        do {
            if(StopRequested())throw std::runtime_error("Manual stop");
            Poll();if(completed>=stamp)return;
            ::Sleep(25);
        }while(::GetTickCount64()<deadline);
        throw std::runtime_error("Upload Fence timeout");
    }
};
}
