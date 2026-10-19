// A software-only game-shaped process for testing launch and desktop lifecycle.
// Never distribute this executable as a game or runtime payload.
#include "../../../runtime/EquipmentMorphPerKeyCore/include/mhw_morph_host.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace {
MhmApi api{};
decltype(&MhwMorphHostTick) clockTick=nullptr;
uint64_t targetHandle=0;
std::filesystem::path root;
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_TIMER){
        clockTick(GetTickCount64());
        MhmWeightState state{};uint32_t count=0;uint64_t requested=0,applied=0;
        const auto result=api.read_weights(targetHandle,&state,1,&count,&requested,&applied);
        std::ofstream out(root/L"fixture-state.json");
        out<<"{\"software_fixture\":true,\"pid\":"<<GetCurrentProcessId()<<",\"result\":"<<result
           <<",\"requested\":"<<state.requested<<",\"source\":"<<state.source<<",\"tick\":"<<GetTickCount64()<<"}\n";
        return 0;
    }
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    try{
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        root=std::filesystem::path(executable).parent_path();
        const auto host=LoadLibraryW((root/L"fixture"/L"MHWEquipmentMorphConfiguredPrototype.dll").c_str());
        if(!host)return 1;
        decltype(&MhwMorphGetApi) get=nullptr;decltype(&MhwMorphHostAttach) attach=nullptr;
        auto load=[&]<class T>(T& fn,const char* name){const auto address=GetProcAddress(host,name);std::memcpy(&fn,&address,sizeof(fn));return fn!=nullptr;};
        if(!load(get,"MhwMorphGetApi")||!load(attach,"MhwMorphHostAttach")||!load(clockTick,"MhwMorphHostTick"))return 2;
        if(get(MHM_API_VERSION,sizeof(api),&api)!=MHM_OK)return 3;
        unsigned char basis[64]{};MhmDelta delta{0,1,0,0};MhmHostKey key{};
        strcpy_s(key.info.id,"vrc.blink");key.info.maximum=1;key.deltas=&delta;key.count=1;
        MhmHostTarget target{MHM_BODY,sizeof(basis),"synthetic/pl001_0000",basis,&key,1,0,nullptr};
        if(attach(&target,&targetHandle)!=MHM_OK)return 4;
        clockTick(GetTickCount64());
        WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=L"EquipmentMorphSoftwareFixture";
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
        const auto window=CreateWindowExW(0,wc.lpszClassName,L"EquipmentMorph 软件联动测试（无游戏/GPU）",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,720,300,nullptr,nullptr,instance,nullptr);
        if(!window)return 5;
        CreateWindowW(L"STATIC",L"这是隔离的软件测试进程，没有连接真实游戏或 GPU。\n\nCtrl＋Shift＋M：呼出 / 隐藏控制窗口\n关闭此测试窗口：模拟游戏退出",WS_CHILD|WS_VISIBLE,
            32,32,650,180,window,nullptr,instance,nullptr);
        SetTimer(window,1,50,nullptr);ShowWindow(window,SW_SHOWDEFAULT);ShowWindow(window,SW_SHOW);
        if(!LoadLibraryW((root/L"MHWEquipmentMorphControlBridge.dll").c_str()))return 6;
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        return 0;
    }catch(...){return 7;}
}
