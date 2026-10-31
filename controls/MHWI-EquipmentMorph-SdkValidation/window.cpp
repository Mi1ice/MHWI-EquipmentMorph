#include "WindowModel.hpp"
#include <deque>

namespace mhmvalidation {
namespace {
constexpr wchar_t ClassName[]=L"MHWI_SDK_Validation_Window_v1";
struct Context {
    HWND window=nullptr,status=nullptr,details=nullptr;std::map<Action,HWND> buttons;
    WindowCommands commands;bool textFailure=false,registered=false;HINSTANCE module=nullptr;
    HFONT font=nullptr;WindowModel previous;
    ~Context(){if(window&&IsWindow(window))DestroyWindow(window);if(font)DeleteObject(font);if(registered)UnregisterClassW(ClassName,module);}
    void Queue(Action action){commands.Push(action);}
};
int Scale(HWND window,int value){return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
void Layout(Context& c){
    RECT r{};GetClientRect(c.window,&r);const auto margin=Scale(c.window,16),gap=Scale(c.window,8),height=Scale(c.window,32);const auto width=r.right-2*margin;
    MoveWindow(c.status,margin,margin,width,Scale(c.window,118),TRUE);
    const std::array<Action,5> first{Action::PreviousCase,Action::Begin,Action::NextCase,Action::ZeroAll,Action::End};
    const std::array<Action,3> second{Action::PreviousStep,Action::RepeatStep,Action::NextStep};int y=margin+Scale(c.window,126);
    for(size_t i=0;i<first.size();++i)MoveWindow(c.buttons.at(first[i]),margin+static_cast<int>(i)*(width+gap)/5,y,(width-4*gap)/5,height,TRUE);
    y+=height+gap;for(size_t i=0;i<second.size();++i)MoveWindow(c.buttons.at(second[i]),margin+static_cast<int>(i)*(width+gap)/3,y,(width-2*gap)/3,height,TRUE);
    y+=height+gap;MoveWindow(c.details,margin,y,width,std::max(Scale(c.window,80),static_cast<int>(r.bottom)-y-margin),TRUE);
}
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* c=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){auto* create=reinterpret_cast<CREATESTRUCTW*>(l);c=static_cast<Context*>(create->lpCreateParams);c->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c));}
    if(!c)return DefWindowProcW(window,message,w,l);
    switch(message){
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED&&LOWORD(w)>=100&&LOWORD(w)<108){c->Queue(static_cast<Action>(LOWORD(w)-100));return 0;}break;
    case WM_CLOSE:c->Queue(Action::End);return 0;
    case WM_SIZE:if(c->details)Layout(*c);return 0;
    case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(l);m->ptMinTrackSize={Scale(window,760),Scale(window,560)};return 0;}
    case WM_DPICHANGED:{auto* r=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
    case WM_DESTROY:PostQuitMessage(0);return 0;
    default:break;
    }
    return DefWindowProcW(window,message,w,l);
}
void Update(Context& c,const WindowModel& m){
    if(c.previous.title!=m.title)SetWindowTextW(c.window,m.title.c_str());
    if(c.previous.status!=m.status)SetWindowTextW(c.status,m.status.c_str());
    if(c.previous.details!=m.details){
        const auto line=SendMessageW(c.details,EM_GETFIRSTVISIBLELINE,0,0);DWORD start=0,end=0;SendMessageW(c.details,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        SetWindowTextW(c.details,m.details.c_str());const auto length=static_cast<DWORD>(GetWindowTextLengthW(c.details));SendMessageW(c.details,EM_SETSEL,std::min(start,length),std::min(end,length));
        SendMessageW(c.details,EM_LINESCROLL,0,line-SendMessageW(c.details,EM_GETFIRSTVISIBLELINE,0,0));
    }
    for(const auto& [action,button]:c.buttons)EnableWindow(button,m.buttons.at(action)&&!c.commands.closing);c.previous=m;
}
}
int RunValidationWindow(HINSTANCE module,const Catalog& catalog,const MhmApi& api,IdentityQuery identity,const std::filesystem::path& logDirectory){
    SdkSession sdk;Require(sdk.Connect(api,identity)==MHM_OK,"SDK or identity extension unavailable");
    std::filesystem::create_directories(logDirectory);const auto log=logDirectory/(L"session-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".ndjson");
    FileTraceWriter trace(log);Controller controller(sdk,catalog,trace);Context c;c.module=module;bool failedExit=false;
    const auto oldDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=module;wc.lpfnWndProc=Procedure;wc.lpszClassName=ClassName;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    c.registered=RegisterClassExW(&wc)!=0;Require(c.registered||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Cannot register validation window");
    c.window=CreateWindowExW(0,ClassName,L"形态键 · SDK现场对照",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,940,740,nullptr,nullptr,module,&c);
    Require(c.window!=nullptr,"Cannot create validation window");
    c.font=CreateFontW(-MulDiv(10,static_cast<int>(GetDpiForWindow(c.window)),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    c.status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,1,1,c.window,nullptr,module,nullptr);
    c.details=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL,0,0,1,1,c.window,nullptr,module,nullptr);
    const std::array<std::pair<Action,const wchar_t*>,8> labels{{{Action::PreviousCase,L"上一例"},{Action::Begin,L"开始本例"},{Action::NextCase,L"下一例"},{Action::ZeroAll,L"全部归零"},{Action::End,L"结束"},{Action::PreviousStep,L"前一步权重"},{Action::RepeatStep,L"重复当前步骤"},{Action::NextStep,L"后一步权重"}}};
    for(const auto& [action,label]:labels){auto button=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,1,1,c.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+static_cast<int>(action))),module,nullptr);Require(button!=nullptr,"Cannot create validation button");c.buttons[action]=button;}
    Require(c.status&&c.details,"Cannot create validation text controls");
    const auto font=c.font?reinterpret_cast<WPARAM>(c.font):reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));SendMessageW(c.status,WM_SETFONT,font,TRUE);SendMessageW(c.details,WM_SETFONT,font,TRUE);for(const auto& [a,b]:c.buttons){(void)a;SendMessageW(b,WM_SETFONT,font,TRUE);}
    SendMessageW(c.details,EM_SETLIMITTEXT,1024*1024,0);Layout(c);controller.Tick(GetTickCount64());Update(c,MakeWindowModel(controller.GetSnapshot()));ShowWindow(c.window,SW_SHOWNORMAL);
    for(;;){
        MSG msg{};for(unsigned count=0;count<64&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++count){if(msg.message==WM_QUIT){c.Queue(Action::End);}else if(!IsDialogMessageW(c.window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        try{
            while(!c.commands.actions.empty()){const auto action=c.commands.actions.front();c.commands.actions.pop_front();controller.Dispatch(action,GetTickCount64());}
            controller.Tick(GetTickCount64());const auto snapshot=controller.GetSnapshot();
            if(c.textFailure){SetWindowTextW(c.status,L"界面文字编码或显示失败，正在按结束流程归零／释放；详细状态见操作记录。");}else Update(c,MakeWindowModel(snapshot));
            if(snapshot.finished&&c.commands.closing){if(!snapshot.normalEnd)MessageBoxW(c.window,L"本次结束未确认归零。已释放本工具来源，其他来源可能恢复；请保留操作记录。",L"结束状态未确认",MB_OK|MB_ICONWARNING);if(IsWindow(c.window))DestroyWindow(c.window);break;}
        }catch(...){
            c.textFailure=true;
            if(c.commands.closing){const auto snapshot=controller.GetSnapshot();sdk.Release();TraceRecord record;record.event="window_failed_cleanup_unconfirmed";record.phase="interrupted";record.error="Window processing failed; zero not confirmed";record.reference=snapshot.reference;record.observation=snapshot.observation;record.targets=snapshot.targets;trace.Append(record);failedExit=true;break;}
            c.Queue(Action::End);
        }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT);
    }
    if(oldDpi)SetThreadDpiAwarenessContext(oldDpi);return !failedExit&&controller.GetSnapshot().normalEnd?0:1;
}
}
