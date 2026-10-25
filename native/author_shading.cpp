// Small C boundary for the fixed Blender 5.1.2 MikkTSpace implementation.
// No Blender normal-space storage, Python callbacks, or parallel reductions.
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#define LIKELY(value) (value)
#define UNLIKELY(value) (value)
using uint=unsigned;
#include "../research/blender-mikktspace-5.1.2/mikktspace.hh"

namespace {
struct Corners {
    unsigned count;const float *positions,*normals,*uv;float* result;
    unsigned GetNumFaces()const{return count/3;}
    unsigned GetNumVerticesOfFace(unsigned)const{return 3;}
    bool has_uv()const{return true;}
    mikk::float3 GetPosition(unsigned face,unsigned corner)const{return {positions+(face*3+corner)*3};}
    mikk::float3 GetNormal(unsigned face,unsigned corner)const{return {normals+(face*3+corner)*3};}
    mikk::float3 GetTexCoord(unsigned face,unsigned corner)const{const auto p=uv+(face*3+corner)*2;return {p[0],p[1],0.f};}
    void SetTangentSpace(unsigned face,unsigned corner,mikk::float3 tangent,bool orientation){
        auto out=result+(face*3+corner)*4;out[0]=tangent.x;out[1]=tangent.y;out[2]=tangent.z;out[3]=orientation?1.f:-1.f;
    }
};
}
extern "C" __declspec(dllexport) unsigned MhmAuthorShadingVersion(){return 1;}
extern "C" __declspec(dllexport) int MhmAuthorTangents(unsigned count,const float* positions,const float* normals,const float* uv,float* result){
    if(!count||count%3||count>3000000||!positions||!normals||!uv||!result)return 1;
    for(unsigned i=0;i<count;++i){
        float length=0;
        for(unsigned axis=0;axis<3;++axis){
            if(!std::isfinite(positions[i*3+axis])||!std::isfinite(normals[i*3+axis]))return 1;
            length+=normals[i*3+axis]*normals[i*3+axis];
        }
        if(!std::isfinite(length)||length<1e-12f)return 1;
        for(unsigned axis=0;axis<2;++axis)if(!std::isfinite(uv[i*2+axis]))return 1;
    }
    for(unsigned i=0;i<count*4;++i)result[i]=std::numeric_limits<float>::quiet_NaN();
    try{Corners mesh{count,positions,normals,uv,result};mikk::Mikktspace<Corners> generator(mesh);generator.genTangSpace();return 0;}
    catch(...){return 2;}
}
