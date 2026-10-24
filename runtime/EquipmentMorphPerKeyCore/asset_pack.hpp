#pragma once
#include "core.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <Windows.h>
#include <bcrypt.h>

namespace mhm::pack {
inline std::string Digest(std::span<const unsigned char> bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    std::array<unsigned char,32> digest{};
    const auto a=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    const auto b=a>=0?BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0):a;
    const auto c=b>=0?BCryptHashData(hash,const_cast<unsigned char*>(bytes.data()),static_cast<ULONG>(bytes.size()),0):b;
    const auto d=c>=0?BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0):c;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(d<0)throw std::runtime_error("Pack SHA256");
    constexpr char digits[]="0123456789abcdef";std::string result;
    for(unsigned char value:digest){result+=digits[value>>4];result+=digits[value&15];}return result;
}
class Reader {
    std::span<const unsigned char> bytes;size_t cursor=0;
public:
    explicit Reader(std::span<const unsigned char> data):bytes(data){}
    std::span<const unsigned char> Take(size_t count) {
        if(count>bytes.size()-cursor)throw std::runtime_error("Truncated pack");
        const auto result=bytes.subspan(cursor,count);cursor+=count;return result;
    }
    uint32_t U32(){const auto raw=Take(4);return uint32_t(raw[0])|(uint32_t(raw[1])<<8)|(uint32_t(raw[2])<<16)|(uint32_t(raw[3])<<24);}
    std::string Identifier(size_t bound) {
        const auto count=U32();if(!count||count>=bound)throw std::runtime_error("Pack identifier size");
        const auto raw=Take(count);std::string result(raw.begin(),raw.end());
        if(!mhm::Identifier(result.c_str(),bound)||result.find('\0')!=result.npos)throw std::runtime_error("Pack identifier");return result;
    }
    bool Done()const{return cursor==bytes.size();}
};
inline Asset Decode(std::span<const unsigned char> data) {
    if(data.size()>128*1024*1024)throw std::runtime_error("Pack size");
    Reader input(data);const auto magic=input.Take(8);
    const auto version=input.U32();
    const bool advanced=version==2&&std::memcmp(magic.data(),"MHMPACK2",8)==0;
    if(!advanced&&(version!=1||std::memcmp(magic.data(),"MHMPACK1",8)!=0))throw std::runtime_error("Pack version");
    Asset asset;asset.perKeyNormals=advanced;asset.part=input.U32();const auto basisBytes=input.U32(),keys=input.U32(),mode=input.U32();
    if(advanced&&mode!=1)throw std::runtime_error("Pack normal composition mode");
    const auto rotations=advanced?0:mode;
    if(asset.part>MHM_LEGS||!basisBytes||basisBytes>64*1024*1024||!keys||keys>limits.keys_per_target||rotations>1000000)throw std::runtime_error("Pack bounds");
    asset.model=input.Identifier(128);const auto basis=input.Take(basisBytes);asset.basis.assign(basis.begin(),basis.end());
    static_assert(sizeof(MhmDelta)==16&&sizeof(MhmRotation)==20);
    uint32_t totalNormals=0;
    for(uint32_t i=0;i<keys;i++) {
        // The identifier length precedes the five fixed key fields in the pack.
        const auto idBytes=input.U32();if(!idBytes||idBytes>=64)throw std::runtime_error("Pack key identifier size");
        Key key;const auto minimum=input.U32(),maximum=input.U32();
        std::memcpy(&key.info.minimum,&minimum,4);std::memcpy(&key.info.maximum,&maximum,4);
        key.info.flags=input.U32();key.info.exclusive_group=input.U32();const auto count=input.U32();
        const auto normalCount=advanced?input.U32():0;
        if(count>1000000||normalCount>1000000)throw std::runtime_error("Pack delta bound");
        if(normalCount>1000000-totalNormals)throw std::runtime_error("Pack total normal bound");
        totalNormals+=normalCount;
        const auto id=input.Take(idBytes);std::memcpy(key.info.id,id.data(),id.size());
        if(std::memchr(id.data(),0,id.size()))throw std::runtime_error("Pack key identifier");
        const auto deltas=input.Take(static_cast<size_t>(count)*16);key.deltas.resize(count);
        if(count)std::memcpy(key.deltas.data(),deltas.data(),deltas.size());
        const auto normals=input.Take(static_cast<size_t>(normalCount)*16);key.normals.resize(normalCount);
        if(normalCount)std::memcpy(key.normals.data(),normals.data(),normals.size());asset.keys.push_back(std::move(key));
    }
    const auto raw=input.Take(static_cast<size_t>(rotations)*20);asset.rotations.resize(rotations);
    if(rotations)std::memcpy(asset.rotations.data(),raw.data(),raw.size());
    if(!input.Done()||!Validate(asset))throw std::runtime_error("Pack asset fields/trailing data");return asset;
}
inline Asset Load(const std::filesystem::path& path,const std::string& expectedSha) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Pack file");
    const auto size=file.tellg();if(size<=0||size>128*1024*1024)throw std::runtime_error("Pack file size");
    std::vector<unsigned char> raw(static_cast<size_t>(size));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(raw.data()),static_cast<std::streamsize>(raw.size()))||Digest(raw)!=expectedSha)throw std::runtime_error("Pack read/digest");
    return Decode(raw);
}
}
