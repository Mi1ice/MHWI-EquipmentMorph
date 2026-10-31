#pragma once
#include "../../runtime/EquipmentMorphPerKeyCore/include/mhw_morph_api.h"
#include <cstring>
#include <type_traits>

// Optional, versioned panel-to-expression contract. This is not a core SDK extension.
namespace morphexpression {
inline constexpr uint32_t Version=1;
struct Config {
    uint32_t enabled=0,mapped=0,part=0;
    char model[128]{},modelSha[65]{},packSha[65]{},key[64]{};
};
enum class Status:uint32_t {Starting,Disabled,WaitingTarget,Tracking,WaitingSource,Inconsistent,Unsupported,ApiError};
struct State {
    uint32_t version=Version,bytes=sizeof(State),ready=0;
    Config config;
    uint64_t completed=0,tick=0,target=0,source=0,winner=0;
    MhmResult configurationResult=MHM_OK;
    Status status=Status::Starting;
    float closure=0,applied=0;
    char detail[128]{};
};
using Read=MhmResult(MHM_CALL*)(uint32_t,uint32_t,State*);
using Configure=MhmResult(MHM_CALL*)(uint32_t,uint32_t,const Config*,uint64_t*);
template<size_t N> inline bool Text(const char (&text)[N]){
    const auto* end=static_cast<const char*>(std::memchr(text,0,N));
    if(!end||end==text)return false;
    for(auto* at=text;at!=end;++at)if(static_cast<unsigned char>(*at)<32||static_cast<unsigned char>(*at)>126)return false;
    return true;
}
inline bool Sha(const char (&text)[65]){
    if(text[64]!=0)return false;
    for(size_t i=0;i<64;++i)if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return false;
    return true;
}
inline bool Valid(const Config& config){
    return config.enabled<=1&&config.mapped<=1&&config.part<=4&&
        (config.mapped?(Text(config.model)&&Sha(config.modelSha)&&Sha(config.packSha)&&Text(config.key)):
        (!config.part&&!config.model[0]&&!config.modelSha[0]&&!config.packSha[0]&&!config.key[0]));
}
static_assert(std::is_trivially_copyable_v<Config>&&std::is_trivially_copyable_v<State>);
static_assert(sizeof(Config)==336&&sizeof(State)==536);
}
