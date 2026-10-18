#pragma once
namespace resourceinfo {
struct Properties {
    const char* status="unavailable";
    HRESULT query=E_PENDING,heapResult=E_PENDING;
    bool descriptionAvailable=false,heapAvailable=false;
    D3D12_RESOURCE_DESC description{};
    D3D12_HEAP_PROPERTIES heap{};
    D3D12_HEAP_FLAGS heapFlags=D3D12_HEAP_FLAG_NONE;
};
// Accept only interfaces whose entire used vtable is in the already verified
// runtime module. Proxies/other modules stay unavailable; diagnostics never
// relax the native buffer contract or invalidate a working binding.
inline bool Guard(std::uintptr_t pointer,unsigned count) noexcept {
    if(pointer<=0xFFFF||(count!=3&&count!=15))return false;
    const auto base=reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(L"D3D12Core.dll"));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};std::uintptr_t table=0;
    if(!base||!observer::Read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096
        ||!observer::Read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE
        ||nt.OptionalHeader.SizeOfImage<count*8||nt.OptionalHeader.SizeOfImage>16*1024*1024
        ||!observer::Read(pointer,table)||table<base||table-base>nt.OptionalHeader.SizeOfImage-count*8)return false;
    std::array<std::uintptr_t,15> methods{};
    if(count==3){std::array<std::uintptr_t,3> unknown{};if(!observer::Read(table,unknown))return false;std::copy(unknown.begin(),unknown.end(),methods.begin());}
    else if(!observer::Read(table,methods))return false;
    for(unsigned i=0;i<count;i++){
        MEMORY_BASIC_INFORMATION page{};
        if(methods[i]<base||methods[i]-base>=nt.OptionalHeader.SizeOfImage||!::VirtualQuery(reinterpret_cast<void*>(methods[i]),&page,sizeof(page))
            ||page.State!=MEM_COMMIT||(page.Protect&(PAGE_GUARD|PAGE_NOACCESS))
            ||!(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    }
    return true;
}
inline HRESULT Query(IUnknown* source,ID3D12Resource** result) noexcept {
    __try{return source->QueryInterface(__uuidof(ID3D12Resource),reinterpret_cast<void**>(result));}
    __except(EXCEPTION_EXECUTE_HANDLER){return E_FAIL;}
}
inline bool Describe(ID3D12Resource* resource,Properties& properties) noexcept {
    __try{properties.description=resource->GetDesc();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline HRESULT Heap(ID3D12Resource* resource,Properties& properties) noexcept {
    __try{return resource->GetHeapProperties(&properties.heap,&properties.heapFlags);}
    __except(EXCEPTION_EXECUTE_HANDLER){return E_FAIL;}
}
inline void Release(ID3D12Resource* resource) noexcept {
    __try{resource->Release();}__except(EXCEPTION_EXECUTE_HANDLER){}
}
using Validator=bool(*)(std::uintptr_t,unsigned);
inline Properties Read(IUnknown* source,Validator validate=Guard) noexcept {
    Properties properties;ID3D12Resource* resource=nullptr;
    if(!validate(reinterpret_cast<std::uintptr_t>(source),3)){properties.status="source_guard_failed";return properties;}
    properties.query=Query(source,&resource);
    if(FAILED(properties.query)||!resource){properties.status="query_failed";return properties;}
    if(!validate(reinterpret_cast<std::uintptr_t>(resource),15)){
        properties.status="resource_guard_failed";
        if(validate(reinterpret_cast<std::uintptr_t>(resource),3))Release(resource);
        return properties;
    }
    properties.descriptionAvailable=Describe(resource,properties);
    if(properties.descriptionAvailable){properties.heapResult=Heap(resource,properties);properties.heapAvailable=SUCCEEDED(properties.heapResult);}
    properties.status=!properties.descriptionAvailable?"description_unavailable":!properties.heapAvailable?"heap_unavailable":"available";
    Release(resource);return properties;
}
template<class Log> void Write(Log& log,unsigned part,std::uint64_t epoch,unsigned slot,const privateprobe::Buffer& buffer,const Properties& properties){
    char description[500]="null",heap[500]="null",line[1800]{};
    if(properties.descriptionAvailable)std::snprintf(description,sizeof(description),"{\"dimension\":%u,\"width\":%llu,\"alignment\":%llu,\"layout\":%u,\"flags\":%u}",
        static_cast<unsigned>(properties.description.Dimension),properties.description.Width,properties.description.Alignment,static_cast<unsigned>(properties.description.Layout),static_cast<unsigned>(properties.description.Flags));
    if(properties.heapAvailable)std::snprintf(heap,sizeof(heap),"{\"type\":%u,\"cpu_page_property\":%u,\"memory_pool_preference\":%u,\"creation_node_mask\":%u,\"visible_node_mask\":%u,\"flags\":%u}",
        static_cast<unsigned>(properties.heap.Type),static_cast<unsigned>(properties.heap.CPUPageProperty),static_cast<unsigned>(properties.heap.MemoryPoolPreference),properties.heap.CreationNodeMask,properties.heap.VisibleNodeMask,static_cast<unsigned>(properties.heapFlags));
    const auto n=std::snprintf(line,sizeof(line),"{\"event\":\"private_buffer_properties\",\"part\":%u,\"epoch\":%llu,\"slot\":%u,\"wrapper\":\"0x%llX\",\"resource\":\"0x%llX\",\"buffer_bytes\":%u,\"status\":\"%s\",\"query_hresult\":%ld,\"heap_hresult\":%ld,\"description\":%s,\"heap\":%s,\"resource_state\":null,\"residency\":null,\"gpu_timing_available\":false}\n",
        part,epoch,slot,buffer.wrapper,buffer.resource,buffer.bytes,properties.status,properties.query,properties.heapResult,description,heap);
    if(n<0||static_cast<size_t>(n)>=sizeof(line)||!log.Write(line))throw std::runtime_error("Resource properties log");
}
}
