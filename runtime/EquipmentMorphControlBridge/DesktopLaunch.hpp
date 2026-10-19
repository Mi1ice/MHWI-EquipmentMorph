#pragma once
#include <filesystem>
#include <optional>
#include <fstream>

namespace mhmcontrol {
inline std::optional<std::filesystem::path> DesktopPath(const std::filesystem::path& game){
    if(_wcsicmp(game.filename().c_str(),L"MonsterHunterWorld.exe")!=0)return {};
    return game.parent_path()/L"MHWI-EquipmentMorph"/L"MHWI-EquipmentMorph-ControlCenter.exe";
}
inline std::wstring DesktopCommand(const std::filesystem::path& desktop,DWORD pid){
    return L"\""+desktop.wstring()+L"\" --game-pid "+std::to_wstring(pid);
}
inline void StartDesktop() noexcept {
    try{
        wchar_t executable[32768]{};const auto size=GetModuleFileNameW(nullptr,executable,32768);
        if(!size||size>=32768)return;
        const auto desktop=DesktopPath(executable);if(!desktop)return;
        const auto attributes=GetFileAttributesW(desktop->c_str());
        // Older installations have no ControlCenter. Keep their manual client usable.
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return;
        auto command=DesktopCommand(*desktop,GetCurrentProcessId());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION process{};
        const bool started=CreateProcessW(desktop->c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
            nullptr,desktop->parent_path().c_str(),&startup,&process)!=FALSE;
        const auto error=started?0:GetLastError();
        if(started){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
        const auto log=std::filesystem::path(executable).parent_path()/L"nativePC"/L"plugins"/L"MHWEquipmentMorph"/L"logs";
        std::error_code ignored;std::filesystem::create_directories(log,ignored);
        std::ofstream out(log/(L"control-center-startup-"+std::to_wstring(GetCurrentProcessId())+L".json"));
        out<<"{\"schema\":\"mhwi-control-center-startup-v1\",\"started\":"<<(started?"true":"false")
            <<",\"pid\":"<<GetCurrentProcessId()<<",\"win32_error\":"<<error<<"}\n";
    }catch(...){ /* Desktop startup must never disable the game bridge. */ }
}
}
