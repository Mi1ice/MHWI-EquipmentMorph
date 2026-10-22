#pragma once
#include <Windows.h>
#include <mutex>
#include <utility>
#include <optional>
#include <atomic>
#include <cstdint>
#include <array>

namespace morphgui {
inline bool ToggleKey(UINT key,bool repeated,bool textInput,unsigned modifiers){return key==VK_DELETE&&!repeated&&!textInput&&modifiers==0;}
// Keep the panel's real cursor visible, but let host show/hide-until-done loops finish.
inline int SuppressedCursorCount(BOOL shown){return shown?0:-1;}
inline bool ImeMessage(UINT message){
    return (message>=WM_IME_STARTCOMPOSITION&&message<=WM_IME_COMPOSITION)||(message>=WM_IME_SETCONTEXT&&message<=WM_IME_KEYUP);
}
inline bool PanelMouseNotice(UINT message){return message==WM_MOUSELEAVE||message==WM_NCMOUSELEAVE||message==WM_CAPTURECHANGED;}
inline constexpr std::array<int,5> MouseVirtualKeys={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
struct MouseButtonMessage {int button=-1;bool down=false;};
inline MouseButtonMessage MouseButton(UINT message,WPARAM value){
    switch(message){
    case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:return {0,true};case WM_LBUTTONUP:return {0,false};
    case WM_RBUTTONDOWN:case WM_RBUTTONDBLCLK:return {1,true};case WM_RBUTTONUP:return {1,false};
    case WM_MBUTTONDOWN:case WM_MBUTTONDBLCLK:return {2,true};case WM_MBUTTONUP:return {2,false};
    case WM_XBUTTONDOWN:case WM_XBUTTONDBLCLK:case WM_XBUTTONUP:
        if(HIWORD(value)==XBUTTON1||HIWORD(value)==XBUTTON2)return {HIWORD(value)==XBUTTON1?3:4,message!=WM_XBUTTONUP};
    }return {};
}
// Preserve native click sequences queued between frames; repair only idle channels.
inline unsigned MouseRepairs(unsigned physical,unsigned rendered,unsigned queued){return (physical^rendered)&~queued&31u;}
inline unsigned LogicalMouse(unsigned physical,bool swapped){return swapped?(physical&~3u)|((physical&1u)<<1)|((physical&2u)>>1):physical;}
inline bool SupersededMouse(uint32_t messageTick,uint32_t repairedTick){return repairedTick!=0&&static_cast<int32_t>(messageTick-repairedTick)<0;}
// Unclip only after NewFrame has consumed the release, including trickled events.
class MouseReleaseHandoff {
    std::mutex mutex;uint64_t generation=0;bool held=false,pending=false,posted=false;
public:
    void Begin(){std::scoped_lock lock(mutex);++generation;held=true;pending=posted=false;}
    void End(){std::scoped_lock lock(mutex);if(held){held=false;pending=true;posted=false;}}
    bool Waiting(){std::scoped_lock lock(mutex);return pending;}
    std::optional<uint64_t> Ready(bool down,bool queuedButtons){std::scoped_lock lock(mutex);if(!pending||posted||down||queuedButtons)return {};posted=true;return generation;}
    bool Complete(uint64_t token,bool physicalDown){std::scoped_lock lock(mutex);if(token!=generation||!pending||!posted)return false;posted=false;if(physicalDown)return false;pending=false;return true;}
    void PostFailed(uint64_t token){std::scoped_lock lock(mutex);if(token==generation)posted=false;}
    void Cancel(){std::scoped_lock lock(mutex);++generation;held=pending=posted=false;}
};
enum class InputPhase:uint16_t {Idle,Entry,Platform,Hotkey,Visibility,Drag,GuiWait,Backend,Route,Host};
struct InputTrace {
    std::atomic_uint64_t active=0,longest=0;
    static uint64_t Pack(UINT message,InputPhase phase,uint32_t tick){return uint64_t(tick)<<32|uint64_t(phase)<<16|(message&0xffff);}
    struct Scope {
        InputTrace& trace;UINT message;uint32_t start=static_cast<uint32_t>(GetTickCount64());uint64_t previous;
        Scope(InputTrace& value,UINT msg):trace(value),message(msg),previous(trace.active.exchange(Pack(msg,InputPhase::Entry,start))){}
        void Phase(InputPhase phase){trace.active=Pack(message,phase,static_cast<uint32_t>(GetTickCount64()));}
        ~Scope(){const auto elapsed=uint64_t(static_cast<uint32_t>(GetTickCount64())-start);auto old=trace.longest.load();while(old<elapsed&&!trace.longest.compare_exchange_weak(old,elapsed)){}trace.active=previous;}
    };
};
struct PlatformUpdate {bool text=false;LONG x=0,y=0;int cursor=0;bool operator==(const PlatformUpdate&)const=default;};
// Render only publishes values. IMM and native cursor calls run on the HWND thread.
class PlatformMailbox {
    std::mutex mutex;PlatformUpdate latest;bool queued=false;
public:
    bool Ime(bool text,LONG x,LONG y){std::scoped_lock lock(mutex);if(latest.text==text&&latest.x==x&&latest.y==y)return false;latest.text=text;latest.x=x;latest.y=y;return !std::exchange(queued,true);}
    bool Cursor(int cursor){std::scoped_lock lock(mutex);if(latest.cursor==cursor)return false;latest.cursor=cursor;return !std::exchange(queued,true);}
    PlatformUpdate Take(){std::scoped_lock lock(mutex);queued=false;return latest;}
};
}
