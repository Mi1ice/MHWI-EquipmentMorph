#pragma once
namespace retirementtest {
// Production retirement uses these owned COM objects and byte arrays. No game
// process, native constructor, command submission or graphics driver is used.
struct Fence final : ID3D12Fence {
    ULONG references=1;UINT64 completed=1000;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(iid!=__uuidof(ID3D12Fence))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {return --references;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**) override {return E_NOTIMPL;}
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override {return completed;}
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64,HANDLE) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Signal(UINT64) override {return E_NOTIMPL;}
};
template<class Bytes> void Put(Bytes& bytes,size_t at,std::uint64_t value,size_t count){
    for(size_t i=0;i<count;i++)bytes[at+i]=static_cast<unsigned char>(value>>(i*8));
}
struct Arena {
    const std::uintptr_t oldBase=observer::imageBase,oldRenderer=facecontinuous::resourceRenderer,
        oldUpload=facecontinuous::resourceUploadFence,oldDrawing=facecontinuous::resourceDrawingFence;
    const bool oldArmed=facecontinuous::armed;
    Fence uploadObject,drawObject;
    std::array<unsigned char,0x1200> memory{};
    std::uintptr_t rendererGlobal=reinterpret_cast<std::uintptr_t>(memory.data());
    privateprobe::UploadFence upload;facecontinuous::DrawingFence drawing;
    Arena(){
        observer::imageBase=reinterpret_cast<std::uintptr_t>(&rendererGlobal)-0x51C7050;
        upload.renderer=drawing.renderer=rendererGlobal;
        upload.pointer=reinterpret_cast<std::uintptr_t>(&uploadObject);drawing.pointer=reinterpret_cast<std::uintptr_t>(&drawObject);
        Put(memory,0x1158,upload.pointer,8);Put(memory,0x168,drawing.pointer,8);Put(memory,0x1E0,101,8);
        drawObject.completed=101;
        if(!upload.reader.Attach(&uploadObject)||!drawing.reader.Attach(&drawObject))throw std::runtime_error("Retirement fake fences");
        upload.Poll();drawing.Poll();
        facecontinuous::resourceRenderer=rendererGlobal;facecontinuous::resourceUploadFence=upload.pointer;
        facecontinuous::resourceDrawingFence=drawing.pointer;facecontinuous::armed=true;
    }
    ~Arena(){
        upload.reader.Retained()->Release();drawing.reader.Retained()->Release();
        observer::imageBase=oldBase;facecontinuous::resourceRenderer=oldRenderer;
        facecontinuous::resourceUploadFence=oldUpload;facecontinuous::resourceDrawingFence=oldDrawing;facecontinuous::armed=oldArmed;
    }
    facecontinuous::Frame Frame(std::uint64_t counter)const{return {0x50000,rendererGlobal,drawing.pointer,counter,counter,1,0,GetCurrentThreadId()};}
    void Counter(std::uint64_t value){Put(memory,0x1E0,value,8);}
};
struct Fixture {
    facecontinuous::Part part;
    std::array<std::array<unsigned char,128>,3> wrappers{};
    std::array<std::array<unsigned char,144>,3> backings{};
    std::array<std::uintptr_t,3> handles{};
    Fixture(const mhm::Asset& asset,const std::array<std::uint64_t,3>& repeats){
        part.asset=std::make_shared<const mhm::Asset>(asset);part.index=asset.part;part.allocated=true;
        for(unsigned i=0;i<3;i++){
            auto& b=part.buffers[i];b.bytes=64;b.wrapper=reinterpret_cast<std::uintptr_t>(wrappers[i].data());
            b.backing=reinterpret_cast<std::uintptr_t>(backings[i].data());b.resource=b.backing+0x1000;b.view=b.wrapper+0x1000;b.initialization=10+i;
            part.stamps[i]=repeats[i]==UINT64_MAX?b.initialization:repeats[i];handles[i]=b.wrapper;
            Put(wrappers[i],0,observer::imageBase+observed::vertexVtableRva,8);Put(wrappers[i],0x48,1,4);Put(wrappers[i],0x5C,64,4);
            Put(wrappers[i],0x60,b.backing,8);Put(wrappers[i],0x78,b.view,8);
            Put(backings[i],0,observer::imageBase+0x3539948,8);Put(backings[i],0x10,64,4);Put(backings[i],0x20,b.resource,8);
            Put(backings[i],0x78,b.initialization,8);Put(backings[i],0x80,repeats[i],8);
        }
    }
    void Bind(Arena& arena,std::uint64_t counter){
        if(!part.adapter->Live()&&!part.adapter->Arm())throw std::runtime_error("Retirement fixture arm");
        const facecontinuous::Identity id{0x10000+part.index*0x10000,0x80000+counter*0x100,0x70000,0x90000};
        if(!part.adapter->Capture(id,id.source))throw std::runtime_error("Retirement fixture capture");
        part.binding=std::make_unique<equipmentmorph::PartBinding>();part.binding->Attach(*part.asset);part.target=part.binding->target;
        if(!part.adapter->PublishResources(handles)||!part.adapter->Launch(arena.Frame(counter),0,0x60000))throw std::runtime_error("Retirement fixture launch");
        part.reusable=false;
    }
    void CpuDrain(Arena& arena,std::uint64_t counter){
        auto& drain=part.drain;const auto first=arena.Frame(counter+1),next=arena.Frame(counter+2);
        if(!drain.Launch(first,0,0x60000)||!drain.Wait(first,0,0x61000,0)||!drain.Join(first)
            ||!drain.Signal(arena.rendererGlobal,arena.drawing.pointer,counter+2,counter+2,counter+2,true)
            ||!drain.Launch(next,0,0x60000)||!drain.Wait(next,0,0x61000,0)||!drain.Join(next))throw std::runtime_error("Retirement fixture CPU drain");
    }
};
inline unsigned Run(const std::filesystem::path& path){
    unsigned count=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);count++;};
    Arena arena;privateprobe::Log log;
    log.file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(log.file!=INVALID_HANDLE_VALUE,"retirement regression log opened");
    mhm::Asset asset;asset.part=MHM_WAIST;asset.model="pl/synthetic/waist";asset.basis.resize(64);
    mhm::Key key;strcpy_s(key.info.id,"controlled");key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);
    {
        Fixture initial(asset,{UINT64_MAX,UINT64_MAX,UINT64_MAX});auto& b=initial.part.buffers[0];
        check(privateprobe::Contract(b)&&facecontinuous::BufferUploadComplete(b,10,10),"native constructor UINT64_MAX repeat sentinel permits completed initialization");
        check(!facecontinuous::BufferUploadComplete(b,10,9)&&!facecontinuous::BufferUploadComplete(b,10,UINT64_MAX),"constructor-only completion still requires finite completed fence");
        check(!facecontinuous::BufferUploadComplete(b,20,1000),"constructor sentinel cannot hide a previously recorded update");
        check(!facecontinuous::BufferUploadComplete(b,UINT64_MAX,1000),"constructor sentinel cannot stand in for a finite expected upload");
        Put(initial.backings[0],0x80,0,8);
        check(!facecontinuous::BufferUploadComplete(b,10,10),"zero is not the verified native constructor sentinel");
        Put(initial.backings[0],0x80,10,8);
        check(!facecontinuous::BufferUploadComplete(b,10,10),"unexpected repeat write equal to initialization is rejected");
    }
    {
        Fixture updated(asset,{20,21,22});auto& b=updated.part.buffers[0];
        check(facecontinuous::BufferUploadComplete(b,20,20),"recorded repeated upload still completes at its exact stamp");
        check(!facecontinuous::BufferUploadComplete(b,20,19)&&!facecontinuous::BufferUploadComplete(b,20,UINT64_MAX),"recorded upload requires finite completed fence");
        Put(updated.backings[0],0x80,UINT64_MAX,8);
        check(!facecontinuous::BufferUploadComplete(b,20,1000),"recorded upload reverting to constructor sentinel remains rejected");
    }
    const std::array<std::array<std::uint64_t,3>,3> patterns={{{UINT64_MAX,UINT64_MAX,UINT64_MAX},{20,UINT64_MAX,UINT64_MAX},{20,21,22}}};
    for(unsigned i=0;i<patterns.size();i++){
        const std::uint64_t counter=100+i*10;arena.Counter(counter+1);arena.drawObject.completed=counter+1;
        Fixture fixture(asset,patterns[i]);fixture.Bind(arena,counter);
        fixture.part.adapter->Invalidate(0,"equipment_part_model_replaced");
        facecontinuous::EndPart(log,fixture.part,arena.upload,arena.drawing);
        check(fixture.part.retiring&&!fixture.part.reusable&&!fixture.part.binding,"zero or partially updated buffers enter retirement");
        check(fixture.part.drain.UploadFloor()==*std::max_element(fixture.part.stamps.begin(),fixture.part.stamps.end()),"retirement floor includes constructor-only slots");
        facecontinuous::DrainStep(log,fixture.part,arena.upload,arena.drawing);
        check(fixture.part.retiring,"GPU completion cannot replace CPU retirement proof");
        fixture.CpuDrain(arena,counter);
        facecontinuous::DrainStep(log,fixture.part,arena.upload,arena.drawing);
        check(fixture.part.retiring,"CPU successor join still waits for draw fence");
        arena.Counter(counter+2);arena.drawObject.completed=counter+2;arena.drawing.Poll();
        facecontinuous::DrainStep(log,fixture.part,arena.upload,arena.drawing);
        check(fixture.part.reusable&&!fixture.part.retiring&&fixture.part.epoch==2&&fixture.part.adapter->Live(),"completed retirement rearms constructor-only and updated buffers");
        for(unsigned slot=0;slot<3;slot++)check(facecontinuous::BufferUploadComplete(fixture.part.buffers[slot],fixture.part.stamps[slot],arena.upload.completed),"retired slot retains exact upload proof for reuse");
    }
    for(unsigned kind=0;kind<5;kind++){
        const std::uint64_t counter=150+kind*10;arena.Counter(counter+1);arena.drawObject.completed=counter+1;
        Fixture rejected(asset,{UINT64_MAX,UINT64_MAX,UINT64_MAX});rejected.Bind(arena,counter);
        if(kind==0)rejected.part.stamps[0]=20; // recorded update vanished
        if(kind==1)Put(rejected.backings[0],0x80,5,8);
        if(kind==2)Put(rejected.backings[0],0x80,20,8); // unexpected write
        if(kind==3)Put(rejected.backings[0],0x80,0,8); // not the native constructor sentinel
        if(kind==4)Put(rejected.wrappers[0],0x48,0,4);
        rejected.part.adapter->Invalidate(0,"equipment_part_model_replaced");
        bool refused=false;try{facecontinuous::EndPart(log,rejected.part,arena.upload,arena.drawing);}catch(const std::runtime_error& error){refused=std::strcmp(error.what(),"Part retirement stamp")==0;}
        check(refused&&!rejected.part.retiring&&!rejected.part.reusable,"invalid, unexpected or missing update evidence remains rejected");
    }
    for(unsigned kind=0;kind<3;kind++){
        arena.Counter(201);arena.drawObject.completed=201;
        Fixture changed(asset,{UINT64_MAX,UINT64_MAX,UINT64_MAX});changed.Bind(arena,200);changed.part.adapter->Invalidate(0,"equipment_part_model_replaced");
        facecontinuous::EndPart(log,changed.part,arena.upload,arena.drawing);changed.CpuDrain(arena,200);
        arena.Counter(202);arena.drawObject.completed=202;arena.drawing.Poll();
        Put(changed.backings[0],0x80,kind==0?30:kind==1?0:changed.part.buffers[0].initialization,8);
        bool refused=false;try{facecontinuous::DrainStep(log,changed.part,arena.upload,arena.drawing);}catch(const std::runtime_error& error){refused=std::strcmp(error.what(),"Retired part stamp changed")==0;}
        check(refused&&changed.part.retiring&&!changed.part.reusable,"upload change during retirement cannot permit reuse");
    }
    for(unsigned kind=0;kind<2;kind++){
        Arena removedArena;removedArena.Counter(211);removedArena.drawObject.completed=211;
        Fixture removed(asset,{UINT64_MAX,UINT64_MAX,UINT64_MAX});removed.Bind(removedArena,210);
        removed.part.adapter->Invalidate(0,"equipment_part_model_replaced");
        auto& fence=kind?removedArena.drawObject:removedArena.uploadObject;fence.completed=UINT64_MAX;
        bool refused=false;try{facecontinuous::EndPart(log,removed.part,removedArena.upload,removedArena.drawing);}catch(const std::runtime_error&){refused=true;}
        const auto& reader=kind?removedArena.drawing.reader:removedArena.upload.reader;
        check(refused&&reader.State()==frameobserver::FenceState::deviceRemoved&&!removed.part.retiring&&!removed.part.reusable,"constructor sentinel never permits UINT64_MAX upload or draw Fence completion");
    }
    // Real public bindings and a source kept open across repeated body+waist
    // retirement. No release is sent before changing equipment.
    {
        auto bodyAsset=asset;bodyAsset.part=MHM_BODY;bodyAsset.model="pl/synthetic/body001";
        Fixture body(bodyAsset,{20,21,22}),waist(asset,{UINT64_MAX,UINT64_MAX,UINT64_MAX});
        MhmApi api{};std::uint64_t source=0,revision=0;
        check(MhwMorphGetApi(MHM_API_VERSION,sizeof(api),&api)==MHM_OK&&api.open_source("held-switch",220,2000,&source)==MHM_OK,"control source opened for no-release switches");
        for(unsigned pass=0;pass<3;pass++){
            const std::uint64_t counter=300+pass*10;arena.Counter(counter+1);arena.drawObject.completed=counter+1;
            body.Bind(arena,counter);waist.Bind(arena,counter);
            MhmWeightState state{};std::uint32_t needed=0;std::uint64_t requested=0,applied=0;
            check(api.read_weights(body.part.target,&state,1,&needed,&requested,&applied)==MHM_OK&&state.requested==0,"return to 001 creates a zero-weight target without replay");
            const MhmUpdate held{body.part.target,0,.6f};
            check(api.submit(source,&held,1,&revision)==MHM_OK,"body weight set while source stays open");
            const auto oldBody=body.part.target,oldWaist=waist.part.target;
            body.part.adapter->Invalidate(0,"equipment_part_model_replaced");waist.part.adapter->Invalidate(0,"equipment_part_model_replaced");
            facecontinuous::EndPart(log,body.part,arena.upload,arena.drawing);facecontinuous::EndPart(log,waist.part,arena.upload,arena.drawing);
            check(body.part.retiring&&waist.part.retiring&&facecontinuous::armed&&api.heartbeat(source)==MHM_OK,"body and zero-weight waist retire without stopping runtime or closing source");
            check(api.read_weights(oldBody,&state,1,&needed,&requested,&applied)==MHM_STALE&&api.read_weights(oldWaist,&state,1,&needed,&requested,&applied)==MHM_STALE,"both previous target handles become stale");
            check(api.submit(source,&held,1,&revision)==MHM_STALE,"held old contribution cannot control a replacement target");
            body.CpuDrain(arena,counter);waist.CpuDrain(arena,counter);
            arena.Counter(counter+2);arena.drawObject.completed=counter+2;arena.drawing.Poll();
            facecontinuous::DrainStep(log,body.part,arena.upload,arena.drawing);facecontinuous::DrainStep(log,waist.part,arena.upload,arena.drawing);
            check(body.part.reusable&&waist.part.reusable&&facecontinuous::armed,"both pools drain and rearm across repeated no-release switches");
        }
        check(api.close_source(source)==MHM_OK,"held switch source released after regression");
    }
    LARGE_INTEGER zero{},size{};DWORD read=0;GetFileSizeEx(log.file,&size);SetFilePointerEx(log.file,zero,nullptr,FILE_BEGIN);
    std::string records(static_cast<size_t>(size.QuadPart),'\0');
    check(ReadFile(log.file,records.data(),static_cast<DWORD>(records.size()),&read,nullptr)&&read==records.size(),"retirement diagnostic records readable");
    check(records.find("\"event\":\"part_retirement_buffer_rejected\"")!=records.npos
        &&records.find("\"expected_stamp\":20")!=records.npos&&records.find("\"repeat_stamp\":18446744073709551615")!=records.npos,
        "retirement rejection logs observed and expected per-slot evidence");
    return count;
}
}
