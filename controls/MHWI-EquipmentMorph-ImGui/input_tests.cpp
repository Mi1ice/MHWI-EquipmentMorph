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
    const auto cursorLoop=[](bool shown,const auto& call){unsigned calls=0;while(calls<32){++calls;const int count=call(shown);if(shown?count>=0:count<0)return calls;}return 0u;};
    check(cursorLoop(false,[](bool){return 0;})==0,"legacy zero return cannot terminate a host hide-until-negative loop");
    const auto panelCursor=[&](bool shown){return SuppressedCursorCount(shown);};
    check(cursorLoop(false,panelCursor)==1,"host hide-until-negative loop terminates on its first suppressed call");
    check(cursorLoop(true,panelCursor)==1,"host show-until-nonnegative loop terminates on its first suppressed call");
    bool cursorLoopsFinish=true;
    for(unsigned i=0;i<1000;i++)cursorLoopsFinish=cursorLoopsFinish&&cursorLoop(false,panelCursor)==1&&cursorLoop(true,panelCursor)==1;
    check(cursorLoopsFinish,"repeated host show and hide loops both continue to terminate");
    check(SuppressedCursorCount(2)>=0,"any nonzero Windows BOOL requests cursor display");
    check(ToggleKey(VK_DELETE,false,false,0),"plain Del toggles the panel");
    check(!ToggleKey(VK_DELETE,true,false,0),"holding Del cannot repeatedly toggle");
    check(!ToggleKey(VK_DELETE,false,true,0),"Del remains text deletion while editing");
    for(unsigned modifier:{1u,2u,4u,8u,3u})check(!ToggleKey(VK_DELETE,false,false,modifier),"modified Del is not the panel shortcut");
    check(!ToggleKey('M',false,false,3)&&!ToggleKey(VK_PROCESSKEY,false,false,0),"old shortcut and IME process keys do not invoke panel toggling");
    for(UINT message:{WM_IME_STARTCOMPOSITION,WM_IME_COMPOSITION,WM_IME_ENDCOMPOSITION,WM_IME_SETCONTEXT,WM_IME_CHAR,WM_IME_NOTIFY,WM_IME_REQUEST})check(ImeMessage(message),"private IME messages stay on the window thread");
    check(!ImeMessage(WM_CHAR)&&!ImeMessage(WM_KEYDOWN)&&!ImeMessage(WM_INPUTLANGCHANGE),"committed text and language updates take their dedicated routes");
    for(UINT message:{WM_MOUSELEAVE,WM_NCMOUSELEAVE,WM_CAPTURECHANGED})check(PanelMouseNotice(message),"panel mouse notices are isolated from host input while capturing");
    for(UINT message:{WM_KILLFOCUS,WM_ACTIVATE,WM_SIZE,WM_CANCELMODE})check(!PanelMouseNotice(message),"real host focus and window lifecycle notifications remain available");
    const std::array<UINT,5> downs={WM_LBUTTONDOWN,WM_RBUTTONDOWN,WM_MBUTTONDOWN,WM_XBUTTONDOWN,WM_XBUTTONDOWN};
    const std::array<UINT,5> ups={WM_LBUTTONUP,WM_RBUTTONUP,WM_MBUTTONUP,WM_XBUTTONUP,WM_XBUTTONUP};
    const std::array<UINT,5> doubles={WM_LBUTTONDBLCLK,WM_RBUTTONDBLCLK,WM_MBUTTONDBLCLK,WM_XBUTTONDBLCLK,WM_XBUTTONDBLCLK};
    for(unsigned i=0;i<5;i++){
        const WPARAM value=i<3?0:MAKEWPARAM(0,i==3?XBUTTON1:XBUTTON2);
        check(MouseButton(downs[i],value).button==static_cast<int>(i)&&MouseButton(downs[i],value).down,"native mouse down maps to the matching physical channel");
        check(MouseButton(ups[i],value).button==static_cast<int>(i)&&!MouseButton(ups[i],value).down,"native mouse up maps to the matching physical channel");
        check(MouseButton(doubles[i],value).button==static_cast<int>(i)&&MouseButton(doubles[i],value).down,"double click preserves native button ordering");
        const unsigned bit=1u<<i;
        check(MouseRepairs(bit,0,0)==bit&&MouseRepairs(0,bit,0)==bit,"lost down and lost up are both repairable for each mouse button");
        check(MouseRepairs(0,bit,bit)==0&&MouseRepairs(bit,0,bit)==0,"queued native presses and releases are consumed before polling correction");
    }
    check(MouseButton(WM_MOUSEMOVE,0).button<0&&MouseButton(WM_XBUTTONDOWN,MAKEWPARAM(0,3)).button<0,"movement and invalid X buttons cannot invent button transitions");
    check(MouseRepairs(31,31,0)==0&&MouseRepairs(0,0,0)==0,"matching physical and rendered mouse state never duplicates a click");
    check(MouseRepairs(31,0,30)==1&&MouseRepairs(0,31,1)==30,"one queued button does not prevent repair of independent idle channels");
    check(MouseRepairs(0xffff,0,0)==31,"only five supported mouse channels can be repaired");
    unsigned rendered=0,delivered=0;
    for(unsigned physical:{0u,1u,1u,1u,0u,0u}){const auto repair=MouseRepairs(physical,rendered,0);if(repair&1)delivered++;rendered=(rendered&~repair)|(physical&repair);}
    check(rendered==0&&delivered==2,"a click missing all native messages produces exactly one press and release across frames");
    check(MouseRepairs(0,0,1)==0,"a complete short native click queued between frames is preserved without adding physical events");
    check(LogicalMouse(1,false)==1&&LogicalMouse(2,false)==2,"ordinary physical left and right match native logical buttons");
    check(LogicalMouse(1,true)==2&&LogicalMouse(2,true)==1,"swapped system buttons match the logical Win32 event channels");
    check(LogicalMouse(31,true)==31&&LogicalMouse(28,true)==28,"button swapping preserves simultaneous presses and auxiliary buttons");
    check(!SupersededMouse(100,0),"native clicks are not discarded before any physical repair");
    check(SupersededMouse(100,200)&&!SupersededMouse(200,200)&&!SupersededMouse(201,200),"only events older than a physical repair are retired");
    check(SupersededMouse(0xfffffff0u,20)&&!SupersededMouse(20,0xfffffff0u),"mouse event ordering tolerates the Windows tick counter wrap");
    MouseReleaseHandoff drag;
    check(!drag.Waiting()&&!drag.Ready(false,false),"idle cursor handoff cannot request an unclip");
    drag.Begin();check(!drag.Waiting()&&!drag.Ready(false,false),"a held drag cannot be released by a render frame");
    drag.End();check(drag.Waiting()&&!drag.Ready(true,false),"native button up waits for the rendered button release");
    check(!drag.Ready(false,true),"trickled button events must drain before leaving the game");
    auto release=drag.Ready(false,false);check(release.has_value()&&!drag.Ready(false,false),"a consumed release posts exactly one window-thread acknowledgement");
    check(drag.Complete(*release,false)&&!drag.Waiting(),"window thread unclips after matching render acknowledgement");
    check(!drag.Complete(*release,false),"duplicate acknowledgements cannot release a newer capture");
    drag.Begin();drag.End();release=drag.Ready(false,false);drag.Begin();
    check(!drag.Complete(*release,false)&&!drag.Waiting(),"rapid re-press invalidates a previously posted release");
    drag.End();release=drag.Ready(false,false);check(release.has_value(),"the newer drag has an independent release");
    check(!drag.Complete(*release,true)&&drag.Waiting(),"physical re-press cannot unclip before its queued native event");
    release=drag.Ready(false,false);check(release.has_value(),"a physical-button race can retry on a later frame");
    drag.PostFailed(*release);release=drag.Ready(false,false);check(release.has_value(),"failed PostMessage does not strand the cursor in the game");
    drag.Cancel();check(!drag.Waiting()&&!drag.Complete(*release,false),"focus or capture loss cancels pending release acknowledgements");
    drag.Begin();drag.Cancel();drag.End();check(!drag.Waiting(),"a late button up cannot revive a cancelled drag");
    drag.Begin();drag.End();release=drag.Ready(false,false);drag.End();check(!drag.Ready(false,false)&&drag.Complete(*release,false),"duplicate native button ups preserve the posted acknowledgement");
    const auto trace=InputTrace::Pack(WM_MOUSELEAVE,InputPhase::Host,1234);
    check((trace&0xffff)==WM_MOUSELEAVE&&((trace>>16)&0xffff)==static_cast<uint16_t>(InputPhase::Host)&&(trace>>32)==1234,"diagnostic breadcrumb keeps message phase and timestamp coherent");
    InputTrace timing;{
        InputTrace::Scope outer(timing,WM_LBUTTONUP);outer.Phase(InputPhase::Backend);const auto parent=timing.active.load();
        {InputTrace::Scope nested(timing,WM_CAPTURECHANGED);nested.Phase(InputPhase::Route);check((timing.active.load()&0xffff)==WM_CAPTURECHANGED,"reentrant capture notice reports its own active message");}
        check(timing.active.load()==parent,"returning from capture notification restores the enclosing input phase");
    }check(timing.active.load()==0,"completed input messages leave no stale active breadcrumb");
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
