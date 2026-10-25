#include "WindowModel.hpp"

namespace {
std::filesystem::path ModulePath(HMODULE module){std::wstring path(32768,L'\0');const auto count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));mhmvalidation::Require(count>0&&count<path.size(),"Module path unavailable or truncated");path.resize(count);return path;}
template<class T>T Procedure(HMODULE module,const char* name){const auto address=GetProcAddress(module,name);mhmvalidation::Require(address!=nullptr,"Required public SDK entry point unavailable");T result{};static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;}
struct CoreReference {HMODULE module=nullptr;~CoreReference(){if(module)FreeLibrary(module);}};
DWORD WINAPI Worker(void* argument){
    auto module=static_cast<HMODULE>(argument);HMODULE self=nullptr;DWORD result=1;std::filesystem::path directory;bool enteredWindow=false;
    try{
        const auto ownPath=ModulePath(module);directory=ownPath.parent_path();mhmvalidation::Require(GetModuleHandleExW(0,ownPath.c_str(),&self)!=FALSE,"Cannot retain validation module");
        CoreReference core;mhmvalidation::Require(GetModuleHandleExW(0,L"MHWEquipmentMorphConfiguredPrototype.dll",&core.module)!=FALSE,"Core module is not loaded");
        const auto getApi=Procedure<decltype(&MhwMorphGetApi)>(core.module,"MhwMorphGetApi");const auto identity=Procedure<mhmvalidation::IdentityQuery>(core.module,"MhwMorphGetAssetIdentity");
        MhmApi api{};mhmvalidation::Require(getApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK,"Core SDK version unavailable");
        const auto catalog=mhmvalidation::LoadCatalogFile(directory/L"MHWEquipmentMorphSdkValidation.cases.json");
        enteredWindow=true;result=static_cast<DWORD>(mhmvalidation::RunValidationWindow(module,catalog,api,identity,directory/L"MHWEquipmentMorphSdkValidation.logs"));
    }catch(const std::exception& error){
        try{if(!directory.empty()){const auto logs=directory/L"MHWEquipmentMorphSdkValidation.logs";std::filesystem::create_directories(logs);mhmvalidation::FileTraceWriter trace(logs/(L"startup-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".ndjson"));mhmvalidation::TraceRecord record;record.event=enteredWindow?"session_failed":"startup_failed";record.error=error.what();trace.Append(record);}}catch(...){}
        MessageBoxW(nullptr,enteredWindow?L"验收会话异常终止，归零未确认；自身来源按退出清理，其他来源可能恢复。请保留插件邻接的SdkValidation.logs记录。":L"验收工具未启动，未施加案例。请检查邻接案例文件、已加载核心及SDK身份扩展；错误记录在插件邻接的SdkValidation.logs目录。",L"形态键 · SDK现场对照",MB_OK|MB_ICONERROR);
    }catch(...){MessageBoxW(nullptr,L"验收工具启动失败；未能确认会话状态。",L"形态键 · SDK现场对照",MB_OK|MB_ICONERROR);}
    if(self)FreeLibraryAndExitThread(self,result);return result;
}
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID reserved){
    (void)reserved;if(reason==DLL_PROCESS_ATTACH){const auto thread=CreateThread(nullptr,0,Worker,module,0,nullptr);if(!thread)return FALSE;CloseHandle(thread);}return TRUE;
}
