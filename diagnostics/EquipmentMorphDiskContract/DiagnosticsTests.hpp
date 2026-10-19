#pragma once
namespace diagnosticstest {
struct Log {
    std::string text;bool accept=true;
    bool Write(const char* value){text+=value;return accept;}
};
struct UnlockedLog : Log {
    bool unlocked=true;
    bool Write(const char* value){
        std::thread other([&](){if(facecontinuous::gate.try_lock())facecontinuous::gate.unlock();else unlocked=false;});other.join();
        return Log::Write(value);
    }
};
struct Resource : ID3D12Resource {
    ULONG refs=1;unsigned queries=0,releases=0,descriptions=0,heaps=0;
    HRESULT queryResult=S_OK,heapResult=S_OK;bool descriptionThrows=false;
    D3D12_RESOURCE_DESC description{};D3D12_HEAP_PROPERTIES heap{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out) override {queries++;*out=nullptr;if(FAILED(queryResult))return queryResult;*out=static_cast<ID3D12Resource*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {releases++;return --refs;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Map(UINT,const D3D12_RANGE*,void**) override {return E_NOTIMPL;}
    void STDMETHODCALLTYPE Unmap(UINT,const D3D12_RANGE*) override {}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override {descriptions++;if(descriptionThrows)::RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return description;}
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override {return 0;}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT,const D3D12_BOX*,const void*,UINT,UINT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*,UINT,UINT,UINT,const D3D12_BOX*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES* out,D3D12_HEAP_FLAGS* flags) override {heaps++;*out=heap;*flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;return heapResult;}
};
inline std::uintptr_t acceptedResource=0;
inline bool rejectSource=false,rejectResource=false;
inline bool Validate(std::uintptr_t value,unsigned count){return value==acceptedResource&&!(count==3?rejectSource:rejectResource);}
inline unsigned Run(const std::filesystem::path& path){
    using namespace inputtiming;
    unsigned checks=0;auto check=[&](bool ok,const char* name){if(!ok)throw std::runtime_error(name);checks++;};
    Sample sample(10);sample.Move(Phase::Held,20);sample.Move(Phase::Read,35);sample.Move(Phase::Wait,65);sample.Move(Phase::Held,70);
    sample.Move(Phase::Read,75);sample.Move(Phase::Wait,85);sample.Move(Phase::Held,90);Totals total;sample.Record(total,100);
    check(total.samples==1&&total.total==90&&total.wait==20&&total.held==30&&total.read==40&&total.maxTotal==90&&total.maxWait==20,"retry timing phases partition sampled elapsed time");
    Sample invalid(10);invalid.Move(Phase::Held,9);invalid.Record(total,100);
    check(total.invalid==1&&total.samples==1&&total.total==90,"backwards QPC is excluded without changing the valid totals");
    total.calls=UINT64_MAX;Add(total.calls,1,total.saturated);
    check(total.calls==UINT64_MAX&&total.saturated,"diagnostic totals saturate instead of wrapping");
    Sampler sampler{1};unsigned hits=0;for(unsigned i=0;i<64000;i++)hits+=sampler.Next()?1:0;
    check(hits>800&&hits<1200,"pseudorandom sampling stays near the nominal one in sixty-four rate");
    sampler.state=0;sampler.Next();check(sampler.state!=0,"zero sampler seed cannot disable sampling forever");
    metrics={};metrics[Index(1,5,Route::Other)]=total;const auto taken=Take();
    check(taken[Index(1,5,Route::Other)].calls==UINT64_MAX&&metrics[Index(1,5,Route::Other)].calls==0,"window snapshot resets only recorded aggregates");
    Log log;Write(log,taken,UINT64_MAX,UINT64_MAX);
    check(log.text.find("\"saturated\":true")!=std::string::npos&&log.text.back()=='\n',"maximum diagnostic values fit a complete JSON record");
    log.text.clear();Write(log,Window{},1000,10000000);check(log.text.empty(),"empty windows do not emit empty records");
    log.accept=false;bool rejected=false;try{Write(log,taken,1,1);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"timing write failures are reported by the worker");
    {
        inputfasttest::Fixture f;metrics={};samplers[0].state=1;qpcCalls=0;
        for(unsigned i=0;i<256;i++)inputfasttest::Need(f.Input(1)==0);
        const auto& zero=metrics[Index(1,MHM_BODY,Route::Original)];
        check(zero.calls==256&&zero.samples>0&&qpcCalls==zero.samples*3,"zero callbacks count all calls and read QPC only for sampled calls");
        check(f.Input(2)==0&&f.Input(3)==1,"diagnostics retain original-input native read counts");
        f.Request(.6f,1);f.Queue();inputfasttest::Need(f.Adapter().Completed(20,100));f.Cycle();
        metrics={};samplers[0].state=1;qpcCalls=0;
        for(unsigned i=0;i<256;i++)inputfasttest::Need(f.Input(1,true)==38);
        const auto guarded=metrics[Index(1,MHM_BODY,Route::Guarded)];
        check(guarded.calls==256&&guarded.samples>0&&qpcCalls==guarded.samples*6,"guarded callbacks sample both locks and unlocked reads without extra native reads");
        check(guarded.wait+guarded.held+guarded.read==guarded.total&&guarded.invalid==0,"real callback sections sum to sampled total time");
        for(unsigned i=0;i<256;i++)inputfasttest::Need(f.Input(3,true)==41);
        check(f.Input(2,true)==39,"draw read count is unchanged by timing");
        f.Cycle();check(f.Adapter().Live(),"sampled compute-dispatch coverage preserves a live next cycle");
        auto c=f.Context(1);c.rdi=0x12345;facecontinuous::Input(c,1);
        check(metrics[Index(1,5,Route::Other)].calls==1,"unrelated owners stay in a separate bounded bin");
        UnlockedLog unlocked;facecontinuous::WriteInputTiming(unlocked,1000,10000000);
        check(unlocked.unlocked&&!unlocked.text.empty()&&metrics[Index(1,MHM_BODY,Route::Guarded)].calls==0,"worker logging occurs after releasing the callback gate");
        f.Input(1,true);check(metrics[Index(1,MHM_BODY,Route::Guarded)].calls==1,"new callbacks remain in the next diagnostic window");
    }
    qpcCalls=0;check(!Begin(0)&&!Begin(4)&&qpcCalls==0,"invalid kinds never sample or index outside the fixed window");
    Resource resource;acceptedResource=reinterpret_cast<std::uintptr_t>(&resource);rejectSource=rejectResource=false;
    resource.description.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;resource.description.Width=4096;resource.description.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource.heap.Type=D3D12_HEAP_TYPE_DEFAULT;resource.heap.CreationNodeMask=resource.heap.VisibleNodeMask=1;
    auto properties=resourceinfo::Read(&resource,Validate);
    Log resourceLog;privateprobe::Buffer buffer;buffer.bytes=4096;buffer.resource=acceptedResource;
    resourceinfo::Write(resourceLog,MHM_BODY,7,0,buffer,properties);
    check(properties.descriptionAvailable&&properties.heapAvailable&&properties.heap.Type==D3D12_HEAP_TYPE_DEFAULT&&resource.refs==1&&resource.releases==1,"owned default-resource query balances its temporary interface reference");
    resource.heap.Type=D3D12_HEAP_TYPE_UPLOAD;properties=resourceinfo::Read(&resource,Validate);
    check(properties.heap.Type==D3D12_HEAP_TYPE_UPLOAD&&properties.description.Width==4096&&resource.refs==1,"heap type is read from the interface rather than inferred from native mode");
    rejectSource=true;const auto queries=resource.queries;properties=resourceinfo::Read(&resource,Validate);
    check(resource.queries==queries&&std::strcmp(properties.status,"source_guard_failed")==0,"source validation failure performs no COM calls");
    rejectSource=false;rejectResource=true;const auto descriptions=resource.descriptions;properties=resourceinfo::Read(&resource,Validate);
    check(resource.refs==1&&resource.descriptions==descriptions&&std::strcmp(properties.status,"resource_guard_failed")==0,"resource validation failure releases a guarded temporary interface without reading properties");
    rejectResource=false;resource.queryResult=E_NOINTERFACE;properties=resourceinfo::Read(&resource,Validate);
    check(resource.refs==1&&!properties.descriptionAvailable&&std::strcmp(properties.status,"query_failed")==0,"unsupported resource interfaces remain unavailable");
    resource.queryResult=S_OK;resource.heapResult=E_INVALIDARG;properties=resourceinfo::Read(&resource,Validate);
    check(properties.descriptionAvailable&&!properties.heapAvailable&&properties.heapResult==E_INVALIDARG&&resource.refs==1,"reserved-resource heap failure retains its valid description");
    resourceinfo::Write(resourceLog,MHM_BODY,7,0,buffer,properties);
    check(resourceLog.text.find("\"heap\":null")!=std::string::npos&&resourceLog.text.find("\"resource_state\":null,\"residency\":null,\"gpu_timing_available\":false")!=std::string::npos,"resource logs explicitly preserve unknown heap, state, residency and GPU timing");
    resource.descriptionThrows=true;const auto heaps=resource.heaps;properties=resourceinfo::Read(&resource,Validate);
    check(!properties.descriptionAvailable&&resource.heaps==heaps&&resource.refs==1&&std::strcmp(properties.status,"description_unavailable")==0,"property access exceptions do not query more methods or retain a reference");
    check(!resourceinfo::Guard(0,3)&&!resourceinfo::Guard(1,15)&&!resourceinfo::Guard(acceptedResource,16),"invalid diagnostic interface ranges are rejected");
    privateprobe::Log output;output.file=::CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(output.file!=INVALID_HANDLE_VALUE,"diagnostic regression log opens");
    log.accept=true;log.text.clear();Write(log,taken,UINT64_MAX,UINT64_MAX);
    check(output.Write(log.text.c_str())&&output.Write(resourceLog.text.c_str())&&output.Flush(),"timing and resource JSON regression records are written completely");
    metrics={};return checks;
}
}
