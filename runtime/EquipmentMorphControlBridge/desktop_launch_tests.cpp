#include "Pipe.hpp"
#include "DesktopLaunch.hpp"
#include <cstdio>
#include <stdexcept>
int main(){try{
    unsigned checks=0;const auto check=[&](bool value,const char* why){if(!value)throw std::runtime_error(why);checks++;};
    check(!mhmcontrol::DesktopPath(L"C:/fixture/ControlPipeTests.exe"),"software pipe tests never autostart desktop");
    const auto path=mhmcontrol::DesktopPath(L"C:/fixture with spaces/MonsterHunterWorld.exe");
    check(path.has_value()&&path->filename()==L"MHWI-EquipmentMorph-ControlCenter.exe","distinct desktop filename");
    check(path->parent_path().filename()==L"MHWI-EquipmentMorph","launch from fixed adjacent directory");
    const auto command=mhmcontrol::DesktopCommand(*path,1234);
    check(command.front()==L'"'&&command.find(L"\" --game-pid 1234")!=std::wstring::npos,"quoted path and numeric process identity");
    check(!mhmcontrol::DesktopPath(L"MonsterHunterWorld.exe.bak"),"reject misleading process name");
    std::printf("PASS: %u desktop-launch checks; no process created\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
