#include "Pipe.hpp"
#include "Session.hpp"
#include "DesktopLaunch.hpp"
#include <cstring>

namespace {
DWORD WINAPI Worker(void*){
    try {
        HMODULE self=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&self))return 1;
        HMODULE host=nullptr;
        while(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"MHWEquipmentMorphConfiguredPrototype.dll",&host))Sleep(250);
        using Getter=MhmResult(MHM_CALL*)(uint32_t,uint32_t,MhmApi*);Getter getter=nullptr;
        const auto address=GetProcAddress(host,"MhwMorphGetApi");std::memcpy(&getter,&address,sizeof(getter));MhmApi api{};
        if(!getter||getter(MHM_API_VERSION,sizeof(api),&api)!=MHM_OK||api.bytes!=sizeof(api)
           ||(api.capabilities&(MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE))!=(MHM_CAP_BATCH|MHM_CAP_LAYERS|MHM_CAP_LEASE))return 2;
        const auto name=mhmcontrol::PipeName(GetCurrentProcessId());
        // Runs on this worker, after host verification, never inside DllMain.
        mhmcontrol::StartDesktop();
        for(;;){
            HANDLE pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
            if(pipe==INVALID_HANDLE_VALUE)return 3;
            OVERLAPPED connect{};connect.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if(!connect.hEvent){CloseHandle(pipe);return 4;}
            bool connected=ConnectNamedPipe(pipe,&connect)!=FALSE;
            if(!connected){const auto error=GetLastError();if(error==ERROR_PIPE_CONNECTED)connected=true;
                else if(error==ERROR_IO_PENDING){DWORD count=0;connected=GetOverlappedResult(pipe,&connect,&count,TRUE)!=FALSE;}}
            CloseHandle(connect.hEvent);
            if(connected){
                mhmcontrol::Session session(api);uint64_t sequence=0,lastRequest=GetTickCount64(),lastHeartbeat=lastRequest;
                auto alive=[&](){const auto now=GetTickCount64();
                    if(now-lastRequest>=1500){session.Release();return false;}
                    if(now-lastHeartbeat>=500){lastHeartbeat=now;const auto r=session.Heartbeat();if(r!=MHM_OK&&r!=MHM_STALE)return false;}
                    return true;};
                for(;;){
                    mhmcontrol::Request request;
                    if(!mhmcontrol::Transfer(pipe,&request,sizeof(request),false,nullptr,alive))break;
                    if(!mhmcontrol::Valid(request,sequence))break;
                    sequence=request.sequence;lastRequest=GetTickCount64();
                    auto result=session.Heartbeat();
                    if(result==MHM_OK||result==MHM_STALE){
                        mhmcontrol::Response before;result=session.Snapshot(before);
                        if(result==MHM_OK)result=session.Change(request);
                    }
                    mhmcontrol::Response response;const auto snapshot=session.Snapshot(response);
                    response.result=result==MHM_OK?snapshot:result;response.sequence=sequence;response.pid=GetCurrentProcessId();
                    if(!mhmcontrol::Transfer(pipe,&response,sizeof(response),true,nullptr,alive))break;
                }
            }
            DisconnectNamedPipe(pipe);CloseHandle(pipe);
        }
    }catch(...){return 5;}
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);const auto thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;
}
