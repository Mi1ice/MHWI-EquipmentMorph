// Included inside facecontinuous. Worker owns uploads and public bindings;
// gate only serializes adapter/native callback state, not native GPU calls.
bool BufferUploadComplete(privateprobe::Buffer& buffer,std::uint64_t expected,std::uint64_t completed){
    std::uint64_t stamp=0;
    return expected!=UINT64_MAX&&expected>=buffer.initialization&&completed!=UINT64_MAX&&completed>=expected
        &&privateprobe::Contract(buffer)&&observer::Read(buffer.backing+0x80,stamp)&&stamp==expected;
}
void ResetPartLocked(Part& part){
    if(part.epoch==UINT64_MAX)throw std::runtime_error("Part epoch exhausted");
    part.epoch++;part.adapter=std::make_unique<LaunchAdapter>(part.epoch);part.drain=DrainBoundary{};
    part.retiring=false;part.target=0;part.updates=part.replacements=part.lastRevision=0;part.lastSample.clear();
    part.cadence=facesmooth::SampleGate{};
    if(armed&&part.asset&&!part.blocked&&!part.adapter->Arm())throw std::runtime_error("Fresh part arm");
}
void ResetPart(Part& part){std::lock_guard lock(gate);ResetPartLocked(part);}
void ActivateWanted(Part& part){
    std::lock_guard lock(gate);
    if(part.active==part.wanted)return;
    if(part.binding||part.retiring||part.adapter->Target().Valid()||(part.allocated&&!part.reusable))throw std::runtime_error("Model switch before retirement completed");
    if(part.active)part.cached[*part.active]=static_cast<BufferPool&>(part);
    static_cast<BufferPool&>(part)=BufferPool{};part.active=part.wanted;part.asset.reset();
    const auto& entries=equipmentnative::catalogue.parts[part.index];
    if(part.active){
        const auto& entry=entries.at(*part.active);
        static_cast<BufferPool&>(part)=part.cached[*part.active];part.cached[*part.active]=BufferPool{};
        part.asset=std::shared_ptr<const mhm::Asset>(entry,&entry->asset);
        equipmentnative::profiles[part.index]=equipmentnative::Active(part.index,entry.get());
    }else equipmentnative::profiles[part.index]=equipmentnative::Active(part.index,nullptr);
    part.payload.clear();ResetPartLocked(part);
}
void EndPart(privateprobe::Log& log,Part& part,privateprobe::UploadFence& upload,DrawingFence& drawing){
    const auto oldTarget=part.target;part.binding.reset();part.target=0;
    if(oldTarget){MhmApi api{};MhmWeightState values[64]{};std::uint32_t count=0;std::uint64_t req=0,app=0;
        if(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)!=MHM_OK||api.read_weights(oldTarget,values,64,&count,&req,&app)!=MHM_STALE)throw std::runtime_error("Part handle not retired");}
    bool rebind=false,launched=false;Frame fixed{};
    {std::lock_guard lock(gate);rebind=armed&&RebindReason(part.adapter->Reason());fixed=part.adapter->Fixed();launched=part.adapter->HasLaunched();}
    char line[700]{};std::snprintf(line,sizeof(line),"{\"event\":\"public_part_retired\",\"part\":%u,\"epoch\":%llu,\"target\":%llu,\"old_handle_stale\":true,\"reason\":\"%s\",\"rebind\":%s}\n",part.asset->part,part.epoch,oldTarget,part.adapter->Reason(),rebind?"true":"false");
    if(!log.Write(line))throw std::runtime_error("Part retire log");
    if(!rebind){part.blocked=true;return;}
    if(!part.allocated){ResetPart(part);return;}
    // The worker has already polled the shared fences. Before the first launch
    // these buffers have only constructor uploads, and no native draw references.
    // A reused set still retains the completion proof from its previous drain.
    if(!launched){
        if(drawing.completed==UINT64_MAX||drawing.completed<part.reuseFence)throw std::runtime_error("Unlaunched part draw completion");
        for(unsigned i=0;i<3;i++)if(!BufferUploadComplete(part.buffers[i],part.stamps[i],upload.completed))throw std::runtime_error("Unlaunched part upload completion");
        part.reusable=true;
        std::snprintf(line,sizeof(line),"{\"event\":\"part_unlaunched_reusable\",\"part\":%u,\"epoch\":%llu,\"upload_completed\":%llu,\"previous_retirement_fence\":%llu}\n",part.asset->part,part.epoch,upload.completed,part.reuseFence);
        if(!log.Write(line))throw std::runtime_error("Unlaunched part retire log");ResetPart(part);return;
    }
    upload.Poll();drawing.Poll();std::uintptr_t renderer=0,fence=0;std::uint64_t floor=0,maximum=0;
    {std::lock_guard lock(gate);
        if(!fixed.Stable()||!RendererFacts(renderer,fence,floor)||renderer!=resourceRenderer||fence!=resourceDrawingFence
                ||upload.pointer!=resourceUploadFence)throw std::runtime_error("Part retirement identity");
        for(unsigned i=0;i<3;i++){auto& b=part.buffers[i];auto& stamp=part.drainStamps[i];
            if(!privateprobe::Contract(b)||!observer::Read(b.backing+0x80,stamp)||stamp==UINT64_MAX||stamp<b.initialization)throw std::runtime_error("Part retirement stamp");maximum=std::max(maximum,stamp);}
        part.drain=DrainBoundary{};if(!part.drain.Start(fixed,floor,maximum))throw std::runtime_error("Part drain start");
        part.retiring=true;part.drainDeadline=::GetTickCount64()+30000;
    }
    std::snprintf(line,sizeof(line),"{\"event\":\"part_retirement_begin\",\"part\":%u,\"epoch\":%llu,\"upload_floor\":%llu,\"counter_floor\":%llu}\n",part.asset->part,part.epoch,maximum,floor);
    if(!log.Write(line))throw std::runtime_error("Part drain log");
}
void DrainStep(privateprobe::Log& log,Part& part,privateprobe::UploadFence& upload,DrawingFence& drawing){
    bool ready=false;std::uint64_t covered=0,maximum=0;
    {std::lock_guard lock(gate);if(part.drain.Failed()||!part.drain.Completed(upload.completed,drawing.completed))throw std::runtime_error(part.drain.Reason());
        ready=part.drain.Ready();covered=part.drain.CoveredSignal();maximum=part.drain.UploadFloor();}
    if(!ready){if(::GetTickCount64()>=part.drainDeadline)throw std::runtime_error("Part drain timeout");return;}
    for(unsigned i=0;i<3;i++){std::uint64_t stamp=0;auto& b=part.buffers[i];
        if(!privateprobe::Contract(b)||!observer::Read(b.backing+0x80,stamp)||stamp!=part.drainStamps[i])throw std::runtime_error("Retired part stamp changed");}
    part.stamps=part.drainStamps;part.reuseFence=covered;part.reusable=true;
    char line[800]{};std::snprintf(line,sizeof(line),"{\"event\":\"part_retirement_complete\",\"part\":%u,\"epoch\":%llu,\"covered_signal\":%llu,\"successor_joined\":true,\"upload_floor\":%llu,\"upload_completed\":%llu,\"draw_completed\":%llu,\"part_buffers\":3}\n",part.asset->part,part.epoch,covered,maximum,upload.completed,drawing.completed);
    if(!log.Write(line))throw std::runtime_error("Part drain complete log");ResetPart(part);
}
void InitializePart(privateprobe::Log& log,Part& part,privateprobe::UploadFence& upload,DrawingFence& drawing,std::uint64_t now){
    Identity identity{};{std::lock_guard lock(gate);identity=part.adapter->Target();if(!part.adapter->Live()||!Matches(part.index,identity,true)){part.adapter->Invalidate(0,"equipment_part_identity_changed");return;}}
    using Ctor=void*(__fastcall*)(void*,unsigned,unsigned,unsigned,const void*);std::array<std::uintptr_t,3> handles{};char line[800]{};
    if(!part.allocated) {
        for(unsigned slot=0;slot<3;slot++){
            {std::lock_guard lock(gate);if(!part.adapter->Live()||!Matches(part.index,identity,false)){part.adapter->Invalidate(0,"equipment_part_identity_changed");return;}}
            if(!CodeMatches())throw std::runtime_error("Constructor code changed");upload.Poll();drawing.Poll();auto& buffer=part.buffers[slot];
            // A cancelled first allocation may already own some completed slots.
            // They were never published; reuse them instead of allocating again.
            if(buffer.wrapper){
                if(!BufferUploadComplete(buffer,part.stamps[slot],upload.completed))throw std::runtime_error("Partial part upload completion");
                handles[slot]=buffer.wrapper;continue;
            }
            void* storage=::VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!storage)throw std::runtime_error("Private storage");
            buffer.bytes=static_cast<unsigned>(part.asset->basis.size());buffer.wrapper=reinterpret_cast<std::uintptr_t>(reinterpret_cast<Ctor>(observer::imageBase+0x25855C0)(storage,buffer.bytes,0,0x01000000,part.asset->basis.data()));
            if(buffer.wrapper!=reinterpret_cast<std::uintptr_t>(storage)||!privateprobe::Contract(buffer,true))throw std::runtime_error("Part private contract");
            upload.Wait(buffer.initialization);handles[slot]=buffer.wrapper;part.stamps[slot]=buffer.initialization;
            for(const auto& other:parts)for(const auto& prior:other.buffers)if(prior.wrapper && &prior!=&buffer)
                if(prior.wrapper==buffer.wrapper||prior.backing==buffer.backing||prior.resource==buffer.resource||prior.view==buffer.view)throw std::runtime_error("Private alias across parts");
            for(const auto& other:parts)for(const auto& cached:other.cached)for(const auto& prior:cached.buffers)if(prior.wrapper)
                if(prior.wrapper==buffer.wrapper||prior.backing==buffer.backing||prior.resource==buffer.resource||prior.view==buffer.view)throw std::runtime_error("Private alias across cached models");
            std::snprintf(line,sizeof(line),"{\"event\":\"private_part_buffer_initialized\",\"part\":%u,\"slot\":%u,\"bytes\":%u,\"initial_stamp\":%llu}\n",part.asset->part,slot,buffer.bytes,buffer.initialization);
            if(!log.Write(line))throw std::runtime_error("Part initialization log");
        }
        part.allocated=true;
    }else{
        upload.Poll();drawing.Poll();
        for(unsigned slot=0;slot<3;slot++){auto& b=part.buffers[slot];
            if(!part.reusable||!BufferUploadComplete(b,part.stamps[slot],upload.completed)
                    ||drawing.completed<part.reuseFence)throw std::runtime_error("Part reuse completion");handles[slot]=b.wrapper;}
        std::snprintf(line,sizeof(line),"{\"event\":\"private_part_reused\",\"part\":%u,\"epoch\":%llu,\"retirement_fence\":%llu,\"upload_completed\":%llu,\"draw_completed\":%llu,\"part_buffers\":3}\n",part.asset->part,part.epoch,part.reuseFence,upload.completed,drawing.completed);
        if(!log.Write(line))throw std::runtime_error("Part reuse log");
    }
    part.reusable=false;
    part.binding=std::make_unique<equipmentmorph::PartBinding>();part.binding->Attach(*part.asset);part.target=part.binding->target;
    part.payload.resize(part.asset->basis.size());
    MhmTargetInfo descriptor{};descriptor.handle=part.target;descriptor.part=part.asset->part;descriptor.key_count=static_cast<unsigned>(part.asset->keys.size());std::memcpy(descriptor.model_id,part.asset->model.c_str(),part.asset->model.size()+1);
    if(part.smoother.Reset({&descriptor,1},now)!=MHM_OK||!part.cadence.Reset(now))throw std::runtime_error("Part smoother reset");
    {std::lock_guard lock(gate);if(!part.adapter->Live()||!Matches(part.index,identity,true)){part.adapter->Invalidate(0,"equipment_part_identity_changed");return;}
        if(!part.adapter->PublishResources(handles))throw std::runtime_error("Part publication");}
    WriteState(log,part,"part_ready_original_inputs");
}
void WriteUpload(privateprobe::Log& log,const Part& part,unsigned slot,const launchpool::MorphVersion& version,std::uint64_t stamp,
                 const std::string& sha,const launchpool::Slot& previous){
    char line[4096]{};const auto weights=Weights(version.weights,static_cast<unsigned>(part.asset->keys.size()));
    const auto count=std::snprintf(line,sizeof(line),"{\"event\":\"part_upload_queued\",\"part\":%u,\"epoch\":%llu,\"slot\":%u,\"core_frame\":%llu,\"revision\":%llu,\"weights\":%s,\"stamp\":%llu,\"payload_sha256\":\"%s\",\"previous_retirement_fence\":%llu,\"previous_coverage_fence\":%llu,\"previous_joined\":%s}\n",part.asset->part,part.epoch,slot,version.frame,version.revision,weights.c_str(),stamp,sha.c_str(),previous.retirementFence,previous.coverageFence,previous.joinedAfterUse?"true":"false");
    if(count<0||static_cast<size_t>(count)>=sizeof(line)||!log.Write(line))throw std::runtime_error("Part upload log");
}
bool PartStep(privateprobe::Log& log,Part& part,privateprobe::UploadFence& upload,DrawingFence& drawing,std::uint64_t now){
    launchpool::View view{};bool live=false,started=false;
    {std::lock_guard lock(gate);if(part.adapter->Live()&&!Matches(part.index,part.adapter->Target(),false))part.adapter->Invalidate(0,"equipment_part_identity_changed");
        live=part.adapter->Live();started=part.adapter->Started();
        if(live&&started){if(part.adapter->Fixed().renderer!=upload.renderer||part.adapter->Fixed().fence!=drawing.pointer)throw std::runtime_error("Part fence identity");part.adapter->Completed(upload.completed,drawing.completed);}
        view=part.adapter->View();live=part.adapter->Live();}
    if(!live)return false;
    if(started)part.binding->Acknowledge(view);part.binding->Collect(view);
    std::vector<float> requested;std::uint64_t revision=0;if(!part.binding->Requested(static_cast<unsigned>(part.asset->keys.size()),requested,revision))return false;
    std::vector<MhmUpdate> desired;for(unsigned k=0;k<requested.size();k++)desired.push_back({part.target,k,requested[k]});
    if(part.smoother.SetTargets(desired,revision,now)!=MHM_OK)throw std::runtime_error("Part smooth target");
    if(part.cadence.Due(now)&&CanSubmit(view)){
        std::vector<MhmUpdate> sampled;std::uint64_t sampledRevision=0;
        if(part.smoother.Sample(now,sampled,sampledRevision)!=MHM_OK)throw std::runtime_error("Part smooth sample");std::vector<float> values;for(const auto& value:sampled)values.push_back(value.weight);
        if(values!=part.lastSample||sampledRevision!=part.lastRevision){launchpool::MorphVersion version{};
            if(part.binding->Capture(values,sampledRevision,version)){
                std::lock_guard lock(gate);if(part.adapter->Live()){
                    if(!part.adapter->Request(version)||!part.cadence.Submitted(now))throw std::runtime_error("Part request/cadence");part.lastSample=values;part.lastRevision=sampledRevision;}
            }}
    }
    std::optional<launchpool::Token> token;launchpool::Slot previous{};launchpool::MorphVersion version{};std::uint64_t previousUse=0;
    {std::lock_guard lock(gate);if(part.adapter->Started()&&part.adapter->Live()){
        const auto prior=part.adapter->View();token=part.adapter->Prepare();
        if(token){previous=prior.slots[token->slot];previousUse=std::max(previous.lastUseSignal,previous.retirementFence);const auto state=part.adapter->View();version=state.slots[token->slot].weight;
            if(!part.adapter->BeginUpload(*token))token.reset();}}}
    if(token){
        auto& buffer=part.buffers[token->slot];auto& payload=part.payload;
        if(MhwMorphHostCompose(version.frame,part.target,payload.data(),static_cast<unsigned>(payload.size()))!=MHM_OK)throw std::runtime_error("Part composition");
        if(!CodeMatches()||!privateprobe::Contract(buffer))throw std::runtime_error("Part map code/contract");upload.Poll();drawing.Poll();
        if(drawing.completed<part.reuseFence||drawing.completed<previousUse||upload.completed<part.stamps[token->slot]||previous.phase!=launchpool::Phase::Free
                ||(previousUse&&(!previous.joinedAfterUse||!previous.retirementFence||previous.coverageFence<previous.retirementFence)))throw std::runtime_error("Part slot retirement");
        using Map=void*(__fastcall*)(void*,unsigned,unsigned,unsigned);using Unmap=void(__fastcall*)(void*,unsigned);const auto before=upload.completed;
        const auto sha=privateprobe::Digest(payload.data(),payload.size());
        void* mapped=reinterpret_cast<Map>(observer::imageBase+0x255B810)(reinterpret_cast<void*>(buffer.wrapper),1,0,buffer.bytes);std::uintptr_t staging=0;
        if(!privateprobe::Contract(buffer)||!observer::Read(buffer.backing+0x58,staging)||staging!=reinterpret_cast<std::uintptr_t>(mapped)||!privateprobe::CopyMapped(mapped,payload.data(),payload.size()))throw std::runtime_error("Part map/copy");
        reinterpret_cast<Unmap>(observer::imageBase+0x255B890)(reinterpret_cast<void*>(buffer.wrapper),buffer.bytes);staging=1;std::uint64_t stamp=0;
        if(!privateprobe::Contract(buffer)||!observer::Read(buffer.backing+0x80,stamp)||!privateprobe::FreshUpload(stamp,before,part.stamps[token->slot])||!observer::Read(buffer.backing+0x58,staging)||staging)throw std::runtime_error("Part fresh upload");
        part.stamps[token->slot]=stamp;part.updates++;
        {std::lock_guard lock(gate);if(part.adapter->Live()&&!part.adapter->QueueUpload(*token,stamp))throw std::runtime_error("Part queue");}
        WriteUpload(log,part,token->slot,version,stamp,sha,previous);
    }
    return part.lastSample!=requested||view.requested!=view.applied;
}
