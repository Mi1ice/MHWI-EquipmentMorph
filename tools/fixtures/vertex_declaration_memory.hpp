#pragma once
#include "vertex_declarations.hpp"
#include <array>
#include <cstring>
#include <vector>
namespace captureddeclarations {
template<class Bytes,class T> void Put(Bytes& raw,size_t at,T value){std::memcpy(raw.data()+at,&value,sizeof(value));}
template<class Bytes> uintptr_t Address(Bytes& raw){return reinterpret_cast<uintptr_t>(raw.data());}
struct Memory {
    std::vector<unsigned char> root=std::vector<unsigned char>(9496),metadata=std::vector<unsigned char>(260),
        array=std::vector<unsigned char>(45*8),nativeArray=std::vector<unsigned char>(45*8);
    std::array<std::vector<unsigned char>,45> source,native,loaded;
    std::vector<std::array<char,64>> names=std::vector<std::array<char,64>>(14*16);
    uintptr_t pointer=0;
    Memory(){
        pointer=Address(root);Put(root,56,Address(metadata));Put(root,9312,Address(nativeArray));Put(root,9492,45u);
        Put(metadata,200,Address(array));Put(metadata,256,45u);
        for(size_t i=0;i<45;i++){
            source[i].resize(24);native[i].resize(120);Put(source[i],12,directory[i]);Put(native[i],76,directory[i]);
        }
        size_t name=0;
        for(const auto& r:records){
            source[r.index].resize(24+16*r.elements.size());loaded[r.index].resize(16*r.elements.size());
            Put(source[r.index],16,static_cast<uint16_t>(r.elements.size()));Put(source[r.index],18,static_cast<uint16_t>(r.stride));Put(native[r.index],112,static_cast<uint32_t>(r.elements.size()));Put(native[r.index],116,r.stride);
            for(size_t j=0;j<r.elements.size();j++){
                const auto& e=r.elements[j];std::memcpy(names[name].data(),e.name,std::strlen(e.name)+1);
                Put(source[r.index],24+j*16,Address(names[name++]));Put(source[r.index],32+j*16,e.packed);Put(source[r.index],36+j*16,e.reserved);
            }
            std::memcpy(loaded[r.index].data(),source[r.index].data()+24,loaded[r.index].size());Put(native[r.index],104,Address(loaded[r.index]));
        }
        for(size_t i=0;i<45;i++){Put(array,i*8,Address(source[i]));Put(nativeArray,i*8,Address(native[i]));}
    }
    uintptr_t Slot(){return reinterpret_cast<uintptr_t>(&pointer);}
};
}
