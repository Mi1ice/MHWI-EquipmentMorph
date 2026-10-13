#pragma once
#include <cstdint>

namespace mhm::localplayer {
// Derived from the exact guarded FindMasterPlayer code of the pinned build.
// Data reads only: neither SPL nor a game function invocation is required.
inline constexpr std::uintptr_t singletonSlot=0x500ECA0,singletonVtable=0x33FE5B0;
inline constexpr std::uintptr_t playerVtable=0x34A6D18,selectorRva=0x1B42010;
inline constexpr unsigned selectorBytes=89;
inline constexpr char selectorSha[]="ac20959afacdc9adbdad72a972ad78a56137a08ad86564816756a3bc63323b94";
struct Entry {
    std::uint64_t record=0;
    std::int32_t first=-1,second=-1;
    std::uint64_t player=0;
    bool operator==(const Entry&)const=default;
};
static_assert(sizeof(Entry)==24);
enum class Status {Unavailable,None,Ready};
struct Selection {
    Status status=Status::Unavailable;
    std::uintptr_t root=0,record=0,player=0;
    unsigned index=0;
    Entry entry{};
    bool operator==(const Selection&)const=default;
};
inline bool Pointer(std::uintptr_t p,std::uintptr_t span=0){return p>0xFFFF&&p<0x0000800000000000ULL-span;}
template<class Reader> Selection Select(Reader&& read,std::uintptr_t image){
    Selection result;std::uintptr_t table=0,again=0;
    if(!Pointer(image,0x55D3000)||!read(image+singletonSlot,result.root)||!Pointer(result.root,0xC280)
        ||!read(result.root,table)||table!=image+singletonVtable||!read(result.root+0xAE40,result.record))return result;
    result.status=Status::None;if(!result.record)return result;
    for(unsigned i=0;i<24;i++){
        Entry entry{};if(!read(result.root+0x48+i*0x740,entry)){result.status=Status::Unavailable;return result;}
        if((entry.first<0&&entry.second<0)||entry.record!=result.record)continue;
        result.index=i;result.entry=entry;
        // Match the game's FIRST matching record, including a null/inactive
        // player. Never skip it and silently select a later/remote character.
        std::uint8_t flags=0;
        if(!entry.player)return result;
        if(!Pointer(entry.player,0x126D0)||!read(entry.player+0xC,flags)||!read(entry.player,table)){result.status=Status::Unavailable;return result;}
        if(!(flags&0x0E))return result;
        if(table!=image+playerVtable){result.status=Status::Unavailable;return result;}
        if(!read(image+singletonSlot,again)||again!=result.root||!read(result.root+0xAE40,again)||again!=result.record){result.status=Status::Unavailable;return result;}
        Entry repeated{};if(!read(result.root+0x48+i*0x740,repeated)||repeated!=entry){result.status=Status::Unavailable;return result;}
        result.player=entry.player;result.status=Status::Ready;return result;
    }
    return result;
}
template<class Reader> bool StillSelected(Reader&& read,std::uintptr_t image,const Selection& expected){
    // Bounded prefix lookup is usually one entry; it catches a changed earlier
    // match as well as replacement of the cached entry, root or main record.
    if(expected.status!=Status::Ready)return false;
    const auto current=Select(read,image);
    return current.status==Status::Ready&&current.root==expected.root&&current.record==expected.record
        &&current.player==expected.player&&current.index==expected.index&&current.entry==expected.entry;
}
}
