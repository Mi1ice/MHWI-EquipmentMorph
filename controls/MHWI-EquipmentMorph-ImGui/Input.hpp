#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <imm.h>
#include <safetyhook.hpp>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <array>
#include <atomic>
#include <mutex>
#include <cstring>
#include <wrl/client.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace morphgui {
using Microsoft::WRL::ComPtr;
struct Input {
    inline static Input* instance=nullptr;
    inline static thread_local bool internal=false;
    std::recursive_mutex gui;
    ImGuiContext* context=nullptr;
    std::atomic<HWND> window=nullptr;std::atomic<WNDPROC> previous=nullptr;
    std::atomic_bool visible=false,available=false,releaseKeys=false,releaseMouse=false;
    std::atomic_uint64_t toggles=0,blockedMessages=0,blockedPolls=0,hotkeyMessages=0;
    std::atomic_uint lastHotkeyModifiers=0;
    std::atomic_bool hotkeyRegistered=false;std::atomic_uint hotkeyError=0;
    static constexpr int hotkeyId=0x4d48;
    UINT message=RegisterWindowMessageW(L"MHWI EquipmentMorph ImGui visibility v1");
    std::array<safetyhook::InlineHook,12> hooks;
    int cursorAdjustments=0;RECT clip{};bool clipNull=false;POINT anchor{};
    struct OwnCalls {bool before=internal;OwnCalls(){internal=true;}~OwnCalls(){internal=before;}};
    enum {Async,Key,Keyboard,Warp,Clip,CursorCount,CursorShape,KeyboardState,KeyboardData,MouseState,MouseData,RawData};
    bool Capturing()const{return available.load()&&visible.load()&&GetForegroundWindow()==window;}
    SHORT Physical(int key)const{return hooks[Async].original<decltype(&GetAsyncKeyState)>()(key);}
    SHORT MessageKey(int key)const{OwnCalls own;return hooks[Key].original<decltype(&GetKeyState)>()(key);}
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
        std::scoped_lock lock(gui);OwnCalls own;
        shown=shown&&available.load()&&GetForegroundWindow()==window;
        if(shown==visible.load())return;
        if(shown){
            GetCursorPos(&anchor);GetClipCursor(&clip);clipNull=false;
            hooks[Clip].original<decltype(&ClipCursor)>()(nullptr);
            cursorAdjustments=0;
            while(cursorAdjustments<32){cursorAdjustments++;if(hooks[CursorCount].original<decltype(&ShowCursor)>()(TRUE)>=0)break;}
            hooks[CursorShape].original<decltype(&SetCursor)>()(LoadCursorW(nullptr,IDC_ARROW));
        }else{
            visible=false;
            releaseKeys=true;releaseMouse=true;
            if(GetCapture()==window)ReleaseCapture();
            while(cursorAdjustments>0){hooks[CursorCount].original<decltype(&ShowCursor)>()(FALSE);cursorAdjustments--;}
            hooks[Clip].original<decltype(&ClipCursor)>()(GetForegroundWindow()==window&&!clipNull?&clip:nullptr);
        }
        visible=shown;toggles++;
        if(const auto ime=ImmGetContext(window)){ImmNotifyIME(ime,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(window,ime);}
        if(context){ImGui::SetCurrentContext(context);ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearEventsQueue();}
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
        // Register/unregister on the HWND's thread, and only while the game
        // has focus. WM_HOTKEY is recognized before IME translation.
        if(GetForegroundWindow()==window){
            if(!hotkeyRegistered){hotkeyRegistered=RegisterHotKey(window,hotkeyId,MOD_CONTROL|MOD_SHIFT|MOD_NOREPEAT,'M')!=FALSE;hotkeyError=hotkeyRegistered?0:GetLastError();}
        }else if(hotkeyRegistered.exchange(false))UnregisterHotKey(window,hotkeyId);
    }
    void RequestClose(){if(window)PostMessageW(window,message,0,0);}
    static LRESULT CALLBACK Procedure(HWND w,UINT m,WPARAM a,LPARAM b){
        auto& self=*instance;
        if(m==self.message){if(a==3)self.SynchronizeHotkey();else self.Visibility(a==2?!self.visible.load():a!=0);return 0;}
        if(m==WM_SETFOCUS||m==WM_ACTIVATE)self.SynchronizeHotkey();
        if(m==WM_HOTKEY&&a==hotkeyId&&self.hotkeyRegistered){
            self.hotkeyMessages++;self.lastHotkeyModifiers=3;self.Visibility(!self.visible.load());return 0;
        }
        // The host has already called TranslateMessage: ImmGetVirtualKey may
        // now return VK_PROCESSKEY. The original scan code is still available.
        const auto virtualKey=a==VK_PROCESSKEY?MapVirtualKeyExW((static_cast<ULONG_PTR>(b)>>16)&0xff,MAPVK_VSC_TO_VK,GetKeyboardLayout(0)):a;
        if(!self.hotkeyRegistered&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)&&virtualKey=='M'&&!(b&(1ll<<30))){
            const auto modifiers=((self.MessageKey(VK_CONTROL)&0x8000)?1u:0u)|((self.MessageKey(VK_SHIFT)&0x8000)?2u:0u);
            self.hotkeyMessages++;self.lastHotkeyModifiers=modifiers;
            if(modifiers==3){self.Visibility(!self.visible.load());return 0;}
        }
        if(m==WM_KILLFOCUS||m==WM_CANCELMODE||(m==WM_SIZE&&a==SIZE_MINIMIZED)){
            self.Visibility(false);if(m!=WM_CANCELMODE&&self.hotkeyRegistered.exchange(false))UnregisterHotKey(w,hotkeyId);
        }
        if(self.visible.load()){
            std::scoped_lock lock(self.gui);OwnCalls own;
            if(self.context){ImGui::SetCurrentContext(self.context);ImGui_ImplWin32_WndProcHandler(w,m,a,b);}
        }
        const bool keyboard=m==WM_KEYDOWN||m==WM_KEYUP||m==WM_SYSKEYDOWN||m==WM_SYSKEYUP||m==WM_CHAR||m==WM_SYSCHAR||m==WM_UNICHAR;
        const bool mouse=m>=WM_MOUSEFIRST&&m<=WM_MOUSELAST;
        if(self.Capturing()&&(keyboard||mouse||m==WM_INPUT||m==WM_SETCURSOR)){
            self.blockedMessages++;
            if(m==WM_INPUT)return DefWindowProcW(w,m,a,b); // Foreground raw input cleanup.
            if(m==WM_SETCURSOR){OwnCalls own;self.hooks[CursorShape].original<decltype(&SetCursor)>()(LoadCursorW(nullptr,IDC_ARROW));return TRUE;}
            return 0;
        }
        if((keyboard&&self.BlockPoll(false))||(mouse&&self.BlockPoll(true)))return 0;
        if(m==WM_NCDESTROY){self.Visibility(false);if(self.hotkeyRegistered.exchange(false))UnregisterHotKey(w,hotkeyId);const auto result=CallWindowProcW(self.previous,w,m,a,b);self.window=nullptr;return result;}
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
        if(!internal&&s.Capturing()){std::scoped_lock lock(s.gui);if(rect)s.clip=*rect;s.clipNull=rect==nullptr;return TRUE;}
        return s.hooks[Clip].original<decltype(&ClipCursor)>()(rect);}
    static int WINAPI CursorVisibility(BOOL shown){auto& s=*instance;if(!internal&&s.Capturing())return 0;return s.hooks[CursorCount].original<decltype(&ShowCursor)>()(shown);}
    static HCURSOR WINAPI Cursor(HCURSOR cursor){auto& s=*instance;
        if(!internal&&s.Capturing())cursor=LoadCursorW(nullptr,IDC_ARROW);return s.hooks[CursorShape].original<decltype(&SetCursor)>()(cursor);}
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
        std::array<void*,12> targets={reinterpret_cast<void*>(&GetAsyncKeyState),reinterpret_cast<void*>(&GetKeyState),reinterpret_cast<void*>(&GetKeyboardState),
            reinterpret_cast<void*>(&SetCursorPos),reinterpret_cast<void*>(&ClipCursor),reinterpret_cast<void*>(&ShowCursor),reinterpret_cast<void*>(&SetCursor),nullptr,nullptr,nullptr,nullptr,reinterpret_cast<void*>(&GetRawInputData)};
        std::array<void*,12> callbacks={reinterpret_cast<void*>(&AsyncKey),reinterpret_cast<void*>(&KeyState),reinterpret_cast<void*>(&KeyboardStateFn),
            reinterpret_cast<void*>(&WarpCursor),reinterpret_cast<void*>(&ClipMouse),reinterpret_cast<void*>(&CursorVisibility),reinterpret_cast<void*>(&Cursor),
            reinterpret_cast<void*>(&KeyboardRead),reinterpret_cast<void*>(&KeyboardReadData),reinterpret_cast<void*>(&MouseRead),reinterpret_cast<void*>(&MouseReadData),reinterpret_cast<void*>(&Raw)};
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
