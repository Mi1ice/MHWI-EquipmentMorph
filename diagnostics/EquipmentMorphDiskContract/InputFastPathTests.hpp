#pragma once
namespace inputfasttest {
// All addresses belong to this fixture. Production callbacks run directly;
// no hooks, native functions, uploads, Fence queries or game process are used.
template<class Bytes,class Value> void Put(Bytes& bytes,size_t at,Value value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
template<class Bytes> std::uintptr_t Address(Bytes& bytes){return reinterpret_cast<std::uintptr_t>(bytes.data());}
inline void Need(bool ok){if(!ok)throw std::runtime_error("Original-input fixture contract");}
struct Fixture {
    std::array<facecontinuous::Part,5> oldParts=std::move(facecontinuous::parts);
    std::array<equipmentnative::Profile,5> oldProfiles=equipmentnative::profiles;
    const std::uintptr_t oldBase=observer::imageBase,oldPlayer=facecontinuous::anchorPlayer;
    const bool oldArmed=facecontinuous::armed;
    const std::uint64_t oldDrawOwners=drawfilter::owners.bits.load();
    static constexpr auto PartIndex=MHM_BODY;
    const mhm::localplayer::Selection oldSelection=facecontinuous::mainPlayerSelection;
    const std::array<std::uintptr_t,5> oldOwners=facecontinuous::mainPlayerOwners;
    std::vector<unsigned char> image=std::vector<unsigned char>(0x55D3000),player=std::vector<unsigned char>(0x12700),
        singleton=std::vector<unsigned char>(0xC280),bridge=std::vector<unsigned char>(0x500),model=std::vector<unsigned char>(0x200),
        source=std::vector<unsigned char>(0x88),backing=std::vector<unsigned char>(0x90),renderer=std::vector<unsigned char>(0x1200),
        mesh=std::vector<unsigned char>(80),context=std::vector<unsigned char>(0x800),draw=std::vector<unsigned char>(0x90);
    std::array<unsigned char,8> ownerSlot{};
    std::array<std::array<unsigned char,128>,3> wrappers{};
    std::array<std::array<unsigned char,144>,3> backings{};
    std::uint64_t counter=100;
    Fixture(){
        using namespace facecontinuous;parts=decltype(parts){};auto& part=parts[PartIndex];part.index=PartIndex;
        observer::imageBase=Address(image);const auto base=observer::imageBase;
        const auto p=Address(player),b=Address(bridge),m=Address(model),s=Address(source),r=Address(renderer);
        anchorPlayer=p;mainPlayerOwners={};mainPlayerOwners[PartIndex]=p;
        drawfilter::owners.bits=0;drawfilter::owners.Publish(mainPlayerOwners);
        equipmentnative::profiles={};auto& profile=equipmentnative::profiles[PartIndex];
        profile.configured=true;profile.part=PartIndex;profile.vertexBytes=64;profile.meshCount=1;
        profile.path="pl\\f_equip\\pl001_0000\\body\\mod\\f_body001_0000";
        Put(image,mhm::localplayer::singletonSlot,Address(singleton));Put(singleton,0,base+mhm::localplayer::singletonVtable);
        Put(singleton,0xAE40,std::uintptr_t{0x60000});Put(singleton,0x48,mhm::localplayer::Entry{0x60000,0,-1,p});
        Put(player,0,base+observed::ownerVtableRva);player[0xC]=0xE;Put(player,0x126C8,b);Put(player,0x2A0,m);
        Put(bridge,0,base+0x324A830);Put(bridge,0x60,p);Put(bridge,0xF0,p);Put(bridge,0x480,m);
        Put(image,observed::ownerVtableRva,base+facelife::destructorRva);
        Put(image,observed::ownerVtableRva+25*8,base+facelife::removeRva);Put(image,observed::ownerVtableRva+38*8,base+facelife::modelRva);
        Put(model,0,base+observed::modelVtableRva);std::memcpy(model.data()+0xC,profile.path.c_str(),profile.path.size()+1);
        Put(model,0x100,64u);Put(model,0xC8,Address(mesh));Put(model,0xD0,1u);Put(model,0x110,s);
        Put(source,0,base+observed::vertexVtableRva);Put(source,0x48,1u);Put(source,0x58,1u);Put(source,0x5C,64u);Put(source,0x60,Address(backing));
        Put(backing,0,base+0x3539948);Put(backing,0x20,std::uintptr_t{0x90000});
        Put(image,0x51C7050,r);Put(renderer,0x168,std::uintptr_t{0x30000});Put(renderer,0x1E0,counter);
        Put(ownerSlot,0,p);Put(image,observed::sourceBindingIdRva,0u);Put(context,0x710,s);
        mainPlayerSelection=mhm::localplayer::Select([](std::uintptr_t a,auto& value){return observer::Read(a,value);},base);
        Need(mainPlayerSelection.status==mhm::localplayer::Status::Ready&&Matches(PartIndex,{p,m,s,p},false));
        std::array<std::uintptr_t,3> handles{};
        for(unsigned i=0;i<3;i++){
            auto& buffer=part.buffers[i];buffer.wrapper=Address(wrappers[i]);buffer.backing=Address(backings[i]);buffer.bytes=64;
            buffer.view=0xA0000+i*0x100;buffer.resource=0xB0000+i*0x100;buffer.initialization=10+i;handles[i]=buffer.wrapper;
            Put(wrappers[i],0,base+observed::vertexVtableRva);Put(wrappers[i],0x48,1u);Put(wrappers[i],0x5C,64u);
            Put(wrappers[i],0x60,buffer.backing);Put(wrappers[i],0x78,buffer.view);
            Put(backings[i],0,base+0x3539948);Put(backings[i],0x10,64u);Put(backings[i],0x20,buffer.resource);Put(backings[i],0x78,buffer.initialization);
        }
        mhm::Asset asset;asset.part=PartIndex;asset.model="pl/synthetic/original-input";asset.basis.resize(64);
        mhm::Key key;strcpy_s(key.info.id,"controlled");key.info.maximum=1;key.deltas.push_back({0,1,0,0});asset.keys.push_back(key);
        part.asset=std::make_shared<const mhm::Asset>(std::move(asset));part.binding=std::make_unique<equipmentmorph::PartBinding>();
        part.binding->Attach(*part.asset);part.target=part.binding->target;armed=true;
        Need(Adapter().Arm()&&Adapter().Capture({p,m,s,p},s)&&Adapter().PublishResources(handles));StartCycle();
    }
    ~Fixture(){
        facecontinuous::parts=decltype(facecontinuous::parts){};facecontinuous::parts=std::move(oldParts);
        equipmentnative::profiles=std::move(oldProfiles);observer::imageBase=oldBase;facecontinuous::anchorPlayer=oldPlayer;
        facecontinuous::mainPlayerSelection=oldSelection;facecontinuous::mainPlayerOwners=oldOwners;facecontinuous::armed=oldArmed;
        drawfilter::owners.bits=oldDrawOwners;
    }
    facecontinuous::Part& Part(){return facecontinuous::parts[PartIndex];}
    facecontinuous::LaunchAdapter& Adapter(){return *Part().adapter;}
    facecontinuous::Frame Frame(){return {0x50000,Address(renderer),0x30000,counter,counter,1,0,GetCurrentThreadId()};}
    void StartCycle(){Need(Adapter().Launch(Frame(),0,0x70000));}
    void EndCycle(){
        Need(Adapter().Wait(Frame(),0,0x80000,0)&&Adapter().Boundary(Frame()));
        counter++;Put(renderer,0x1E0,counter);Need(Adapter().Signal(Address(renderer),0x30000,counter,counter,counter,true));
    }
    void Cycle(){EndCycle();StartCycle();}
    void Request(float weight,std::uint64_t frame){launchpool::MorphVersion version{};version.frame=version.revision=frame;version.weights[0]=weight;Need(Adapter().Request(version));}
    void Queue(){const auto token=Adapter().Prepare();Need(token&&Adapter().BeginUpload(*token)&&Adapter().QueueUpload(*token,20));}
    safetyhook::Context Context(unsigned kind){
        safetyhook::Context c{};c.rdi=kind==2?Address(draw):Address(player);c.r15=Address(player);
        c.rsi=Address(mesh);c.r13=Address(mesh);c.rcx=Address(context);c.r8=kind==3?7:Address(source);c.rbp=Address(ownerSlot)-0xD0;return c;
    }
    std::uint64_t Input(unsigned kind,bool replaced=false){
        Put(context,0x710,replaced?Address(wrappers[0]):Address(source));auto c=Context(kind);reads=0;
        facecontinuous::Input(c,kind);const auto observedReads=reads;
        Need(c.r8==(kind==3?7:replaced?Address(wrappers[0]):Address(source)));return observedReads;
    }
};
inline unsigned Run(){
    unsigned count=0;auto check=[&](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);count++;};
    {
        Fixture fixture;const auto target=fixture.Part().target;
        check(fixture.Input(1)==0,"stable zero compute bypasses repeated native memory reads");
        check(fixture.Input(2)==0,"stable zero draw preserves original input without snapshot reads");
        check(fixture.Input(3)==1,"stable zero dispatch only reads its owner frame slot");
        check(fixture.Adapter().OriginalBypasses()==3,"zero bypass exposes a bounded aggregate counter without callback logging");
        check(fixture.Part().target==target&&fixture.Adapter().Live(),"zero bypass keeps public target live");
        fixture.Cycle();check(fixture.Adapter().Live(),"skipped original inputs preserve compute-dispatch coverage");
        fixture.Request(.6f,1);fixture.Queue();Need(fixture.Adapter().Completed(20,100));
        check(fixture.Input(1)==0&&fixture.Input(3)==1,"mid-cycle nonzero request cannot split original compute and dispatch routes");
        fixture.Cycle();
        check(fixture.Input(1,true)>0&&fixture.Input(3,true)>1,"nonzero publication restores full compute and dispatch checks");
        check(fixture.Adapter().Inputs()==1&&fixture.Adapter().Dispatches()==1,"nonzero callbacks retain coverage accounting");
        fixture.Request(0,2);
        check(fixture.Input(1,true)>0&&fixture.Input(3,true)>1,"zero request cannot bypass a still-published nonzero buffer");
        fixture.Cycle();
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"original inputs retain full checks while old resources retire");
        fixture.Cycle();Need(fixture.Adapter().Completed(20,102));
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"incomplete drawing Fence prevents zero bypass despite original publication");
        fixture.EndCycle();Need(fixture.Adapter().Completed(20,fixture.counter));fixture.StartCycle();
        check(fixture.Input(1)==0&&fixture.Input(3)==1,"completed successor and GPU proof restore zero bypass at next cycle");
        fixture.Cycle();check(fixture.Adapter().Live(),"zero-nonzero-zero cycle remains live");
    }
    {
        Fixture fixture;fixture.Request(.6f,1);fixture.Queue();fixture.Cycle();
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"pending upload cannot enter zero bypass");
        Need(fixture.Adapter().Completed(20,100));
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"mid-cycle upload completion does not change callback route");
        fixture.Cycle();check(fixture.Input(1,true)>0&&fixture.Input(3,true)>1,"ready nonzero buffer still uses guarded publication");
    }
    {
        Fixture fixture;fixture.Request(.6f,1);Need(bool(fixture.Adapter().Prepare()));fixture.Cycle();
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"preparing slot cannot enter zero bypass");
        fixture.Request(0,2);check(fixture.Input(1)>0&&fixture.Input(3)>1,"cancellation cannot change route inside a native cycle");
        fixture.Cycle();check(fixture.Input(1)==0&&fixture.Input(3)==1,"cancelled unused slot can return to zero bypass at next launch");
    }
    {
        Fixture fixture;fixture.Request(.6f,1);fixture.Cycle();
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"unprepared nonzero request cannot use the original zero route");
        const auto token=fixture.Adapter().Prepare();Need(token&&fixture.Adapter().BeginUpload(*token));fixture.Cycle();
        check(fixture.Input(1)>0&&fixture.Input(3)>1,"awaiting upload stamp cannot use the original zero route");
    }
    {
        Fixture fixture;auto changing=fixture.Context(1);changing.rcx=Address(fixture.player);changing.rdx=Address(fixture.model)+128;
        facecontinuous::ModelChanging(changing);
        check(!fixture.Adapter().Live()&&std::strcmp(fixture.Adapter().Reason(),"equipment_part_model_replaced")==0,"zero bypass preserves model-change invalidation");
        fixture.Input(1);check(!fixture.Adapter().Live(),"invalidated generation is never reactivated by original input");
    }
    {
        Fixture fixture;Put(fixture.singleton,0xAE40,std::uintptr_t{0});
        check(fixture.Input(1)==0,"changed selection while zero leaves the original input untouched");
        fixture.Request(.6f,1);fixture.Queue();Need(fixture.Adapter().Completed(20,100));fixture.Cycle();
        check(fixture.Input(1)>0&&!fixture.Adapter().Live(),"nonzero route revalidates main player before any private substitution");
    }
    return count;
}
}
