DWORD WINAPI Worker(void*) noexcept {
    privateprobe::Log log;
    try {
        HMODULE pinned=nullptr;
        if(!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Worker),&pinned))return 1;
        for(const auto name:{L"MHWEquipmentMorphSlotsPrototype.dll",L"MHWEquipmentMorphSlotsPublicClient.dll",L"MHWEquipmentMorphIndependentPartsPrototype.dll",L"MHWEquipmentMorphPartsPublicClient.dll",L"MHWEquipmentMorphMultipartRebindPrototype.dll",L"MHWEquipmentMorphGroupPublicClient.dll",L"MHWEquipmentMorphPartCore.dll",L"MHWEquipmentMorphMultipartPrototype.dll",L"MHWEquipmentMorphContinuityAuditPrototype.dll",L"MHWEquipmentMorphPublicClient.dll",L"MHWEquipmentArmsInputObserver.dll",L"MHWEquipmentMorphGroupCore.dll",L"MHWEquipmentMorphRebindPrototype.dll",L"MHWEquipmentMorphPrototype.dll",L"MHWEquipmentBlinkIntentPrototype.dll",L"MHWEquipmentBlinkSourceShadingPrototype.dll",L"MHWEquipmentBlinkCorneaPrototype.dll",L"MHWEquipmentMorphCore.dll",L"MHWEquipmentBlinkShadingPrototype.dll",L"MHWEquipmentBlinkVisibilityPrototype.dll",L"MHWEquipmentBlinkPrototype.dll",L"MHWEquipmentInputObserver.dll",L"MHWContinuousFaceSmoothRebindPrototype.dll",L"MHWContinuousFaceSmoothCadencePrototype.dll",L"MHWContinuousFaceSmoothPrototype.dll",L"MHWContinuousFaceLaunchPrototype.dll",L"MHWZeroLaunchBoundaryAudit.dll",L"MHWContinuousFaceLaunchAudit.dll",L"MHWContinuousFaceMorphAudit.dll",L"MHWContinuousFaceMorphPrototype.dll",L"MHWPrivateDynamicBufferProbe.dll",L"MHWSubmissionRetirementWorkerObserver.dll",L"MHWSubmissionRetirementObserver.dll",L"MHWFaceUpPrototype.dll",L"MHWFaceInputObserver.dll",L"MHWFaceLifecycleObserver.dll",L"MHWFaceUpLifecyclePrototype.dll",L"MHWFrameBoundaryObserver.dll"})if(::GetModuleHandleW(name))return 2;
        wchar_t exePath[1024]{};if(!::GetModuleFileNameW(nullptr,exePath,1024))return 3;
        const auto name=std::wcsrchr(exePath,L'\\');if(!name||::_wcsicmp(name+1,L"MonsterHunterWorld.exe")!=0)return 4;
        HANDLE exe=::CreateFileW(exePath,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(exe==INVALID_HANDLE_VALUE)return 5;
        LARGE_INTEGER size{};DWORD count=0;bool valid=::GetFileSizeEx(exe,&size)&&size.QuadPart>0&&size.QuadPart<128*1024*1024;
        std::vector<unsigned char> bytes(valid?static_cast<std::size_t>(size.QuadPart):0);
        if(valid)valid=::ReadFile(exe,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();
        ::CloseHandle(exe);if(!valid||privateprobe::Digest(bytes.data(),bytes.size())!=observed::exeHash)return 6;
        observer::imageBase=reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
        bool matched=false;for(unsigned i=0;i<1200;i++){if(CodeMatches()){matched=true;break;}::Sleep(100);}if(!matched)return 7;
        wchar_t selfPath[32768]{};const auto selfCount=::GetModuleFileNameW(pinned,selfPath,32768);
        if(!selfCount||selfCount>=32768)return 8;
        const auto root=std::filesystem::path(selfPath).parent_path()/"MHWEquipmentMorph";
        std::filesystem::create_directories(root/"logs");
        const auto logPath=root/"logs"/("runtime-"+std::to_string(::GetCurrentProcessId())+"-"+std::to_string(::GetTickCount64())+".ndjson");
        log.file=::CreateFileW(logPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(log.file==INVALID_HANDLE_VALUE)return 8;
        FILETIME created{},exited{},kernel{},user{};if(!::GetProcessTimes(::GetCurrentProcess(),&created,&exited,&kernel,&user))return 9;
        const auto creation=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;char line[1800]{};
        LARGE_INTEGER frequency{};if(!::QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return 19;
        auto loaded=mhm::configuration::Load(root,std::filesystem::path(exePath).parent_path()/"nativePC");
        equipmentnative::Initialize(loaded);unsigned configured=0;std::string partIds="[";
        for(const auto& failure:loaded.failures){
            const auto failureLine="{\"event\":\"package_disabled\",\"part\":"+std::to_string(failure.part)+",\"package\":"+mhm::JsonString(failure.package)+",\"reason\":"+mhm::JsonString(failure.reason)+"}\n";
            if(!log.Write(failureLine.c_str()))return 10;
        }
        for(unsigned i=0;i<parts.size();i++)if(!loaded.parts[i].empty()){
            if(configured++)partIds+=',';partIds+=std::to_string(i);
            parts[i].index=i;parts[i].cached.resize(loaded.parts[i].size());
        }partIds+=']';
        std::snprintf(line,sizeof(line),"{\"schema\":\"mhwi-equipmentmorph-auto-binding-runtime-v1\",\"pid\":%lu,\"process_creation_filetime\":%llu,\"exe_sha256\":\"%s\",\"qpc_frequency\":%llu,\"hook_count\":%u,\"new_hook_locations\":0,\"private_buffer_limit\":%u,\"slots_per_part\":3,\"parts\":%s,\"api_version\":65536,\"part_lifecycle_capability\":true,\"asset_formats\":[1,2],\"per_key_normal_capability\":true,\"publication\":\"independent_part_pools\",\"resources_retained_until_exit\":true}\n",::GetCurrentProcessId(),creation,observed::exeHash,static_cast<std::uint64_t>(frequency.QuadPart),configured?10u:0u,configured*3,partIds.c_str());
        if(!log.Write(line))return 10;
        if(!configured){log.Write("{\"event\":\"no_usable_packages_no_hooks_no_gpu\"}\n");log.Flush();return 0;}
        const std::array<safetyhook::MidHookFn,10> callbacks={Source,Stream,Dispatch,SignalReturned,WaitReturned,SyncReturned,Remove,Destructor,LaunchStarting,ModelChanging};
        for(unsigned i=0;i<hooks.size();i++){
            auto hook=observer::CreateDisabled(reinterpret_cast<void*>(observer::imageBase+sites[i]),callbacks[i],windows[i]);
            if(!hook||hook->original_bytes().size()>32)throw std::runtime_error("Hook guard refused");hooks[i]=new safetyhook::MidHook(std::move(*hook));
        }
        if(!CodeMatches())throw std::runtime_error("Intervening code change");
        unsigned installed=0;for(;installed<hooks.size();installed++)if(!hooks[installed]->enable())break;
        if(installed!=hooks.size()){
            bool restored=true;while(installed){installed--;if(!hooks[installed]->disable())restored=false;}
            log.Write(restored?"{\"event\":\"hook_enable_failed_restored\"}\n":"{\"event\":\"hook_enable_failed_restore_failed\"}\n");return 14;
        }
        for(unsigned i=0;i<hooks.size();i++)if(!observer::Read(observer::imageBase+sites[i],patches[i]))throw std::runtime_error("Installed patch snapshot");
        enabled=true;if(!CodeMatches())throw std::runtime_error("Installed hook normalization");
        ArmConfiguredParts();
        if(!log.Write("{\"event\":\"automatic_binding_enabled\",\"requires_F8\":false,\"local_player_source\":\"guarded_sPlayer_master_record\"}\n")||!log.Flush())return 16;
        facesmooth::HighResolutionClock clock;if(!clock.Start(Qpc(),static_cast<std::uint64_t>(frequency.QuadPart)))throw std::runtime_error("QPC setup");
        privateprobe::UploadFence upload;DrawingFence drawing;bool attached=false;
        std::uint64_t stateAt=0,codeAt=0,playerAt=0;bool playerSampled=false;inputtiming::WindowClock timingClock;
        for(;;){
            std::uint64_t now=0;if(!clock.Read(Qpc(),now)||MhwMorphHostTick(::GetTickCount64())!=MHM_OK)throw std::runtime_error("Host clock");
            if(now-codeAt>=500){codeAt=now;if(!CodeMatches())throw std::runtime_error("Runtime code changed");}
            if(!playerSampled||now-playerAt>=50){
                playerAt=now;playerSampled=true;
                if(RefreshMainPlayer()){
                    std::uintptr_t player=0;{std::lock_guard lock(gate);player=anchorPlayer;}
                    char selectedLine[220]{};std::snprintf(selectedLine,sizeof(selectedLine),"{\"event\":\"automatic_main_player_changed\",\"player\":\"0x%llx\",\"waiting\":%s}\n",player,player?"false":"true");
                    if(!log.Write(selectedLine))throw std::runtime_error("Main player selection log");
                }
            }
            DWORD foreground=0;::GetWindowThreadProcessId(::GetForegroundWindow(),&foreground);
            if(foreground==::GetCurrentProcessId()&&privateprobe::StopRequested()){
                std::lock_guard lock(gate);DisableAll("manual_stop");
            }
            bool stopped=false;{std::lock_guard lock(gate);stopped=!armed;}
            if(stopped){for(auto& part:parts){part.binding.reset();part.target=0;}log.Write("{\"event\":\"stopped_original_inputs_resources_retained\"}\n");log.Flush();return 0;}
            if(attached){upload.Poll();drawing.Poll();if(upload.renderer!=resourceRenderer||upload.pointer!=resourceUploadFence||drawing.pointer!=resourceDrawingFence)throw std::runtime_error("Shared fence identity");}
            bool settling=false;
            for(auto& part:parts){
                if(part.blocked)continue;
                if(!part.asset){ActivateWanted(part);continue;}
                bool live=false,captured=false;{std::lock_guard lock(gate);live=part.adapter->Live();captured=part.adapter->Target().Valid();}
                if(!live&&!part.retiring){EndPart(log,part,upload,drawing);if(!part.retiring&&!part.blocked)ActivateWanted(part);continue;}
                if(part.retiring){DrainStep(log,part,upload,drawing);if(!part.retiring)ActivateWanted(part);continue;}
                if(!part.binding&&captured){
                    if(!attached){upload.Attach();drawing.Attach(upload.renderer);attached=true;resourceRenderer=upload.renderer;resourceUploadFence=upload.pointer;resourceDrawingFence=drawing.pointer;}
                    InitializePart(log,part,upload,drawing,now);
                }
                if(part.binding)settling=PartStep(log,part,upload,drawing,now)||settling;
            }
            if(now-stateAt>=500){
                stateAt=now;for(auto& part:parts)if(part.asset)WriteState(log,part,"part_state");
                if(const auto elapsed=timingClock.Due(now))WriteInputTiming(log,*elapsed,static_cast<std::uint64_t>(frequency.QuadPart));
                if(!log.Flush())throw std::runtime_error("State flush");
            }
            ::Sleep(settling?1:5);
        }
    }catch(const std::exception& e){
        {std::lock_guard lock(gate);DisableAll("worker_failure");}
        for(auto& part:parts){part.binding.reset();part.target=0;}
        const auto failure="{\"event\":\"failed_overrides_disabled_resources_retained\",\"reason\":"+mhm::JsonString(e.what())+"}\n";log.Write(failure.c_str());log.Flush();return 20;
    }catch(...){std::lock_guard lock(gate);DisableAll("unknown_failure");return 21;}
}
