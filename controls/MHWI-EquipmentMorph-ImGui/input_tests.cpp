#define NOMINMAX
#include "InputPolicy.hpp"
#include "PanelLayout.hpp"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>

int main(){try{
    using namespace morphgui;unsigned checks=0;
    const auto check=[&](bool value,const char* why){if(!value)throw std::runtime_error(why);checks++;};
    check(ToggleKey(VK_DELETE,false,false,0),"plain Del toggles the panel");
    check(!ToggleKey(VK_DELETE,true,false,0),"holding Del cannot repeatedly toggle");
    check(!ToggleKey(VK_DELETE,false,true,0),"Del remains text deletion while editing");
    for(unsigned modifier:{1u,2u,4u,8u,3u})check(!ToggleKey(VK_DELETE,false,false,modifier),"modified Del is not the panel shortcut");
    check(!ToggleKey('M',false,false,3)&&!ToggleKey(VK_PROCESSKEY,false,false,0),"old shortcut and IME process keys do not invoke panel toggling");
    for(UINT message:{WM_IME_STARTCOMPOSITION,WM_IME_COMPOSITION,WM_IME_ENDCOMPOSITION,WM_IME_SETCONTEXT,WM_IME_CHAR,WM_IME_NOTIFY,WM_IME_REQUEST})check(ImeMessage(message),"private IME messages stay on the window thread");
    check(!ImeMessage(WM_CHAR)&&!ImeMessage(WM_KEYDOWN)&&!ImeMessage(WM_INPUTLANGCHANGE),"committed text and language updates take their dedicated routes");
    PlatformMailbox mailbox;
    check(mailbox.Ime(true,100,200),"first IME request schedules window work");
    check(!mailbox.Ime(true,110,210)&&!mailbox.Cursor(2),"IME and cursor changes coalesce into one pending message");
    auto update=mailbox.Take();check(update.text&&update.x==110&&update.y==210&&update.cursor==2,"window thread receives latest complete IME and cursor state");
    check(!mailbox.Ime(true,110,210)&&!mailbox.Cursor(2),"unchanged platform state does not post messages every frame");
    check(mailbox.Ime(false,110,210),"ending text input schedules context restoration");
    update=mailbox.Take();check(!update.text&&update.cursor==2,"restoring IME does not erase native cursor state");
    std::atomic_bool done=false;bool coherent=true;
    std::thread render([&](){for(LONG i=1;i<=5000;i++)mailbox.Ime(true,i,-i);done=true;});
    while(!done.load()){update=mailbox.Take();if(update.text&&update.y!=-update.x)coherent=false;}
    render.join();update=mailbox.Take();check(coherent&&update.text&&update.x==5000&&update.y==-5000,"concurrent frame updates never publish mixed caret coordinates or lose final state");
    check(mailbox.Cursor(3),"window work can be queued again after consumption");
    const auto right=ContainPanel({0,0,1920,1080},{2200,100,800,600});
    check(right.x==1120&&right.y==100,"dragging toward a secondary monitor stops at the game viewport edge");
    const auto left=ContainPanel({0,0,1920,1080},{-400,-100,800,600});
    check(left.x==0&&left.y==0,"negative drag coordinates keep the entire panel reachable");
    const auto bottom=ContainPanel({0,0,1920,1080},{200,1200,800,600});check(bottom.y==480,"bottom-edge dragging keeps the panel inside the game");
    const auto negative=ContainPanel({-1920,0,1920,1080},{-100,50,800,600});check(negative.x==-800,"a game viewport on a negative-coordinate monitor retains its origin");
    const auto resized=ContainPanel({0,0,1280,720},{50,20,1920,1080});check(resized.x==0&&resized.y==0&&resized.w==1280&&resized.h==720,"shrinking the game viewport cannot strand a larger panel outside it");
    const auto empty=ContainPanel({0,0,0,0},{10,20,400,300});check(empty.w==0&&empty.h==0&&empty.x==0&&empty.y==0,"zero-sized viewport calculations stay bounded");
    std::printf("PASS: %u input policy checks; no windows, input automation or game process\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
