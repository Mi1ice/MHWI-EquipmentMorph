#include "Pipe.hpp"
#include "Protocol.hpp"
#include "../EquipmentMorphPerKeyCore/include/mhw_morph_host.h"
#include <atomic>
#include <thread>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <stdexcept>

int main(){std::atomic_bool done=false;std::thread tick;
    try {
        unsigned checks=0;auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);checks++;};
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto root=std::filesystem::path(executable).parent_path();
        const auto host=LoadLibraryW((root/L"fixture"/L"MHWEquipmentMorphConfiguredPrototype.dll").c_str());check(host!=nullptr,"load isolated software fixture");
        auto load=[&]<class T>(T& fn,const char* name){const auto address=GetProcAddress(host,name);static_assert(sizeof(fn)==sizeof(address));std::memcpy(&fn,&address,sizeof(fn));check(fn!=nullptr,name);};
        decltype(&MhwMorphGetApi) get=nullptr;decltype(&MhwMorphHostAttach) attach=nullptr;decltype(&MhwMorphHostTick) clock=nullptr;
        load(get,"MhwMorphGetApi");load(attach,"MhwMorphHostAttach");load(clock,"MhwMorphHostTick");
        MhmApi api{};check(get(MHM_API_VERSION,sizeof(api),&api)==MHM_OK,"fixture API");
        unsigned char basis[64]{};MhmDelta delta{0,1,0,0};MhmHostKey key{};strcpy_s(key.info.id,"Example");key.info.maximum=1;key.deltas=&delta;key.count=1;
        MhmHostTarget target{MHM_BODY,sizeof(basis),"synthetic/body",basis,&key,1,0,nullptr};uint64_t handle=0;
        check(attach(&target,&handle)==MHM_OK,"synthetic metadata");clock(GetTickCount64());
        tick=std::thread([&](){while(!done){clock(GetTickCount64());Sleep(10);}});
        check(LoadLibraryW((root/L"MHWEquipmentMorphControlBridge.dll").c_str())!=nullptr,"load production bridge into fixture process");
        const auto name=mhmcontrol::PipeName(GetCurrentProcessId());
        auto connect=[&](){HANDLE pipe=INVALID_HANDLE_VALUE;const auto start=GetTickCount64();
            while(GetTickCount64()-start<3000){pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(pipe!=INVALID_HANDLE_VALUE)break;Sleep(20);}
            check(pipe!=INVALID_HANDLE_VALUE,"connect actual pipe");DWORD mode=PIPE_READMODE_MESSAGE;check(SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr)!=FALSE,"message mode");return pipe;};
        uint64_t sequence=0;auto exchange=[&](HANDLE pipe,mhmcontrol::Action action,float weight){mhmcontrol::Request request;request.sequence=++sequence;request.action=action;request.target=handle;request.weight=weight;
            mhmcontrol::Response response;const auto start=GetTickCount64();auto alive=[&](){return GetTickCount64()-start<2500;};
            check(mhmcontrol::Transfer(pipe,&request,sizeof(request),true,nullptr,alive)&&mhmcontrol::Transfer(pipe,&response,sizeof(response),false,nullptr,alive)&&mhmcontrol::Valid(response,sequence,GetCurrentProcessId()),"production roundtrip");return response;};
        auto weight=[&](){MhmWeightState state{};uint32_t count=0;uint64_t requested=0,applied=0;check(api.read_weights(handle,&state,1,&count,&requested,&applied)==MHM_OK,"read fixture weights");return state.requested;};
        HANDLE pipe=connect();check(exchange(pipe,mhmcontrol::Action::Set,.432198f).result==MHM_OK&&weight()==.432198f,"arbitrary continuous wire weight");
        Sleep(650);check(exchange(pipe,mhmcontrol::Action::Poll,0).source!=0&&weight()==.432198f,"poll keeps own source alive");
        CloseHandle(pipe);Sleep(200);check(weight()==0,"disconnect immediately releases own source");
        pipe=connect();exchange(pipe,mhmcontrol::Action::Set,.8f);Sleep(1800);check(weight()==0,"unresponsive client releases source without polling");CloseHandle(pipe);
        pipe=connect();check(exchange(pipe,mhmcontrol::Action::Poll,0).source==0,"reconnect never replays weights");exchange(pipe,mhmcontrol::Action::Set,.7f);
        mhmcontrol::Request replay;replay.sequence=sequence;const auto start=GetTickCount64();auto alive=[&](){return GetTickCount64()-start<1500;};
        mhmcontrol::Transfer(pipe,&replay,sizeof(replay),true,nullptr,alive);Sleep(200);check(weight()==0,"replayed sequence disconnects and releases");CloseHandle(pipe);
        done=true;tick.join();std::printf("PASS: %u real-pipe checks; production bridge, isolated software core, no game\n",checks);return 0;
    }catch(const std::exception& e){done=true;if(tick.joinable())tick.join();std::printf("FAIL: %s\n",e.what());return 1;}
}
