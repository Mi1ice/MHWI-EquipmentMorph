#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include "EquipmentLabels.generated.hpp"

namespace morphequipment {
inline std::string Label(std::string_view model){
    const auto found=std::lower_bound(std::begin(labels),std::end(labels),model,[](const auto& item,auto value){return item.model<value;});
    std::string_view resource=model;
    if(model.starts_with("pl/f_equip/")||model.starts_with("pl/m_equip/")){
        const auto end=model.find('/',11);if(end!=std::string_view::npos)resource=model.substr(11,end-11);
    }
    if(found!=std::end(labels)&&found->model==model&&!found->name.empty())return std::string(found->name)+"（"+std::string(resource)+"）";
    return std::string(resource);
}
}
