#define UNICODE
#define _UNICODE
#include "../../runtime/EquipmentMorphControlBridge/Pipe.hpp"
#include "../../runtime/EquipmentMorphControlBridge/Protocol.hpp"
#include <CommCtrl.h>
#include <TlHelp32.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <cwchar>
#include <vector>

namespace {
using namespace mhmcontrol;
constexpr UINT StateMessage=WM_APP+1;
enum { Equipment=101,Keys,Own,Slider,Value,Apply,ReleaseAll };
HWND window,combo,list,check,slider,edit,apply,releaseAll,status,detail;HFONT font;
HANDLE stopEvent;std::thread network;
struct Pending {Request request;uint64_t generation=0;};
std::mutex gate;std::condition_variable wake;std::deque<Pending> queue;
std::atomic_bool closing=false;std::atomic<uint64_t> generation=0;
Response snapshot;uint64_t selectedTarget=0;uint32_t selectedKey=UINT32_MAX;bool drawing=false;
std::vector<uint64_t> displayedTargets;
struct Delivery {Response response{};uint64_t generation=0;bool connected=false;};
std::wstring Text(const char* value){
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,nullptr,0);
    if(!count)return L"?";std::wstring out(static_cast<size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,out.data(),count);out.pop_back();return out;
}
std::wstring Number(float value){wchar_t out[40]{};swprintf_s(out,L"%.9g",static_cast<double>(value));return out;}
const wchar_t* Part(uint32_t id){static constexpr const wchar_t* parts[]{L"头部",L"身体",L"腕部",L"腰部",L"腿部"};return id<5?parts[id]:L"装备";}
const Key* Selected(){for(uint32_t i=0;i<snapshot.keyCount;i++){const auto& k=snapshot.keys[i];if(k.target==selectedTarget&&k.index==selectedKey)return &k;}return nullptr;}
void Send(Action action,uint64_t target=0,uint32_t key=0,float weight=0){
    Request request;request.action=action;request.target=target;request.key=key;request.weight=weight;
    {std::lock_guard lock(gate);
        if(action==Action::Poll){if(!queue.empty())return;}
        else {
            generation.fetch_add(1);
            if(action==Action::Release)queue.clear();
            if(action==Action::Set&&!queue.empty()&&queue.back().request.action==Action::Set&&queue.back().request.target==target&&queue.back().request.key==key)queue.pop_back();
            if(queue.size()>=64){queue.clear();request.action=Action::Release;}
        }
        queue.push_back({request,generation.load()});
    }wake.notify_one();
}
DWORD GamePid(){
    const auto processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(processes==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);DWORD pid=0;
    if(Process32FirstW(processes,&entry))do{
        if(_wcsicmp(entry.szExeFile,L"MonsterHunterWorld.exe")==0){if(pid){pid=0;break;}pid=entry.th32ProcessID;}
    }while(Process32NextW(processes,&entry));CloseHandle(processes);return pid;
}
void Deliver(std::unique_ptr<Delivery> delivery){
    if(closing)return;
    if(PostMessageW(window,StateMessage,0,reinterpret_cast<LPARAM>(delivery.get())))delivery.release();
}
void Network(){
    HANDLE pipe=INVALID_HANDLE_VALUE;DWORD pid=0;uint64_t sequence=0;
    try {
        while(!closing){
            Request request;uint64_t token=0;
            {std::unique_lock lock(gate);wake.wait(lock,[]{return closing||!queue.empty();});if(closing)break;
                request=queue.front().request;token=queue.front().generation;queue.pop_front();}
            if(pipe==INVALID_HANDLE_VALUE){
                pid=GamePid();
                if(pid)pipe=CreateFileW(PipeName(pid).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
                if(pipe!=INVALID_HANDLE_VALUE){ULONG server=0;DWORD mode=PIPE_READMODE_MESSAGE;
                    if(!GetNamedPipeServerProcessId(pipe,&server)||server!=pid||!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr)){
                        CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;
                    }
                }
                // A new connection always starts read-only; queued gestures from an old connection are discarded.
                {std::lock_guard lock(gate);queue.clear();}
                request=Request{};token=generation.load();
            }
            auto delivery=std::make_unique<Delivery>();delivery->generation=token;
            if(pipe!=INVALID_HANDLE_VALUE){
                request.sequence=++sequence;const auto started=GetTickCount64();
                auto alive=[&](){return !closing&&GetTickCount64()-started<1200;};
                auto& response=delivery->response;
                delivery->connected=Transfer(pipe,&request,sizeof(request),true,stopEvent,alive)
                    &&Transfer(pipe,&response,sizeof(response),false,stopEvent,alive)&&Valid(response,request.sequence,pid);
                if(!delivery->connected){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;std::lock_guard lock(gate);queue.clear();}
            }
            Deliver(std::move(delivery));
        }
    }catch(...){auto failed=std::make_unique<Delivery>();failed->generation=generation.load();Deliver(std::move(failed));}
    // Closing the pipe releases the session even when the UI cannot send a final command.
    if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);
}
void ListText(int row,int column,const std::wstring& text){ListView_SetItemText(list,row,column,const_cast<wchar_t*>(text.c_str()));}
void Editor(bool force){
    drawing=true;const auto* key=Selected();
    EnableWindow(check,key!=nullptr);const bool owned=key&&key->owned;
    SendMessageW(check,BM_SETCHECK,owned?BST_CHECKED:BST_UNCHECKED,0);
    EnableWindow(slider,owned);EnableWindow(edit,key!=nullptr);EnableWindow(apply,key!=nullptr);
    if(key){
        SetWindowTextW(detail,(Text(key->info.id)+L"   范围："+Number(key->info.minimum)+L" ～ "+Number(key->info.maximum)).c_str());
        if(force||(GetFocus()!=edit&&GetCapture()!=slider)){
            const float weight=owned?key->contribution:key->state.requested;
            SetWindowTextW(edit,Number(weight).c_str());
            const double span=static_cast<double>(key->info.maximum)-key->info.minimum;
            const auto position=span>0?static_cast<int>((static_cast<double>(weight)-key->info.minimum)*1000/span+.5):0;
            SendMessageW(slider,TBM_SETPOS,TRUE,position);
        }
    }else {SetWindowTextW(detail,L"请选择一个形态键");SetWindowTextW(edit,L"");SendMessageW(slider,TBM_SETPOS,TRUE,0);}
    drawing=false;
}
void Populate(bool force){
    drawing=true;
    const auto old=selectedTarget;bool found=false;
    std::vector<uint64_t> handles;for(uint32_t i=0;i<snapshot.targetCount;i++)handles.push_back(snapshot.targets[i].info.handle);
    const bool changed=handles!=displayedTargets;
    if(changed)SendMessageW(combo,CB_RESETCONTENT,0,0);
    for(uint32_t i=0;i<snapshot.targetCount;i++){
        const auto& t=snapshot.targets[i];const auto text=std::wstring(Part(t.info.part))+L" · "+Text(t.info.model_id);
        if(changed)SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
        if(t.info.handle==old){if(changed)SendMessageW(combo,CB_SETCURSEL,i,0);found=true;}
    }
    displayedTargets=std::move(handles);
    if(!found){selectedTarget=snapshot.targetCount?snapshot.targets[0].info.handle:0;selectedKey=UINT32_MAX;
        if(snapshot.targetCount)SendMessageW(combo,CB_SETCURSEL,0,0);}
    const Target* target=nullptr;for(uint32_t i=0;i<snapshot.targetCount;i++)if(snapshot.targets[i].info.handle==selectedTarget)target=&snapshot.targets[i];
    const auto count=target?target->count:0;
    if(static_cast<uint32_t>(ListView_GetItemCount(list))!=count||!found||force){
        ListView_DeleteAllItems(list);
        for(uint32_t i=0;i<count;i++){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=const_cast<wchar_t*>(L"");ListView_InsertItem(list,&item);}
        if(selectedKey==UINT32_MAX&&count)selectedKey=0;
        if(selectedKey<count)ListView_SetItemState(list,static_cast<int>(selectedKey),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    for(uint32_t i=0;i<count;i++){
        const auto& k=snapshot.keys[target->first+i];const auto row=static_cast<int>(i);
        ListText(row,0,Text(k.info.id));ListText(row,1,Number(k.state.requested));ListText(row,2,Number(k.state.applied));
        ListText(row,3,k.owned?(k.state.source==snapshot.source?L"本窗口控制":L"本窗口已接管，其他来源优先"):k.state.source?L"其他来源控制":L"未接管");
    }
    drawing=false;Editor(force);
}
void Input(){
    const auto* key=Selected();if(!key)return;wchar_t text[100]{};GetWindowTextW(edit,text,100);
    for(auto& ch:text)if(ch==L',')ch=L'.';
    wchar_t* end=nullptr;const float value=std::wcstof(text,&end);while(end&&iswspace(*end))end++;
    if(end==text||!end||*end||!std::isfinite(value)||value<key->info.minimum||value>key->info.maximum){
        MessageBoxW(window,L"请输入当前形态键范围内的数值。",L"权重无效",MB_OK|MB_ICONINFORMATION);return;
    }
    Send(Action::Set,key->target,key->index,value);
}
HWND Control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id=0){
    const auto result=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return result;
}
LRESULT CALLBACK WindowProc(HWND handle,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        window=handle;font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        Control(L"STATIC",L"装备",0,20,20,70,25);
        combo=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,90,16,750,250,Equipment);
        list=Control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER|WS_TABSTOP,20,60,820,220,Keys);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t* columns[]{L"形态键",L"目标权重",L"已应用",L"控制状态"};const int widths[]{280,120,120,280};
        for(int i=0;i<4;i++){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=widths[i];column.pszText=const_cast<wchar_t*>(columns[i]);ListView_InsertColumn(list,i,&column);}
        detail=Control(L"STATIC",L"请选择一个形态键",0,20,300,820,25);
        check=Control(L"BUTTON",L"接管当前形态键",BS_AUTOCHECKBOX|WS_TABSTOP,20,337,215,28,Own);
        slider=Control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,235,332,395,35,Slider);SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));
        edit=Control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,645,335,105,30,Value);
        apply=Control(L"BUTTON",L"设置",BS_PUSHBUTTON|WS_TABSTOP,760,335,80,30,Apply);
        releaseAll=Control(L"BUTTON",L"释放全部控制",BS_PUSHBUTTON|WS_TABSTOP,20,385,170,35,ReleaseAll);
        Control(L"STATIC",L"权重 0 仍属于接管；释放后交回其它控制来源。关闭窗口会释放本窗口的控制。",0,205,391,640,45);
        status=Control(L"STATIC",L"正在连接游戏…",0,20,450,820,45);
        Editor(true);SetTimer(window,1,250,nullptr);return 0;
    }
    case WM_TIMER:Send(Action::Poll);return 0;
    case StateMessage:{
        const std::unique_ptr<Delivery> delivery(reinterpret_cast<Delivery*>(l));
        if(delivery->generation!=generation.load())return 0;
        if(!delivery->connected){snapshot=Response{};Populate(true);SetWindowTextW(status,L"未连接：请启动游戏并加载控制桥接插件。重连后不会恢复旧权重。");}
        else {
            snapshot=delivery->response;
            Populate(false);
            const auto text=snapshot.result==MHM_OK?(snapshot.targetCount?L"已连接 · 仅显示当前可用形态键":L"已连接 · 等待装备自动绑定")
                :snapshot.result==MHM_CONFLICT?L"设置未接受：形态键存在互斥冲突，原权重保留"
                :snapshot.result==MHM_STALE?L"装备正在更换 · 等待更新":L"本次设置未接受（错误码见说明文档）";
            SetWindowTextW(status,(std::wstring(text)+L"   游戏 "+std::to_wstring(snapshot.pid)+L"   接口结果 "+std::to_wstring(snapshot.result)).c_str());
        }return 0;
    }
    case WM_COMMAND:{
        if(drawing)return 0;const auto id=LOWORD(w);
        if(id==Equipment&&HIWORD(w)==CBN_SELCHANGE){const auto index=SendMessageW(combo,CB_GETCURSEL,0,0);if(index>=0&&static_cast<uint64_t>(index)<snapshot.targetCount){selectedTarget=snapshot.targets[static_cast<size_t>(index)].info.handle;selectedKey=UINT32_MAX;Populate(true);}return 0;}
        if(id==Own&&HIWORD(w)==BN_CLICKED){const auto* key=Selected();if(key){if(SendMessageW(check,BM_GETCHECK,0,0)==BST_CHECKED)Input();else Send(Action::Drop,key->target,key->index,0);}return 0;}
        if(id==Apply){Input();return 0;}if(id==ReleaseAll){Send(Action::Release);return 0;}
        break;
    }
    case WM_NOTIFY:{
        const auto* header=reinterpret_cast<NMHDR*>(l);
        if(!drawing&&header->idFrom==Keys&&header->code==LVN_ITEMCHANGED){const auto* change=reinterpret_cast<NMLISTVIEW*>(l);if((change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED)){selectedKey=static_cast<uint32_t>(change->iItem);Editor(true);}}
        return 0;
    }
    case WM_HSCROLL:{
        if(!drawing&&reinterpret_cast<HWND>(l)==slider){const auto* key=Selected();if(key&&key->owned){const auto position=SendMessageW(slider,TBM_GETPOS,0,0);
            const auto value=static_cast<float>(static_cast<double>(key->info.minimum)+(static_cast<double>(key->info.maximum)-key->info.minimum)*static_cast<double>(position)/1000);
            SetWindowTextW(edit,Number(value).c_str());Send(Action::Set,key->target,key->index,value);}}
        return 0;
    }
    case WM_CLOSE:closing=true;KillTimer(window,1);SetEvent(stopEvent);wake.notify_one();DestroyWindow(window);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(handle,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    const auto single=CreateMutexW(nullptr,FALSE,L"Local\\MHWI-EquipmentMorph-ControlPanel");
    if(!single)return 1;if(GetLastError()==ERROR_ALREADY_EXISTS){const auto existing=FindWindowW(L"MHWIEquipmentMorphPanel",nullptr);if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}CloseHandle(single);return 0;}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent){CloseHandle(single);return 2;}
    WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"MHWIEquipmentMorphPanel";
    RegisterClassW(&cls);window=CreateWindowExW(0,cls.lpszClassName,L"MHWI-EquipmentMorph · 形态键控制",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,880,550,nullptr,nullptr,instance,nullptr);
    if(!window){CloseHandle(stopEvent);CloseHandle(single);return 3;}
    network=std::thread(Network);ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    closing=true;SetEvent(stopEvent);wake.notify_one();network.join();
    while(PeekMessageW(&message,nullptr,StateMessage,StateMessage,PM_REMOVE))delete reinterpret_cast<Delivery*>(message.lParam);
    DeleteObject(font);CloseHandle(stopEvent);CloseHandle(single);return 0;
}
