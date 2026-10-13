#include "named_control.hpp"

// The caller supplies identifiers exported with its own asset package.
// No expression policy, game pointer, hook or background loop is implemented here.
class ManualMorphController {
    mhwi::KeyClient client;
public:
    MhmResult Connect(){return mhwi_example::ConnectLoaded(client);}
    MhmResult SetKeys(const std::vector<mhwi::NamedWeight>& values,uint64_t& revision){return client.Set(values,revision);}
    MhmResult ReleaseKey(uint32_t part,const std::string& model,const std::string& id,uint64_t& revision){return client.ReleaseKey(part,model,id,revision);}
    MhmResult OnTimer(){const auto result=client.Refresh();return result==MHM_OK?client.Heartbeat():result;}
    MhmResult ReleaseAll(){return client.Release();}
};
