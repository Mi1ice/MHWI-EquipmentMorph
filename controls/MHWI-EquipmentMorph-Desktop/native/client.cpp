#include "../../../runtime/EquipmentMorphControlBridge/Pipe.hpp"
#include "../../../runtime/EquipmentMorphControlBridge/Protocol.hpp"
#include <TlHelp32.h>
#include <memory>
#include <cstring>

namespace {
struct Client {
    HANDLE process=nullptr,pipe=INVALID_HANDLE_VALUE;
    DWORD pid=0;
    ~Client(){if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);if(process)CloseHandle(process);}
};
bool IsGame(HANDLE process){
    wchar_t path[32768]{};DWORD count=32768;
    if(!QueryFullProcessImageNameW(process,0,path,&count))return false;
    const auto slash=wcsrchr(path,L'\\');
    return _wcsicmp(slash?slash+1:path,L"MonsterHunterWorld.exe")==0;
}
}
extern "C" {
DWORD mhm_find_game(){
    const auto list=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(list==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);DWORD pid=0;
    if(Process32FirstW(list,&entry))do{
        if(!_wcsicmp(entry.szExeFile,L"MonsterHunterWorld.exe")){
            if(pid){pid=0;break;}pid=entry.th32ProcessID;
        }
    }while(Process32NextW(list,&entry));
    CloseHandle(list);return pid;
}
void* mhm_open_game(DWORD pid){
    try {
    auto c=std::make_unique<Client>();c->pid=pid;
    c->process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!c->process||!IsGame(c->process)||WaitForSingleObject(c->process,0)!=WAIT_TIMEOUT)return nullptr;
    return c.release();
    } catch (...) { return nullptr; }
}
int mhm_game_alive(void* value){
    const auto c=static_cast<Client*>(value);
    return c&&WaitForSingleObject(c->process,0)==WAIT_TIMEOUT;
}
void mhm_disconnect(void* value){
    const auto c=static_cast<Client*>(value);
    if(c&&c->pipe!=INVALID_HANDLE_VALUE){CloseHandle(c->pipe);c->pipe=INVALID_HANDLE_VALUE;}
}
void mhm_close_game(void* value){delete static_cast<Client*>(value);}
int mhm_exchange(void* value,const unsigned char* input,size_t inputBytes,unsigned char* output,size_t outputBytes){
    try {
    if(!value||!input||!output)return 0;
    auto& c=*static_cast<Client*>(value);
    if(inputBytes!=sizeof(mhmcontrol::Request)||outputBytes!=sizeof(mhmcontrol::Response)||!mhm_game_alive(value))return 0;
    bool first=false;
    if(c.pipe==INVALID_HANDLE_VALUE){
        const auto name=mhmcontrol::PipeName(c.pid);
        c.pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(c.pipe==INVALID_HANDLE_VALUE)return 0;
        ULONG server=0;DWORD mode=PIPE_READMODE_MESSAGE;
        if(!GetNamedPipeServerProcessId(c.pipe,&server)||server!=c.pid||!SetNamedPipeHandleState(c.pipe,&mode,nullptr,nullptr)){
            mhm_disconnect(value);return 0;
        }
        first=true;
    }
    mhmcontrol::Request request;std::memcpy(&request,input,sizeof(request));
    // A newly opened connection can only poll, never replay a queued gesture.
    if(first){request.action=mhmcontrol::Action::Poll;request.target=0;request.key=0;request.weight=0;}
    mhmcontrol::Response response;
    const auto started=GetTickCount64();
    auto alive=[&](){return mhm_game_alive(value)&&GetTickCount64()-started<1200;};
    if(!mhmcontrol::Transfer(c.pipe,&request,sizeof(request),true,nullptr,alive)||
       !mhmcontrol::Transfer(c.pipe,&response,sizeof(response),false,nullptr,alive)||
       !mhmcontrol::Valid(response,request.sequence,c.pid)){
        mhm_disconnect(value);return 0;
    }
    std::memcpy(output,&response,sizeof(response));return first?2:1;
    } catch (...) { mhm_disconnect(value);return 0; }
}
DWORD mhm_foreground_pid(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid;}
void mhm_focus_game(DWORD pid){
    EnumWindows([](HWND window,LPARAM argument)->BOOL{
        DWORD owner=0;GetWindowThreadProcessId(window,&owner);
        if(owner==static_cast<DWORD>(argument)&&IsWindowVisible(window)&&!GetWindow(window,GW_OWNER)){
            SetForegroundWindow(window);return FALSE;
        }return TRUE;
    },static_cast<LPARAM>(pid));
}
}
