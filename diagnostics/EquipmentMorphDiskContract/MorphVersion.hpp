#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
#include "../../runtime/EquipmentMorphPerKeyCore/capacity_limits.hpp"

namespace launchpool {
// Owned once on the worker; callback copies share immutable actual-count weights.
struct MorphVersion {
private:
    std::shared_ptr<const std::vector<float>> storage;
public:
    std::uint64_t frame=0,revision=0;
    static MorphVersion FromWeights(std::span<const float> values,std::uint64_t frame,std::uint64_t revision){
        if(values.empty()||values.size()>mhm::capacity::keys_per_target)throw std::invalid_argument("Morph snapshot count");
        MorphVersion result;result.storage=std::make_shared<const std::vector<float>>(values.begin(),values.end());
        result.frame=frame;result.revision=revision;return result;
    }
    std::span<const float> Weights()const noexcept{return storage?std::span<const float>(*storage):std::span<const float>{};}
    bool SameWeights(const MorphVersion& other)const noexcept{
        const auto a=Weights(),b=other.Weights();return std::equal(a.begin(),a.end(),b.begin(),b.end());
    }
    bool operator==(const MorphVersion& other)const noexcept{return frame==other.frame&&revision==other.revision&&SameWeights(other);}
    bool Valid()const noexcept{for(float w:Weights())if(!std::isfinite(w)||w<0||w>1)return false;return true;}
    bool IsZero()const noexcept{for(float w:Weights())if(w!=0)return false;return true;}
};
}
