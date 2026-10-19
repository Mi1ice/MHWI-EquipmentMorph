// Separate DX12/WARP application; never touches the game or morph data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <dinput.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
std::atomic_bool done=false;
std::atomic_uint width=960,height=640,keyMessages=0,mouseMessages=0;
std::atomic_int failure=0;
void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("DX12 fixture operation failed");}
LRESULT CALLBACK Procedure(HWND w,UINT m,WPARAM a,LPARAM b){
    if(m==WM_SIZE&&a!=SIZE_MINIMIZED){width=LOWORD(b);height=HIWORD(b);return 0;}
    if(m==WM_KEYDOWN&&a=='W')keyMessages++;
    if(m==WM_MOUSEMOVE)mouseMessages++;
    if(m==WM_DESTROY){done=true;PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,a,b);
}
void Render(HWND window,const std::filesystem::path& root){
    try{
        ComPtr<ID3D12Debug> debug;const bool debugLayer=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));if(debugLayer)debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> warp;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        ComPtr<ID3D12InfoQueue> diagnostics;if(debugLayer)device.As(&diagnostics);
        D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=width;sd.Height=height;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;
        sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> first;Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&first));ComPtr<IDXGISwapChain3> swap;Check(first.As(&swap));first.Reset();
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        ComPtr<ID3D12Resource> buffers[2];auto getBuffers=[&](){auto handle=heap->GetCPUDescriptorHandleForHeapStart();for(UINT i=0;i<2;i++){Check(swap->GetBuffer(i,IID_PPV_ARGS(&buffers[i])));device->CreateRenderTargetView(buffers[i].Get(),nullptr,handle);handle.ptr+=step;}};getBuffers();
        ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));Check(list->Close());
        ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Fixture fence event missing");
        ComPtr<IDirectInput8W> input;ComPtr<IDirectInputDevice8W> keyboard;
        Check(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(input.GetAddressOf()),nullptr));
        Check(input->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr));Check(keyboard->SetDataFormat(&c_dfDIKeyboard));Check(keyboard->SetCooperativeLevel(window,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE));keyboard->Acquire();
        UINT64 sequence=0,lastReport=0;unsigned asyncHits=0,diHits=0,resizes=0,renderErrors=0;
        auto wait=[&](){Check(queue->Signal(fence.Get(),++sequence));Check(fence->SetEventOnCompletion(sequence,event));if(WaitForSingleObject(event,3000)!=WAIT_OBJECT_0)throw std::runtime_error("Fixture fence timeout");};
        while(!done){
            if(!IsWindow(window))break;
            if(width&&height&&(width!=sd.Width||height!=sd.Height)){
                wait();for(auto& b:buffers)b.Reset();sd.Width=width;sd.Height=height;
                Check(swap->ResizeBuffers(2,sd.Width,sd.Height,sd.Format,0));getBuffers();resizes++;
            }
            if(GetAsyncKeyState('W')&0x8000)asyncHits++;
            BYTE keys[256]{};auto read=keyboard->GetDeviceState(sizeof(keys),keys);if(FAILED(read)){keyboard->Acquire();}else if(keys[DIK_W]&0x80)diHits++;
            Check(allocator->Reset());Check(list->Reset(allocator.Get(),nullptr));const auto index=swap->GetCurrentBackBufferIndex();
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=buffers[index].Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;list->ResourceBarrier(1,&barrier);
            auto rtv=heap->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(index)*step;const float clear[]={.06f,.09f,.12f,1};list->ClearRenderTargetView(rtv,clear,0,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);Check(list->Close());ID3D12CommandList* batch[]={list.Get()};queue->ExecuteCommandLists(1,batch);
            Check(swap->Present(1,0));wait();
            if(diagnostics){
                const auto count=diagnostics->GetNumStoredMessagesAllowedByRetrievalFilter();
                for(UINT64 i=0;i<count;i++){SIZE_T size=0;diagnostics->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                    if(SUCCEEDED(diagnostics->GetMessage(i,message,&size))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){renderErrors++;std::ofstream out(root/L"fixture-d3d12-errors.txt",std::ios::app);out<<message->pDescription<<'\n';}}
                diagnostics->ClearStoredMessages();
            }
            const auto now=GetTickCount64();if(now-lastReport>250){
                lastReport=now;std::ofstream out(root/L"fixture-state.json");out<<"{\"software_fixture\":true,\"warp\":true,\"debug_layer\":"<<(debugLayer?"true":"false")
                    <<",\"frames\":"<<sequence<<",\"game_key_messages\":"<<keyMessages.load()<<",\"game_mouse_messages\":"<<mouseMessages.load()
                    <<",\"async_w_samples\":"<<asyncHits<<",\"directinput_w_samples\":"<<diHits<<",\"resizes\":"<<resizes<<",\"d3d12_errors\":"<<renderErrors
                    <<",\"foreground_is_fixture\":"<<(GetForegroundWindow()==window?"true":"false")<<"}\n";
            }Sleep(8);
        }wait();CloseHandle(event);
    }catch(const std::exception& error){failure=1;std::ofstream out(root/L"fixture-error.txt");out<<error.what();done=true;PostMessageW(window,WM_CLOSE,0,0);}
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);const auto root=std::filesystem::path(file).parent_path();
    const auto overlay=LoadLibraryW((root/L"MHWEquipmentMorphImGui.dll").c_str());if(!overlay)return 2;
    using Boot=int(*)();Boot boot=nullptr;const auto address=GetProcAddress(overlay,"MhmImGuiBootstrapState");std::memcpy(&boot,&address,sizeof(boot));if(!boot)return 3;
    const auto start=GetTickCount64();while(!boot()&&GetTickCount64()-start<15000)Sleep(20);if(boot()!=1)return 4;
    WNDCLASSW wc{};wc.lpfnWndProc=Procedure;wc.hInstance=instance;wc.lpszClassName=L"EquipmentMorphImGuiFixture";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    const auto window=CreateWindowW(wc.lpszClassName,L"EquipmentMorph ImGui DX12 软件测试（非游戏）",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,960,640,nullptr,nullptr,instance,nullptr);if(!window)return 5;
    ShowWindow(window,SW_SHOWDEFAULT);ShowWindow(window,SW_SHOW);
    std::thread render(Render,window,root);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    done=true;render.join();return failure.load();
}
