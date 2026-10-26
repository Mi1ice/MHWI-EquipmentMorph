#pragma once
#include "../../runtime/EquipmentMorphDiskConfiguration/vertex_formats.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

namespace mhm::declarations {
// All pointers below are values copied through the supplied protected reader.
// No game pointer is dereferenced or used after a failed generation check.
template<class T> T Value(const unsigned char* raw,size_t at){T value{};std::memcpy(&value,raw+at,sizeof(T));return value;}
inline bool Span(uintptr_t address,size_t bytes){return address>0xffff&&address<=UINTPTR_MAX-bytes;}
template<class Read> bool Block(Read& read,uintptr_t address,void* output,size_t bytes){return Span(address,bytes)&&read(address,output,bytes);}
template<class T,class Read> bool Get(Read& read,uintptr_t address,T& value){return Block(read,address,&value,sizeof(value));}
struct Header {
    uintptr_t root=0,metadata=0,array=0,natives=0;uint32_t count=0,nativeCount=0;
    bool operator==(const Header&)const=default;
};
template<class Read> bool ReadHeader(Read& read,uintptr_t slot,Header& h){
    return Get(read,slot,h.root)&&Span(h.root,9496)&&Get(read,h.root+56,h.metadata)&&Span(h.metadata,260)
        &&Get(read,h.metadata+200,h.array)&&Get(read,h.metadata+256,h.count)
        &&Get(read,h.root+9312,h.natives)&&Get(read,h.root+9492,h.nativeCount)
        &&h.count&&h.count<=4096&&h.nativeCount==h.count&&Span(h.array,h.count*8ull)&&Span(h.natives,h.count*8ull);
}
struct Copy {uintptr_t address=0;std::vector<unsigned char> bytes;uint32_t disk=0;};
struct Entry {uint32_t disk=0,stride=0;bool verified=false;};
struct Snapshot {
    uintptr_t slot=0;Header header{};std::vector<uintptr_t> records,natives;std::vector<Entry> entries;std::vector<Copy> copies;
    bool SameGeneration(const Snapshot& other)const{return slot==other.slot&&header==other.header&&records==other.records&&natives==other.natives;}
    template<class Read> bool Stable(Read& read)const{
        Header current{};if(!slot||!ReadHeader(read,slot,current)||current!=header)return false;
        std::array<uintptr_t,4096> a{},b{};
        return Block(read,header.array,a.data(),header.count*8ull)&&Block(read,header.natives,b.data(),header.count*8ull)
            &&std::equal(records.begin(),records.end(),a.begin())&&std::equal(natives.begin(),natives.end(),b.begin())
            &&ReadHeader(read,slot,current)&&current==header;
    }
    template<class Read> bool Unchanged(Read& read,std::span<const uint32_t> expected={})const{
        if(!Stable(read))return false;
        for(const auto& c:copies){
            if(!expected.empty()&&std::find(expected.begin(),expected.end(),c.disk)==expected.end())continue;
            std::array<unsigned char,256> now{};
            if(c.bytes.size()>now.size()||!Block(read,c.address,now.data(),c.bytes.size())||!std::equal(c.bytes.begin(),c.bytes.end(),now.begin()))return false;
        }
        return Stable(read);
    }
};
template<class Read> bool Save(Read& read,Snapshot& s,uintptr_t address,size_t bytes,std::vector<unsigned char>& raw,uint32_t disk=0){
    raw.resize(bytes);if(!Block(read,address,raw.data(),bytes))return false;s.copies.push_back({address,raw,disk});return true;
}
template<class Read> bool Capture(Read& read,uintptr_t slot,Snapshot& output){
    output={};Snapshot s;s.slot=slot;
    if(!ReadHeader(read,slot,s.header))return false;
    const auto count=s.header.count;s.records.resize(count);s.natives.resize(count);s.entries.resize(count);
    if(!Block(read,s.header.array,s.records.data(),count*8ull)||!Block(read,s.header.natives,s.natives.data(),count*8ull))return false;
    std::set<uint32_t> hashes;
    for(size_t i=0;i<count;i++){
        const auto record=s.records[i],native=s.natives[i];std::vector<unsigned char> raw;
        if(!Span(record,24)||!Span(native,120)||!Save(read,s,record,24,raw))return false;
        const auto disk=Value<uint32_t>(raw.data(),12);const auto elements=Value<uint16_t>(raw.data(),16);
        s.copies.back().disk=disk;
        if(!hashes.insert(disk).second)return false;
        auto& entry=s.entries[i];entry.disk=disk;const auto* f=vertexformats::Resolve(disk);
        if(!f)continue; // Other declarations may exist; no unknown row can match.
        if(elements!=f->elements.size()||!elements||elements>16||Value<uint16_t>(raw.data(),18)!=f->stride)return false;
        std::vector<unsigned char> source,loaded,header;
        if(!Save(read,s,record+24,elements*16ull,source,disk)||!Save(read,s,native+76,44,header,disk))return false;
        const auto loadedPointer=Value<uintptr_t>(header.data(),28);
        if(Value<uint32_t>(header.data(),0)!=disk||Value<uint32_t>(header.data(),36)!=elements||Value<uint32_t>(header.data(),40)!=f->stride
            ||!Save(read,s,loadedPointer,elements*16ull,loaded,disk)||source!=loaded)return false;
        for(size_t j=0;j<elements;j++){
            const auto& expected=f->elements[j];const auto* element=source.data()+j*16;
            if(Value<uint32_t>(element,8)!=expected.packed||Value<uint32_t>(element,12)!=expected.reserved)return false;
            std::vector<unsigned char> name;const auto length=std::strlen(expected.semantic)+1;
            if(length>64||!Save(read,s,Value<uintptr_t>(element,0),length,name,disk)||std::memcmp(name.data(),expected.semantic,length))return false;
        }
        entry.stride=f->stride;entry.verified=true;
    }
    if(!s.Unchanged(read))return false;output=std::move(s);return true;
}
}
