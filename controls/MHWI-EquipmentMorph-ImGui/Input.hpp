#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <imm.h>
#include <safetyhook.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_win32.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <wrl/client.h>
#include "InputPolicy.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace morphgui {
using Microsoft::WRL::ComPtr;
struct Input {
    inline static Input* instance=nullptr;
    inline static thread_local bool internal=false;
    std::recursive_mutex gui;
    ImGuiContext* context=nullptr;
    std::atomic<HWND> window=nullptr;std::atomic<WNDPROC> previous=nullptr;
    std::atomic_bool visible=false,available=false,releaseKeys=false,releaseMouse=false,closeRequested=false,textInput=false;
    std::atomic_uint64_t toggles=0,blockedMessages=0,blockedPolls=0,hotkeyMessages=0;
    std::atomic_uint lastHotkeyModifiers=0;
    std::atomic_bool hotkeyRegistered=false;std::atomic_uint hotkeyError=0;
    static constexpr int hotkeyId=0x4d48;
    UINT message=RegisterWindowMessageW(L"MHWI EquipmentMorph ImGui visibility v1");
    std::array<safetyhook::InlineHook,13> hooks;
    int cursorAdjustments=0;RECT clip{};bool clipNull=false;POINT anchor{};
    std::mutex cursorState;PlatformMailbox platform;int platformCursor=ImGuiMouseCursor_Arrow;bool dragClipped=false,imeAssociated=false;
    MouseReleaseHandoff dragHandoff;InputTrace trace;
    std::atomic_uint64_t mouseLeaves=0,captureChanges=0,dragReleases=0;
    std::atomic_bool renderedLeft=false;std::atomic_uint queuedLeft=0;
    std::atomic_uint nativeMouse=0,physicalMouse=0;std::atomic_bool mouseSyncQueued=false;
    std::atomic_uint64_t mouseRepairs=0,nativeLeftDown=0,nativeLeftUp=0;
    std::atomic_uint64_t cursorShowCalls=0,cursorHideCalls=0,cursorSuppressions=0;
    std::array<std::atomic_uint32_t,5> mouseRepairTick{};std::atomic_uint64_t staleMouseMessages=0;
    std::atomic_int mouseX=0,mouseY=0;
    HIMC panelIme=nullptr,previousIme=nullptr;
    std::atomic<HCURSOR> nativeCursor=nullptr;
    std::atomic_uint imeError=0;std::atomic_uint64_t imeActivations=0,imeCompositions=0,languageChanges=0,dragBounds=0;
    struct OwnCalls {bool before=internal;OwnCalls(){internal=true;}~OwnCalls(){internal=before;}};
    enum {Async,Key,Keyboard,Warp,Clip,CursorCount,CursorShape,KeyboardState,KeyboardData,MouseState,MouseData,RawData,Translate};
    bool Capturing()const{return available.load()&&visible.load()&&GetForegroundWindow()==window;}
    SHORT Physical(int key)const{return hooks[Async].original<decltype(&GetAsyncKeyState)>()(key);}
    SHORT MessageKey(int key)const{OwnCalls own;return hooks[Key].original<decltype(&GetKeyState)>()(key);}
    unsigned Modifiers()const{return ((MessageKey(VK_CONTROL)&0x8000)?1u:0u)|((MessageKey(VK_SHIFT)&0x8000)?2u:0u)|((MessageKey(VK_MENU)&0x8000)?4u:0u)|(((MessageKey(VK_LWIN)|MessageKey(VK_RWIN))&0x8000)?8u:0u);}
    void WakePlatform(){if(window)PostMessageW(window,message,4,0);}
    void ImeRequest(ImGuiViewport* viewport,ImGuiPlatformImeData* data){
        const bool wanted=data->WantTextInput&&Capturing();textInput=wanted;
        if(platform.Ime(data->WantTextInput,static_cast<LONG>(data->InputPos.x-viewport->Pos.x),static_cast<LONG>(data->InputPos.y-viewport->Pos.y)))WakePlatform();
    }
    void CursorRequest(int cursor){if(platform.Cursor(cursor))WakePlatform();}
    void RestoreIme(){
        OwnCalls own;if(!imeAssociated)return;
        ImmNotifyIME(panelIme,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmAssociateContext(window,previousIme);previousIme=nullptr;imeAssociated=false;
    }
    void NativeCursor(){
        OwnCalls own;POINT point{};RECT client{};
        if(!Capturing()||!GetCursorPos(&point)||!ScreenToClient(window,&point)||!GetClientRect(window,&client)||!PtInRect(&client,point))return;
        const wchar_t* shape=IDC_ARROW;
        switch(platformCursor){case ImGuiMouseCursor_TextInput:shape=IDC_IBEAM;break;case ImGuiMouseCursor_ResizeAll:shape=IDC_SIZEALL;break;
        case ImGuiMouseCursor_ResizeNS:shape=IDC_SIZENS;break;case ImGuiMouseCursor_ResizeEW:shape=IDC_SIZEWE;break;
        case ImGuiMouseCursor_ResizeNESW:shape=IDC_SIZENESW;break;case ImGuiMouseCursor_ResizeNWSE:shape=IDC_SIZENWSE;break;
        case ImGuiMouseCursor_Hand:shape=IDC_HAND;break;case ImGuiMouseCursor_NotAllowed:shape=IDC_NO;break;}
        const auto cursor=platformCursor==ImGuiMouseCursor_None?nullptr:LoadCursorW(nullptr,shape);
        hooks[CursorShape].original<decltype(&SetCursor)>()(cursor);nativeCursor=cursor;
    }
    void ApplyPlatform(){
        const auto update=platform.Take();OwnCalls own;platformCursor=update.cursor;textInput=update.text&&Capturing();
        if(textInput.load()){
            if(!imeAssociated){
                if(!panelIme)panelIme=ImmCreateContext();
                if(panelIme){
                    imeAssociated=true;previousIme=ImmAssociateContext(window,panelIme);const auto actual=ImmGetContext(window);
                    const bool attached=actual==panelIme;if(actual)ImmReleaseContext(window,actual);
                    if(attached){ImmSetOpenStatus(panelIme,TRUE);imeActivations++;imeError=0;}
                    else{ImmAssociateContext(window,previousIme);previousIme=nullptr;imeAssociated=false;imeError=ERROR_INVALID_HANDLE;}
                }
                else imeError=ERROR_NOT_SUPPORTED;
            }
            if(imeAssociated){
                RECT client{};GetClientRect(window,&client);POINT caret{(std::clamp)(update.x,0L,(std::max)(0L,client.right-1)),(std::clamp)(update.y,0L,(std::max)(0L,client.bottom-1))};
                COMPOSITIONFORM composition{};composition.dwStyle=CFS_FORCE_POSITION;composition.ptCurrentPos=caret;ImmSetCompositionWindow(panelIme,&composition);
                CANDIDATEFORM candidate{};candidate.dwStyle=CFS_CANDIDATEPOS;candidate.ptCurrentPos=caret;ImmSetCandidateWindow(panelIme,&candidate);
            }
        }else RestoreIme();
        SynchronizeHotkey();NativeCursor();
    }
    void BoundDrag(bool held){
        OwnCalls own;
        if(held&&Capturing()){
            RECT client{};POINT corner{};if(!GetClientRect(window,&client)||client.right<=0||client.bottom<=0||!ClientToScreen(window,&corner))return;
            OffsetRect(&client,corner.x,corner.y);if(hooks[Clip].original<decltype(&ClipCursor)>()(&client)){dragClipped=true;dragBounds++;}
        }else if(dragClipped){hooks[Clip].original<decltype(&ClipCursor)>()(nullptr);dragClipped=false;}
    }
    unsigned PhysicalMouse()const{unsigned mask=0;for(unsigned i=0;i<MouseVirtualKeys.size();i++)if(Physical(MouseVirtualKeys[i])&0x8000)mask|=1u<<i;return LogicalMouse(mask,GetSystemMetrics(SM_SWAPBUTTON)!=0);}
    void PostMouseSync(){if(!mouseSyncQueued.exchange(true))if(!PostMessageW(window,message,6,0))mouseSyncQueued=false;}
    void SyncMouse(){
        OwnCalls own;mouseSyncQueued=false;if(!Capturing())return;
        const auto mask=PhysicalMouse(),before=nativeMouse.exchange(mask);
        if((mask&1)&&!(before&1)){dragHandoff.Begin();BoundDrag(true);}
        else if(!(mask&1)&&(before&1))dragHandoff.End();
        if(!mask&&GetCapture()==window)ReleaseCapture();
    }
    // Ordinary messages remain the fast path, including clicks wholly between frames.
    void PollMouse(){
        if(!Capturing())return;OwnCalls own;POINT point{};
        if(!GetCursorPos(&point)||!ScreenToClient(window,&point))return;
        mouseX=point.x;mouseY=point.y;ImGui::GetIO().AddMousePosEvent(static_cast<float>(point.x),static_cast<float>(point.y));
        unsigned queued=0,rendered=0;auto& io=ImGui::GetIO();
        for(const auto& event:context->InputEventsQueue)if(event.Type==ImGuiInputEventType_MouseButton)queued|=1u<<event.MouseButton.Button;
        for(unsigned i=0;i<MouseVirtualKeys.size();i++)if(io.MouseDown[i])rendered|=1u<<i;
        const auto physical=PhysicalMouse(),repair=MouseRepairs(physical,rendered,queued);physicalMouse=physical;
        for(unsigned i=0;i<MouseVirtualKeys.size();i++)if(repair&(1u<<i)){mouseRepairTick[i]=static_cast<uint32_t>(GetTickCount64());io.AddMouseButtonEvent(static_cast<int>(i),(physical&(1u<<i))!=0);mouseRepairs++;}
        if(repair||nativeMouse.load()!=physical)PostMouseSync();
    }
    // Called under gui after NewFrame; never calls native cursor APIs on render.
    void FrameInput(){
        renderedLeft=ImGui::GetIO().MouseDown[0];unsigned waiting=0;
        if(dragHandoff.Waiting())for(const auto& event:context->InputEventsQueue)
            if(event.Type==ImGuiInputEventType_MouseButton&&event.MouseButton.Button==0)waiting++;
        queuedLeft=waiting;
        if(const auto token=dragHandoff.Ready(renderedLeft.load(),waiting!=0))
            if(!PostMessageW(window,message,5,static_cast<LPARAM>(*token)))dragHandoff.PostFailed(*token);
    }
    bool Neutral(bool mouse)const{
        for(int key=1;key<256;key++){
            const bool button=key==VK_LBUTTON||key==VK_RBUTTON||key==VK_MBUTTON||key==VK_XBUTTON1||key==VK_XBUTTON2;
            if(button==mouse&&(Physical(key)&0x8000))return false;
        }return true;
    }
    bool BlockPoll(bool mouse){
        if(internal||GetForegroundWindow()!=window)return false;
        if(Capturing())return true;
        auto& barrier=mouse?releaseMouse:releaseKeys;
        if(!barrier.load())return false;
        if(Neutral(mouse)){barrier=false;return false;}return true;
    }
    void Visibility(bool shown){
        OwnCalls own;
        shown=shown&&available.load()&&GetForegroundWindow()==window;
        {std::scoped_lock lock(gui);if(shown==visible.load())return;visible=shown;closeRequested=false;
            if(context){ImGui::SetCurrentContext(context);ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearEventsQueue();}}
        if(shown){
            GetCursorPos(&anchor);RECT originalClip{};GetClipCursor(&originalClip);{std::scoped_lock lock(cursorState);clip=originalClip;clipNull=false;}
            hooks[Clip].original<decltype(&ClipCursor)>()(nullptr);
            cursorAdjustments=0;
            while(cursorAdjustments<32){cursorAdjustments++;if(hooks[CursorCount].original<decltype(&ShowCursor)>()(TRUE)>=0)break;}
            NativeCursor();
            WakePlatform();
        }else{
            releaseKeys=true;releaseMouse=true;
            dragHandoff.Cancel();BoundDrag(false);nativeMouse=0;mouseSyncQueued=false;textInput=false;RestoreIme();
            if(GetCapture()==window)ReleaseCapture();
            while(cursorAdjustments>0){hooks[CursorCount].original<decltype(&ShowCursor)>()(FALSE);cursorAdjustments--;}
            RECT restore{};bool unrestricted=false;{std::scoped_lock lock(cursorState);restore=clip;unrestricted=clipNull;}
            hooks[Clip].original<decltype(&ClipCursor)>()(GetForegroundWindow()==window&&!unrestricted?&restore:nullptr);
        }
        toggles++;SynchronizeHotkey();
    }
    bool Attach(HWND target){
        if(window==target)return true;
        if(window)return false; // One game window; do not subclass secondary/overlay windows.
        previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(target,GWLP_WNDPROC));
        window=target;SetLastError(0);
        previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&Procedure)));
        if(!previous){window=nullptr;return false;}PostMessageW(target,message,3,0);return true;
    }
    void SynchronizeHotkey(){
        // Del is a panel shortcut except while a text field needs normal deletion.
        if(GetForegroundWindow()==window&&!textInput.load()){
            if(!hotkeyRegistered){hotkeyRegistered=RegisterHotKey(window,hotkeyId,MOD_NOREPEAT,VK_DELETE)!=FALSE;hotkeyError=hotkeyRegistered?0:GetLastError();}
        }else if(hotkeyRegistered.exchange(false))UnregisterHotKey(window,hotkeyId);
    }
    void RequestClose(){if(window)PostMessageW(window,message,0,0);}
    void Toggle(){if(visible.load())closeRequested=true;else Visibility(true);}
    static LRESULT CALLBACK Procedure(HWND w,UINT m,WPARAM a,LPARAM b){
        auto& self=*instance;
        InputTrace::Scope trace(self.trace,m);
        if(m==self.message){
            trace.Phase(InputPhase::Platform);
            if(a==6)self.SyncMouse();
            else if(a==5){if(self.dragHandoff.Complete(static_cast<uint64_t>(b),(self.PhysicalMouse()&1)!=0)){self.BoundDrag(false);self.dragReleases++;}}
            else if(a==4)self.ApplyPlatform();else if(a==3)self.SynchronizeHotkey();else if(a==2)self.Toggle();else self.Visibility(a!=0);return 0;
        }
        const auto mouseButton=MouseButton(m,a);const auto mouseTick=mouseButton.button>=0?static_cast<uint32_t>(GetMessageTime()):0;
        if(self.Capturing()&&mouseButton.button>=0&&SupersededMouse(mouseTick,self.mouseRepairTick[mouseButton.button].load())){self.staleMouseMessages++;self.PostMouseSync();return 0;}
        trace.Phase(InputPhase::Hotkey);
        if(m==WM_SETFOCUS||m==WM_ACTIVATE)self.SynchronizeHotkey();
        if(m==WM_HOTKEY&&a==hotkeyId&&self.hotkeyRegistered){
            self.hotkeyMessages++;self.lastHotkeyModifiers=0;if(!self.textInput.load())self.Toggle();return 0;
        }
        if(!self.hotkeyRegistered&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)&&ToggleKey(static_cast<UINT>(a),(b&(1ll<<30))!=0,self.textInput.load(),self.Modifiers())){
            self.hotkeyMessages++;self.lastHotkeyModifiers=0;self.Toggle();return 0;
        }
        if(m==WM_KILLFOCUS||m==WM_CANCELMODE||(m==WM_SIZE&&a==SIZE_MINIMIZED)){
            trace.Phase(InputPhase::Visibility);
            self.Visibility(false);if(m!=WM_CANCELMODE&&self.hotkeyRegistered.exchange(false))UnregisterHotKey(w,hotkeyId);
        }
        bool resetMouse=false;
        if(self.Capturing()){
            trace.Phase(InputPhase::Drag);
            if(const auto button=MouseButton(m,a);button.button>=0){const auto bit=1u<<button.button;if(button.down)self.nativeMouse.fetch_or(bit);else self.nativeMouse.fetch_and(~bit);if(button.button==0){if(button.down)self.nativeLeftDown++;else self.nativeLeftUp++;}}
            if(m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK){self.dragHandoff.Begin();self.BoundDrag(true);}
            else if(m==WM_LBUTTONUP)self.dragHandoff.End();
            else if(m==WM_CAPTURECHANGED){
                self.captureChanges++;
                if(!self.dragHandoff.Waiting()||b!=0){self.dragHandoff.Cancel();self.BoundDrag(false);self.nativeMouse=0;resetMouse=true;}
            }else if(m==WM_SIZE&&self.dragClipped)self.BoundDrag(true);
            if(m==WM_MOUSELEAVE||m==WM_NCMOUSELEAVE)self.mouseLeaves++;
            if(m==WM_INPUTLANGCHANGEREQUEST){OwnCalls own;return DefWindowProcW(w,m,a,b);}
            if(m==WM_INPUTLANGCHANGE){self.languageChanges++;if(self.textInput.load()){OwnCalls own;if(self.imeAssociated&&ImmIsIME(reinterpret_cast<HKL>(b)))ImmSetOpenStatus(self.panelIme,TRUE);self.WakePlatform();}}
        }
        if(self.imeAssociated&&ImeMessage(m)){if(m==WM_IME_COMPOSITION)self.imeCompositions++;OwnCalls own;return DefWindowProcW(w,m,a,b);}
        if(self.visible.load()){
            trace.Phase(InputPhase::GuiWait);
            std::scoped_lock lock(self.gui);OwnCalls own;
            trace.Phase(InputPhase::Backend);
            const bool staleMouse=self.Capturing()&&mouseButton.button>=0&&SupersededMouse(mouseTick,self.mouseRepairTick[mouseButton.button].load());
            if(staleMouse){self.staleMouseMessages++;self.PostMouseSync();}
            else if(self.context){ImGui::SetCurrentContext(self.context);if(resetMouse)for(int button=0;button<5;button++)ImGui::GetIO().AddMouseButtonEvent(button,false);ImGui_ImplWin32_WndProcHandler(w,m,a,b);}
        }
        trace.Phase(InputPhase::Route);
        if(const auto button=MouseButton(m,a);self.Capturing()&&button.button>=0&&!button.down&&self.nativeMouse.load()==0)self.PostMouseSync();
        const bool keyboard=m==WM_KEYDOWN||m==WM_KEYUP||m==WM_SYSKEYDOWN||m==WM_SYSKEYUP||m==WM_CHAR||m==WM_SYSCHAR||m==WM_UNICHAR;
        const bool mouse=m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST;
        if(self.Capturing()&&(keyboard||mouse||m==WM_INPUT||m==WM_SETCURSOR||PanelMouseNotice(m))){
            self.blockedMessages++;
            if(m==WM_INPUT)return DefWindowProcW(w,m,a,b); // Foreground raw input cleanup.
            if(m==WM_SETCURSOR){self.NativeCursor();return TRUE;}
            return 0;
        }
        if((keyboard&&self.BlockPoll(false))||(mouse&&self.BlockPoll(true)))return 0;
        if(m==WM_INPUTLANGCHANGE&&self.Capturing()){OwnCalls own;return DefWindowProcW(w,m,a,b);}
        if(m==WM_NCDESTROY){self.Visibility(false);self.RestoreIme();if(self.panelIme){ImmDestroyContext(self.panelIme);self.panelIme=nullptr;}if(self.hotkeyRegistered.exchange(false))UnregisterHotKey(w,hotkeyId);trace.Phase(InputPhase::Host);const auto result=CallWindowProcW(self.previous,w,m,a,b);self.window=nullptr;return result;}
        trace.Phase(InputPhase::Host);
        return CallWindowProcW(self.previous,w,m,a,b);
    }
    static SHORT WINAPI AsyncKey(int key){auto& s=*instance;const auto value=s.hooks[Async].original<decltype(&GetAsyncKeyState)>()(key);
        if(s.BlockPoll(key>=1&&key<=6)){s.blockedPolls++;return 0;}return value;}
    static SHORT WINAPI KeyState(int key){auto& s=*instance;const auto value=s.hooks[Key].original<decltype(&GetKeyState)>()(key);
        if(s.BlockPoll(key>=1&&key<=6)){s.blockedPolls++;return 0;}return value;}
    static BOOL WINAPI KeyboardStateFn(PBYTE bytes){auto& s=*instance;const auto result=s.hooks[Keyboard].original<decltype(&GetKeyboardState)>()(bytes);
        if(result&&bytes&&s.BlockPoll(false)){std::memset(bytes,0,256);s.blockedPolls++;}return result;}
    static BOOL WINAPI WarpCursor(int x,int y){auto& s=*instance;if(!internal&&s.Capturing())return TRUE;return s.hooks[Warp].original<decltype(&SetCursorPos)>()(x,y);}
    static BOOL WINAPI ClipMouse(const RECT* rect){auto& s=*instance;
        if(!internal&&s.Capturing()){std::scoped_lock lock(s.cursorState);if(rect)s.clip=*rect;s.clipNull=rect==nullptr;return TRUE;}
        return s.hooks[Clip].original<decltype(&ClipCursor)>()(rect);}
    static int WINAPI CursorVisibility(BOOL shown){auto& s=*instance;
        if(!internal){if(shown)s.cursorShowCalls++;else s.cursorHideCalls++;if(s.Capturing()){s.cursorSuppressions++;return SuppressedCursorCount(shown);}}
        return s.hooks[CursorCount].original<decltype(&ShowCursor)>()(shown);}
    static HCURSOR WINAPI Cursor(HCURSOR cursor){auto& s=*instance;
        if(!internal&&s.Capturing())return s.nativeCursor.load();return s.hooks[CursorShape].original<decltype(&SetCursor)>()(cursor);}
    static BOOL WINAPI TranslateInput(const MSG* message){auto& s=*instance;if(s.Capturing()&&s.textInput.load()){OwnCalls own;return s.hooks[Translate].original<decltype(&TranslateMessage)>()(message);}return s.hooks[Translate].original<decltype(&TranslateMessage)>()(message);}
    using DeviceState=HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*,DWORD,LPVOID);
    using DeviceData=HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
    static HRESULT ReadDevice(Input& s,unsigned hook,IDirectInputDevice8W* device,DWORD bytes,LPVOID data){
        const auto result=s.hooks[hook].original<DeviceState>()(device,bytes,data);
        const bool keyboard=bytes==256,mouse=bytes==sizeof(DIMOUSESTATE)||bytes==sizeof(DIMOUSESTATE2);
        if(SUCCEEDED(result)&&data&&(keyboard||mouse)&&s.BlockPoll(mouse)){std::memset(data,0,bytes);s.blockedPolls++;}return result;
    }
    static HRESULT ReadData(Input& s,unsigned hook,IDirectInputDevice8W* device,DWORD bytes,LPDIDEVICEOBJECTDATA data,LPDWORD count,DWORD flags){
        const auto result=s.hooks[hook].original<DeviceData>()(device,bytes,data,count,flags);
        if(SUCCEEDED(result)&&count&&!internal&&(s.Capturing()||s.releaseKeys.load()||s.releaseMouse.load())){
            DIDEVCAPS caps{};caps.dwSize=sizeof(caps);
            if(SUCCEEDED(device->GetCapabilities(&caps))){const auto type=GET_DIDEVICE_TYPE(caps.dwDevType);
                if((type==DI8DEVTYPE_KEYBOARD&&s.BlockPoll(false))||(type==DI8DEVTYPE_MOUSE&&s.BlockPoll(true))){*count=0;s.blockedPolls++;}}
        }return result;
    }
    static HRESULT STDMETHODCALLTYPE KeyboardRead(IDirectInputDevice8W* d,DWORD n,LPVOID p){return ReadDevice(*instance,KeyboardState,d,n,p);}
    static HRESULT STDMETHODCALLTYPE MouseRead(IDirectInputDevice8W* d,DWORD n,LPVOID p){return ReadDevice(*instance,MouseState,d,n,p);}
    static HRESULT STDMETHODCALLTYPE KeyboardReadData(IDirectInputDevice8W* d,DWORD n,LPDIDEVICEOBJECTDATA p,LPDWORD c,DWORD f){return ReadData(*instance,KeyboardData,d,n,p,c,f);}
    static HRESULT STDMETHODCALLTYPE MouseReadData(IDirectInputDevice8W* d,DWORD n,LPDIDEVICEOBJECTDATA p,LPDWORD c,DWORD f){return ReadData(*instance,MouseData,d,n,p,c,f);}
    static UINT WINAPI Raw(HRAWINPUT input,UINT command,LPVOID data,PUINT bytes,UINT header){auto& s=*instance;
        const auto result=s.hooks[RawData].original<decltype(&GetRawInputData)>()(input,command,data,bytes,header);
        if(result!=UINT(-1)&&data&&command==RID_INPUT&&s.Capturing()){
            const auto raw=static_cast<RAWINPUT*>(data);
            if(raw->header.dwType==RIM_TYPEKEYBOARD||raw->header.dwType==RIM_TYPEMOUSE){std::memset(data,0,result);*bytes=0;s.blockedPolls++;return 0;}
        }return result;
    }
    bool Install(){
        instance=this;
        std::array<void*,13> targets={reinterpret_cast<void*>(&GetAsyncKeyState),reinterpret_cast<void*>(&GetKeyState),reinterpret_cast<void*>(&GetKeyboardState),
            reinterpret_cast<void*>(&SetCursorPos),reinterpret_cast<void*>(&ClipCursor),reinterpret_cast<void*>(&ShowCursor),reinterpret_cast<void*>(&SetCursor),nullptr,nullptr,nullptr,nullptr,reinterpret_cast<void*>(&GetRawInputData),reinterpret_cast<void*>(&TranslateMessage)};
        std::array<void*,13> callbacks={reinterpret_cast<void*>(&AsyncKey),reinterpret_cast<void*>(&KeyState),reinterpret_cast<void*>(&KeyboardStateFn),
            reinterpret_cast<void*>(&WarpCursor),reinterpret_cast<void*>(&ClipMouse),reinterpret_cast<void*>(&CursorVisibility),reinterpret_cast<void*>(&Cursor),
            reinterpret_cast<void*>(&KeyboardRead),reinterpret_cast<void*>(&KeyboardReadData),reinterpret_cast<void*>(&MouseRead),reinterpret_cast<void*>(&MouseReadData),reinterpret_cast<void*>(&Raw),reinterpret_cast<void*>(&TranslateInput)};
        ComPtr<IDirectInput8W> input;ComPtr<IDirectInputDevice8W> keyboard,mouse;
        if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(input.GetAddressOf()),nullptr))
            ||FAILED(input->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr))||FAILED(input->CreateDevice(GUID_SysMouse,&mouse,nullptr)))return false;
        auto kb=*reinterpret_cast<void***>(keyboard.Get()),mb=*reinterpret_cast<void***>(mouse.Get());
        targets[KeyboardState]=kb[9];targets[KeyboardData]=kb[10];
        if(mb[9]!=kb[9])targets[MouseState]=mb[9];if(mb[10]!=kb[10])targets[MouseData]=mb[10];
        for(size_t i=0;i<hooks.size();i++)if(targets[i]){
            auto result=safetyhook::InlineHook::create(targets[i],callbacks[i],safetyhook::InlineHook::StartDisabled);
            if(!result)return false;hooks[i]=std::move(*result);
        }
        for(auto& hook:hooks)if(hook&&!hook.enable())return false;
        return true;
    }
};
}
