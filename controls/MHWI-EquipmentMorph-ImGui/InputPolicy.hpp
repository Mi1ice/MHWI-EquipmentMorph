#pragma once
#include <Windows.h>
#include <mutex>
#include <utility>

namespace morphgui {
inline bool ToggleKey(UINT key,bool repeated,bool textInput,unsigned modifiers){return key==VK_DELETE&&!repeated&&!textInput&&modifiers==0;}
inline bool ImeMessage(UINT message){
    return (message>=WM_IME_STARTCOMPOSITION&&message<=WM_IME_COMPOSITION)||(message>=WM_IME_SETCONTEXT&&message<=WM_IME_KEYUP);
}
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
