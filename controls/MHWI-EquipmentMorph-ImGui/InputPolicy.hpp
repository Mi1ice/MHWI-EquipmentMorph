#pragma once
#include <Windows.h>
#include <mutex>
#include <utility>
#include <optional>
#include <atomic>
#include <cstdint>

namespace morphgui {
inline bool ToggleKey(UINT key,bool repeated,bool textInput,unsigned modifiers){return key==VK_DELETE&&!repeated&&!textInput&&modifiers==0;}
inline bool ImeMessage(UINT message){
    return (message>=WM_IME_STARTCOMPOSITION&&message<=WM_IME_COMPOSITION)||(message>=WM_IME_SETCONTEXT&&message<=WM_IME_KEYUP);
}
inline bool PanelMouseNotice(UINT message){return message==WM_MOUSELEAVE||message==WM_NCMOUSELEAVE||message==WM_CAPTURECHANGED;}
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
