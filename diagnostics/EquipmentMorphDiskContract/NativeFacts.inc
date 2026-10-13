bool PatchedCodeMatches() {
    for(unsigned i=0;i<hooks.size();i++) {
        std::array<unsigned char,32> current{};
        if(!observer::Read(observer::imageBase+sites[i],current)||current!=patches[i])return false;
    }
    return true;
}
bool Guard(std::uintptr_t rva,unsigned count,const char* digest) {
    std::vector<unsigned char> data(count);
    if(!observer::Read(observer::imageBase+rva,data.data(),data.size()))return false;
    if(enabled) {
        for(unsigned i=0;i<hooks.size();i++) {
            const auto& original=hooks[i]->original_bytes();
            for(unsigned j=0;j<original.size();j++)if(sites[i]+j>=rva&&sites[i]+j<rva+count)data[sites[i]+j-rva]=original[j];
        }
    }
    return privateprobe::Digest(data.data(),data.size())==digest;
}
bool CodeMatches() {
    if(enabled&&!PatchedCodeMatches())return false;
    for(const auto& g:privateprobe::guards)if(!Guard(g.rva,g.bytes,g.sha))return false;
    for(const auto& g:frameobserver::codeGuards)if(!Guard(g.rva,g.bytes,g.sha))return false;
    for(const auto& g:facelife::lifeGuards)if(!Guard(g.rva,g.bytes,g.sha))return false;
    if(!Guard(mhm::localplayer::selectorRva,mhm::localplayer::selectorBytes,mhm::localplayer::selectorSha))return false;
    std::array<unsigned char,32> dispatch{};
    if(!observer::Read(observer::imageBase+observed::dispatchSiteRva,dispatch))return false;
    if(enabled)std::copy(hooks[2]->original_bytes().begin(),hooks[2]->original_bytes().end(),dispatch.begin());
    return dispatch==observed::dispatchBytes;
}
bool RendererFacts(std::uintptr_t& renderer,std::uintptr_t& fence,std::uint64_t& counter) noexcept {
    return observer::Read(observer::imageBase+0x51C7050,renderer)&&renderer>0xFFFF&&
        observer::Read(renderer+0x168,fence)&&fence>0xFFFF&&observer::Read(renderer+0x1E0,counter);
}
bool EngineFacts(std::uintptr_t engine,Frame& f) noexcept {
    std::uintptr_t actual=0,root=0;f.engine=engine;f.thread=::GetCurrentThreadId();
    return observer::Read(observer::imageBase+0x51C4480,root)&&root==engine&&
        RendererFacts(f.renderer,f.fence,f.before)&&observer::Read(engine+0x78,actual)&&actual==f.renderer&&
        observer::Read(engine+0x68D14,f.workers)&&observer::Read(engine+0x1F3CC,f.mode)&&
        observer::Read(f.renderer+0x1E0,f.after);
}
struct DrawingFence {
    frameobserver::FenceReader reader;
    std::uintptr_t renderer=0,pointer=0;
    std::uint64_t completed=0,polls=0;
    void Attach(std::uintptr_t actualRenderer) {
        renderer=actualRenderer;
        if(!observer::Read(renderer+0x168,pointer)||!privateprobe::InterfaceGuard(pointer)||
           !reader.Attach(reinterpret_cast<ID3D12Fence*>(pointer))||
           !privateprobe::InterfaceGuard(reinterpret_cast<std::uintptr_t>(reader.Retained())))throw std::runtime_error("Drawing Fence interface");
    }
    void Poll() {
        std::uintptr_t r=0,f=0;std::uint64_t count=0;
        if(++polls==UINT64_MAX||!RendererFacts(r,f,count)||r!=renderer||f!=pointer||!reader.Poll(completed)||
           !RendererFacts(r,f,count)||r!=renderer||f!=pointer)throw std::runtime_error("Drawing Fence identity/budget");
    }
};

