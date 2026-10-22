// Optional in-process GUI; the core remains an independent API provider.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include "Input.hpp"
#include "QueueDiscovery.hpp"
#include "View.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace morphgui {
void Require(HRESULT result){if(FAILED(result))throw std::runtime_error("DX12 resource operation failed");}
struct Renderer {
    struct Frame {ComPtr<ID3D12CommandAllocator> allocator;UINT64 fence=0;};
    Input& input;
    Control& control;View view;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12DescriptorHeap> rtvs,srvs;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    std::array<ComPtr<ID3D12Resource>,8> buffers;
    std::array<Frame,8> frames;
    std::array<bool,64> descriptors{};
    UINT count=0,rtvStep=0,srvStep=0;UINT64 stamp=0,sequence=0;HANDLE completed=nullptr;
    bool win32=false,dx12=false;uint64_t closeVisibility=0;
    Renderer(Input& value,Control& controller):input(value),control(controller){}
    bool Drain(){
        if(!fence||!stamp||fence->GetCompletedValue()>=stamp)return true;
        if(FAILED(fence->SetEventOnCompletion(stamp,completed)))return FAILED(device->GetDeviceRemovedReason());
        return WaitForSingleObject(completed,2000)==WAIT_OBJECT_0||FAILED(device->GetDeviceRemovedReason());
    }
    bool Reset(){
        if(!Drain())return false;
        Input::OwnCalls own;
        if(input.context){ImGui::SetCurrentContext(input.context);if(dx12)ImGui_ImplDX12_Shutdown();if(win32)ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(input.context);input.context=nullptr;}
        dx12=win32=false;for(auto& b:buffers)b.Reset();for(auto& f:frames){f.allocator.Reset();f.fence=0;}
        list.Reset();fence.Reset();rtvs.Reset();srvs.Reset();queue.Reset();device.Reset();
        if(completed)CloseHandle(completed);completed=nullptr;count=0;stamp=sequence=0;descriptors.fill(false);return true;
    }
    void Initialize(IDXGISwapChain3* swap,ID3D12CommandQueue* commands,HWND window){
        Require(swap->GetDevice(IID_PPV_ARGS(&device)));ComPtr<ID3D12Device> queueDevice;Require(commands->GetDevice(IID_PPV_ARGS(&queueDevice)));
        if(device.Get()!=queueDevice.Get()||commands->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("Swapchain queue mismatch");
        queue=commands;DXGI_SWAP_CHAIN_DESC description{};Require(swap->GetDesc(&description));count=description.BufferCount;
        if(count<2||count>buffers.size())throw std::runtime_error("Unsupported backbuffer count");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=count;Require(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvs)));
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=64;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Require(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srvs)));
        rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);srvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<count;i++){
            Require(swap->GetBuffer(i,IID_PPV_ARGS(&buffers[i])));device->CreateRenderTargetView(buffers[i].Get(),nullptr,rtv);rtv.ptr+=rtvStep;
            Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&frames[i].allocator)));
        }
        Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,frames[0].allocator.Get(),nullptr,IID_PPV_ARGS(&list)));Require(list->Close());
        Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));completed=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!completed)throw std::runtime_error("Fence event creation failed");
        IMGUI_CHECKVERSION();input.context=ImGui::CreateContext();ImGui::SetCurrentContext(input.context);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr; // No files beside the game executable.
        io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
        wchar_t windows[32768]{};if(GetWindowsDirectoryW(windows,32768)){
            const auto font=std::filesystem::path(windows)/L"Fonts"/L"msyh.ttc";
            const auto utf8=font.u8string();if(std::filesystem::exists(font))io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()),18.f);
        }
        ui::Style();win32=ImGui_ImplWin32_Init(window);if(!win32)throw std::runtime_error("Win32 backend initialization failed");
        ImGui::GetPlatformIO().Platform_SetImeDataFn=[](ImGuiContext*,ImGuiViewport* viewport,ImGuiPlatformImeData* data){Input::instance->ImeRequest(viewport,data);};
        ImGui_ImplDX12_InitInfo init{};init.Device=device.Get();init.CommandQueue=queue.Get();init.NumFramesInFlight=static_cast<int>(count);init.RTVFormat=description.BufferDesc.Format;
        init.SrvDescriptorHeap=srvs.Get();init.UserData=this;
        init.SrvDescriptorAllocFn=[](ImGui_ImplDX12_InitInfo* info,D3D12_CPU_DESCRIPTOR_HANDLE* cpu,D3D12_GPU_DESCRIPTOR_HANDLE* gpu){
            auto& self=*static_cast<Renderer*>(info->UserData);
            for(UINT i=0;i<self.descriptors.size();i++)if(!self.descriptors[i]){
                self.descriptors[i]=true;*cpu=self.srvs->GetCPUDescriptorHandleForHeapStart();cpu->ptr+=SIZE_T(i)*self.srvStep;
                *gpu=self.srvs->GetGPUDescriptorHandleForHeapStart();gpu->ptr+=UINT64(i)*self.srvStep;return;
            }throw std::runtime_error("GUI descriptor capacity exhausted");
        };
        init.SrvDescriptorFreeFn=[](ImGui_ImplDX12_InitInfo* info,D3D12_CPU_DESCRIPTOR_HANDLE cpu,D3D12_GPU_DESCRIPTOR_HANDLE){
            auto& self=*static_cast<Renderer*>(info->UserData);const auto start=self.srvs->GetCPUDescriptorHandleForHeapStart().ptr;
            if(cpu.ptr>=start&&(cpu.ptr-start)%self.srvStep==0){const auto index=(cpu.ptr-start)/self.srvStep;if(index<self.descriptors.size())self.descriptors[index]=false;}
        };
        dx12=ImGui_ImplDX12_Init(&init);if(!dx12)throw std::runtime_error("DX12 backend initialization failed");
    }
    bool Draw(IDXGISwapChain3* swap,ID3D12CommandQueue* commands,HWND window){
        Input::OwnCalls own;
        if(!count)Initialize(swap,commands,window);
        auto& frame=frames[sequence%count];if(frame.fence&&fence->GetCompletedValue()<frame.fence)return false;
        const auto index=swap->GetCurrentBackBufferIndex();if(index>=count)throw std::runtime_error("Invalid backbuffer index");
        ImGui::SetCurrentContext(input.context);ImGui_ImplDX12_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();input.FrameInput();
        const auto extent=ImGui::GetMainViewport()->WorkSize;
        const float marginX=extent.x>528?24.f:0.f,marginY=extent.y>468?24.f:0.f;
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x+marginX,ImGui::GetMainViewport()->WorkPos.y+marginY),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2((std::min)(860.f,extent.x-2*marginX),(std::min)(720.f,extent.y-2*marginY)),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(480.f,extent.x),(std::min)(420.f,extent.y)),extent);bool opened=true;
        if(input.closeRequested.exchange(false)&&!view.closeTicket)view.closeRequested=true;
        // A focus round trip during restoration must not close a newly reopened panel.
        if(!view.closeTicket)closeVisibility=input.toggles.load();
        if(ImGui::Begin("MHWI EquipmentMorph",&opened,ImGuiWindowFlags_NoCollapse)){
            const auto position=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();const auto* viewport=ImGui::GetMainViewport();
            const auto inside=ContainPanel({viewport->WorkPos.x,viewport->WorkPos.y,extent.x,extent.y},{position.x,position.y,size.x,size.y});
            ImGui::SetWindowPos(ImVec2(inside.x,inside.y));ImGui::SetWindowSize(ImVec2(inside.w,inside.h));
            view.Draw(control);
        }ImGui::End();if(!opened&&!view.closeTicket)view.closeRequested=true;
        if(std::exchange(view.hideReady,false)&&closeVisibility==input.toggles.load())input.RequestClose();ImGui::Render();
        input.CursorRequest(ImGui::GetMouseCursor());
        Require(frame.allocator->Reset());Require(list->Reset(frame.allocator.Get(),nullptr));
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=buffers[index].Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&barrier);auto rtv=rtvs->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(index)*rtvStep;list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        ID3D12DescriptorHeap* heaps[]={srvs.Get()};list->SetDescriptorHeaps(1,heaps);ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),list.Get());
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);Require(list->Close());
        ID3D12CommandList* submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);Require(queue->Signal(fence.Get(),++stamp));frame.fence=stamp;sequence++;return true;
    }
};
struct Association {ComPtr<IUnknown> identity;ComPtr<ID3D12CommandQueue> queue;HWND window=nullptr;};
struct State {
    Input input;Control control;Renderer renderer{input,control};std::array<safetyhook::InlineHook,9> hooks;QueueDiscovery discovery;
    std::array<Association,8> associations{};size_t next=0;std::atomic<IDXGISwapChain*> active=nullptr;ComPtr<ID3D12CommandQueue> queue;
    std::atomic_bool queueReady=false;std::atomic_uintptr_t queueAddress=0;
    std::atomic<const char*> bindingRoute="none";std::atomic_uint64_t creations=0,unmatchedPresents=0;
    std::atomic_int boot=0;std::atomic<const char*> error="none";std::atomic_bool resizing=false;
    std::atomic_uint64_t presents=0,drawn=0,busy=0,resizes=0;
    void Disable(const char* reason){error=reason;input.available=false;if(std::strcmp(reason,"control_failed")==0)control.Post(mhmcontrol::Action::Release);input.RequestClose();}
};
State* state=nullptr;
std::atomic_int publishedBoot=0;
enum {CreateLegacy,CreateWindow,Present,Present1,Resize,Resize1,Execute,Barriers,ResetList};
using CreateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using CreateWindowFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using Resize1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown* const*);
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
using BarriersFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using ResetListFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
void STDMETHODCALLTYPE Submit(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists){
    state->hooks[Execute].original<ExecuteFn>()(queue,count,lists);
    try{state->discovery.Submitted(queue,count,lists);}catch(...){state->Disable("queue_discovery_failed");}
}
void STDMETHODCALLTYPE Transition(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    try{state->discovery.Barriers(list,count,barriers);}catch(...){state->Disable("queue_discovery_failed");}
    state->hooks[Barriers].original<BarriersFn>()(list,count,barriers);
}
HRESULT STDMETHODCALLTYPE ResetCommands(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pipeline){
    state->discovery.Reset(list);return state->hooks[ResetList].original<ResetListFn>()(list,allocator,pipeline);
}
void Remember(IUnknown* device,IDXGISwapChain* swap){
    if(!swap||!device)return;ComPtr<ID3D12CommandQueue> queue;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return;
    DXGI_SWAP_CHAIN_DESC description{};if(FAILED(swap->GetDesc(&description))||!description.OutputWindow)return;
    DWORD pid=0;GetWindowThreadProcessId(description.OutputWindow,&pid);if(pid!=GetCurrentProcessId())return;
    ComPtr<IUnknown> identity;if(FAILED(swap->QueryInterface(IID_PPV_ARGS(&identity))))return;
    std::scoped_lock lock(state->input.gui);auto& slot=state->associations[state->next++%state->associations.size()];slot={identity,queue,description.OutputWindow};state->creations++;
}
HRESULT STDMETHODCALLTYPE CreateSwap(IDXGIFactory* factory,IUnknown* device,DXGI_SWAP_CHAIN_DESC* description,IDXGISwapChain** swap){
    const auto result=state->hooks[CreateLegacy].original<CreateFn>()(factory,device,description,swap);if(SUCCEEDED(result)&&swap)try{Remember(device,*swap);}catch(...){state->Disable("swapchain_capture_failed");}return result;
}
HRESULT STDMETHODCALLTYPE CreateWindowSwap(IDXGIFactory2* factory,IUnknown* device,HWND window,const DXGI_SWAP_CHAIN_DESC1* description,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* output,IDXGISwapChain1** swap){
    const auto result=state->hooks[CreateWindow].original<CreateWindowFn>()(factory,device,window,description,fullscreen,output,swap);if(SUCCEEDED(result)&&swap)try{Remember(device,*swap);}catch(...){state->Disable("swapchain_capture_failed");}return result;
}
void Render(IDXGISwapChain* swap,UINT flags){
    state->presents++;
    if((flags&DXGI_PRESENT_TEST)||!state->input.available.load()||state->resizing.load())return;
    if(swap==state->active&&state->queueReady.load()&&!state->input.visible.load())return; // Hidden: no GUI frame or GPU submission.
    std::scoped_lock lock(state->input.gui);
    if(swap!=state->active){
        DXGI_SWAP_CHAIN_DESC description{};if(FAILED(swap->GetDesc(&description))||!IsWindowVisible(description.OutputWindow))return;
        DWORD pid=0;GetWindowThreadProcessId(description.OutputWindow,&pid);if(pid!=GetCurrentProcessId())return;
        RECT bounds{};GetClientRect(description.OutputWindow,&bounds);if(bounds.right<128||bounds.bottom<128)return;
        if(state->input.window&&state->input.window!=description.OutputWindow)return;
        ComPtr<ID3D12Device> device;if(FAILED(swap->GetDevice(IID_PPV_ARGS(&device))))return;
        ComPtr<IUnknown> identity;Require(swap->QueryInterface(IID_PPV_ARGS(&identity)));
        if(!state->renderer.Reset()){state->Disable("gpu_drain_timeout");return;}
        state->discovery.Clear();state->queue.Reset();state->queueReady=false;state->queueAddress=0;
        for(const auto& binding:state->associations)if(binding.identity.Get()==identity.Get()){state->queue=binding.queue;break;}
        if(!state->input.Attach(description.OutputWindow)){state->Disable("window_subclass_failed");return;}
        state->active=swap;state->bindingRoute=state->queue?"creation":"waiting_for_backbuffer_submission";
        if(!state->queue)state->unmatchedPresents++;
    }
    if(!state->queue){
        state->queue=state->discovery.Take();
        if(state->queue)state->bindingRoute="backbuffer_submission";
        else{if(!state->discovery.Begin(swap))state->Disable("unsupported_backbuffer_discovery");return;}
    }
    state->queueAddress=reinterpret_cast<uintptr_t>(state->queue.Get());state->queueReady=true;
    if(swap!=state->active||!state->input.Capturing())return;
    ComPtr<IDXGISwapChain3> modern;Require(swap->QueryInterface(IID_PPV_ARGS(&modern)));
    if(state->renderer.Draw(modern.Get(),state->queue.Get(),state->input.window))state->drawn++;else state->busy++;
}
inline thread_local bool presenting=false;
struct PresentScope{bool outer=!presenting;PresentScope(){presenting=true;}~PresentScope(){if(outer)presenting=false;}};
void BeforePresent(IDXGISwapChain* swap,UINT flags){
    try{Render(swap,flags);}catch(...){state->Disable("render_failed");}
}
HRESULT STDMETHODCALLTYPE ShowFrame(IDXGISwapChain* swap,UINT interval,UINT flags){
    PresentScope scope;if(scope.outer)BeforePresent(swap,flags);const auto result=state->hooks[Present].original<PresentFn>()(swap,interval,flags);
    if(result==DXGI_ERROR_DEVICE_REMOVED||result==DXGI_ERROR_DEVICE_RESET)state->Disable("device_removed");return result;
}
HRESULT STDMETHODCALLTYPE ShowFrame1(IDXGISwapChain1* swap,UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters){
    PresentScope scope;if(scope.outer)BeforePresent(swap,flags);const auto result=state->hooks[Present1].original<Present1Fn>()(swap,interval,flags,parameters);
    if(result==DXGI_ERROR_DEVICE_REMOVED||result==DXGI_ERROR_DEVICE_RESET)state->Disable("device_removed");return result;
}
inline thread_local bool resizing=false;
struct ResizeScope{bool outer=!resizing;ResizeScope(){resizing=true;state->resizing=true;}~ResizeScope(){if(outer){resizing=false;state->resizing=false;}}};
bool BeforeResize(IDXGISwapChain* swap){
    std::scoped_lock lock(state->input.gui);
    if(swap==state->active){
        if(!state->renderer.Reset()){state->Disable("resize_gpu_timeout");return false;}
        state->discovery.Clear();
        if(std::strcmp(state->bindingRoute.load(),"creation")){state->queue.Reset();state->queueReady=false;state->queueAddress=0;state->bindingRoute="waiting_for_backbuffer_submission";}
        state->resizes++;
    }return true;
}
HRESULT STDMETHODCALLTYPE ChangeSize(IDXGISwapChain* swap,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags){
    ResizeScope scope;try{if(!BeforeResize(swap))return DXGI_ERROR_WAS_STILL_DRAWING;}catch(...){state->Disable("resize_failed");return DXGI_ERROR_WAS_STILL_DRAWING;}
    return state->hooks[Resize].original<ResizeFn>()(swap,count,width,height,format,flags);
}
HRESULT STDMETHODCALLTYPE ChangeSize1(IDXGISwapChain3* swap,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags,const UINT* masks,IUnknown* const* queues){
    ResizeScope scope;try{if(!BeforeResize(swap))return DXGI_ERROR_WAS_STILL_DRAWING;}catch(...){state->Disable("resize_failed");return DXGI_ERROR_WAS_STILL_DRAWING;}
    return state->hooks[Resize1].original<Resize1Fn>()(swap,count,width,height,format,flags,masks,queues);
}
bool Install(HINSTANCE module){
    const wchar_t* className=L"MHWI EquipmentMorph DX12 bootstrap";WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=module;wc.lpszClassName=className;RegisterClassW(&wc);
    HWND window=CreateWindowW(className,L"",WS_OVERLAPPEDWINDOW,0,0,16,16,nullptr,nullptr,module,nullptr);
    if(!window)return false;
    struct WindowCleanup{HWND window;HINSTANCE module;const wchar_t* name;~WindowCleanup(){DestroyWindow(window);UnregisterClassW(name,module);}}cleanup{window,module,className};
    ComPtr<IDXGIFactory4> factory;Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<ID3D12Device> device;auto hr=D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device));
    if(FAILED(hr)){ComPtr<IDXGIAdapter> warp;Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));Require(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));}
    D3D12_COMMAND_QUEUE_DESC queueDescription{};queueDescription.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;Require(device->CreateCommandQueue(&queueDescription,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));Require(list->Close());
    DXGI_SWAP_CHAIN_DESC1 description{};description.Width=16;description.Height=16;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
    description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;description.BufferCount=2;description.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swap;Require(factory->CreateSwapChainForHwnd(queue.Get(),window,&description,nullptr,nullptr,&swap));ComPtr<IDXGISwapChain3> modern;Require(swap.As(&modern));
    auto fv=*reinterpret_cast<void***>(factory.Get()),sv=*reinterpret_cast<void***>(modern.Get()),qv=*reinterpret_cast<void***>(queue.Get()),lv=*reinterpret_cast<void***>(list.Get());
    std::array<void*,9> targets={fv[10],fv[15],sv[8],sv[22],sv[13],sv[39],qv[10],lv[26],lv[10]};
    std::array<void*,9> callbacks={reinterpret_cast<void*>(&CreateSwap),reinterpret_cast<void*>(&CreateWindowSwap),reinterpret_cast<void*>(&ShowFrame),reinterpret_cast<void*>(&ShowFrame1),reinterpret_cast<void*>(&ChangeSize),reinterpret_cast<void*>(&ChangeSize1),reinterpret_cast<void*>(&Submit),reinterpret_cast<void*>(&Transition),reinterpret_cast<void*>(&ResetCommands)};
    for(size_t i=0;i<targets.size();i++){
        auto hook=safetyhook::InlineHook::create(targets[i],callbacks[i],safetyhook::InlineHook::StartDisabled);if(!hook)return false;state->hooks[i]=std::move(*hook);
    }
    if(!state->input.Install())return false;
    for(auto& hook:state->hooks)if(!hook.enable())return false;
    state->input.available=true;return true;
}
DWORD WINAPI Worker(void* value){
    HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&pinned))return 1;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);const auto path=std::filesystem::path(executable);
    if(_wcsicmp(path.filename().c_str(),L"MonsterHunterWorld.exe")&&_wcsicmp(path.filename().c_str(),L"EquipmentMorphImGuiFixture.exe"))return 2;
    state=new State;
    const auto folder=path.parent_path()/L"nativePC"/L"plugins"/L"MHWEquipmentMorph"/L"logs";
    state->control.ConfigurePresets(folder.parent_path()/L"presets");
    std::error_code error;std::filesystem::create_directories(folder,error);
    std::ofstream log(folder/(L"imgui-"+std::to_wstring(GetCurrentProcessId())+L".ndjson"));
    log<<"{\"event\":\"imgui_bootstrap\",\"stage\":11,\"state\":\"starting\"}\n";log.flush();
    try{state->boot=Install(static_cast<HINSTANCE>(value))?1:-1;if(state->boot<0)state->Disable("hook_install_failed");}catch(...){state->boot=-1;state->Disable("bootstrap_failed");}
    publishedBoot.store(state->boot.load());
    uint64_t nextConnect=0,nextLog=0;
    for(;;){
        const auto now=GetTickCount64();
        if(!state->control.Connected()&&now>=nextConnect){
            nextConnect=now+500;HMODULE host=nullptr;
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"MHWEquipmentMorphConfiguredPrototype.dll",&host)){
                using Getter=MhmResult(MHM_CALL*)(uint32_t,uint32_t,MhmApi*);Getter getter=nullptr;
                const auto address=GetProcAddress(host,"MhwMorphGetApi");std::memcpy(&getter,&address,sizeof(getter));MhmApi api{};
                morphpreset::IdentityQuery identity=nullptr;const auto identityAddress=GetProcAddress(host,"MhwMorphGetAssetIdentity");std::memcpy(&identity,&identityAddress,sizeof(identity));
                if(getter&&getter(MHM_API_VERSION,sizeof(api),&api)==MHM_OK)state->control.Connect(api,identity);
            }
        }
        try{state->control.Service(now,state->input.visible.load());}catch(...){state->Disable("control_failed");}
        if(now>=nextLog){
        nextLog=now+2000;const auto controls=state->control.Snapshot();const auto probe=state->input.trace.active.load();
        const auto probeAge=probe?static_cast<uint32_t>(GetTickCount64())-static_cast<uint32_t>(probe>>32):0;
        log<<"{\"event\":\"imgui_status\",\"stage\":11,\"tick\":"<<now<<",\"boot\":"<<state->boot.load()
           <<",\"error\":\""<<state->error.load()<<"\",\"window_ready\":"<<(state->input.window?"true":"false")
           <<",\"queue_ready\":"<<(state->queueReady.load()?"true":"false")<<",\"queue_address\":"<<state->queueAddress.load()<<",\"binding_route\":\""<<state->bindingRoute.load()<<"\""
           <<",\"captured_creations\":"<<state->creations.load()<<",\"unmatched_presents\":"<<state->unmatchedPresents.load()
           <<",\"backbuffer_transitions\":"<<state->discovery.backbufferTransitions.load()<<",\"matched_submissions\":"<<state->discovery.matchedSubmissions.load()
           <<",\"visible\":"<<(state->input.visible.load()?"true":"false")<<",\"capture\":"<<(state->input.Capturing()?"true":"false")
           <<",\"presents\":"<<state->presents.load()<<",\"drawn\":"<<state->drawn.load()<<",\"busy_frames\":"<<state->busy.load()
           <<",\"resizes\":"<<state->resizes.load()<<",\"toggles\":"<<state->input.toggles.load()<<",\"blocked_messages\":"<<state->input.blockedMessages.load()
           <<",\"hotkey_messages\":"<<state->input.hotkeyMessages.load()<<",\"last_hotkey_modifiers\":"<<state->input.lastHotkeyModifiers.load()
           <<",\"hotkey_registered\":"<<(state->input.hotkeyRegistered.load()?"true":"false")<<",\"hotkey_error\":"<<state->input.hotkeyError.load()
           <<",\"hotkey_vk\":"<<VK_DELETE<<",\"text_input\":"<<(state->input.textInput.load()?"true":"false")
           <<",\"ime_error\":"<<state->input.imeError.load()<<",\"ime_activations\":"<<state->input.imeActivations.load()<<",\"ime_compositions\":"<<state->input.imeCompositions.load()<<",\"language_changes\":"<<state->input.languageChanges.load()<<",\"drag_bounds\":"<<state->input.dragBounds.load()
           <<",\"drag_releases\":"<<state->input.dragReleases.load()<<",\"drag_release_pending\":"<<(state->input.dragHandoff.Waiting()?"true":"false")
           <<",\"mouse_leaves\":"<<state->input.mouseLeaves.load()<<",\"capture_changes\":"<<state->input.captureChanges.load()<<",\"rendered_left\":"<<(state->input.renderedLeft.load()?"true":"false")<<",\"queued_left\":"<<state->input.queuedLeft.load()
           <<",\"wndproc_message\":"<<(probe&0xffff)<<",\"wndproc_phase\":"<<((probe>>16)&0xffff)<<",\"wndproc_age_ms\":"<<probeAge<<",\"wndproc_max_ms\":"<<state->input.trace.longest.load()
           <<",\"blocked_polls\":"<<state->input.blockedPolls.load()
           <<",\"api_connected\":"<<(controls->connected?"true":"false")<<",\"control_ready\":"<<(controls->ready?"true":"false")
           <<",\"targets\":"<<controls->data.targetCount<<",\"source\":"<<controls->data.source<<",\"control_result\":"<<controls->operation<<",\"snapshot_result\":"<<controls->scan
           <<",\"identity_available\":"<<(controls->presets.available?"true":"false")<<",\"preset_count\":"<<controls->presets.choices->size()
           <<",\"usable_presets\":"<<std::count_if(controls->presets.choices->begin(),controls->presets.choices->end(),[](const auto& choice){return choice.matched>0;})
           <<",\"default_links\":"<<controls->presets.defaults->size()<<",\"auto_paused\":"<<(controls->presets.paused?"true":"false")<<",\"morph_enabled\":"<<(controls->presets.enabled?"true":"false")
           <<",\"auto_message\":"<<morphpreset::Quote(controls->presets.autoMessage)<<",\"editor\":"<<morphpreset::Quote(controls->presets.activeId)<<",\"preview\":"<<(controls->presets.preview?"true":"false")
           <<",\"recycled_presets\":"<<controls->presets.removed->size()
           <<",\"preset_completed\":"<<controls->presets.completed<<",\"preset_success\":"<<(controls->presets.success?"true":"false")
           <<",\"preset_dirty\":"<<(controls->presets.dirty?"true":"false")<<"}\n";log.flush();
        }Sleep(state->input.visible.load()?16:100);
    }
}
}
extern "C" __declspec(dllexport) int MhmImGuiBootstrapState(){return morphgui::publishedBoot.load();}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);const auto thread=CreateThread(nullptr,0,morphgui::Worker,module,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;
}
