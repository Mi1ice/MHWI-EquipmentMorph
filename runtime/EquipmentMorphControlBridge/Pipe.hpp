#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <string>
#include <functional>

namespace mhmcontrol {
inline std::wstring PipeName(DWORD pid){return L"\\\\.\\pipe\\MHWI-EquipmentMorph-"+std::to_wstring(pid);}
// All pending I/O is cancelled and drained before stack buffers/events leave scope.
inline bool Transfer(HANDLE pipe,void* buffer,DWORD size,bool writing,HANDLE stop,
                     const std::function<bool()>& alive){
    OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!operation.hEvent)return false;
    DWORD count=0;bool ok=false;
    const BOOL started=writing?WriteFile(pipe,buffer,size,&count,&operation):ReadFile(pipe,buffer,size,&count,&operation);
    if(started)ok=count==size;
    else if(GetLastError()==ERROR_IO_PENDING){
        HANDLE events[]{operation.hEvent,stop};
        for(;;){
            const auto wait=WaitForMultipleObjects(stop?2u:1u,events,FALSE,100);
            if(wait==WAIT_OBJECT_0){ok=GetOverlappedResult(pipe,&operation,&count,FALSE)&&count==size;break;}
            if(wait!=WAIT_TIMEOUT||!alive()){
                CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&count,TRUE);break;
            }
        }
    }
    CloseHandle(operation.hEvent);return ok;
}
}
