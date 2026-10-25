#pragma once
#include "PanelData.hpp"
#include "KeyFilter.hpp"
#include <span>
namespace morphgui {
struct RowEdit {float value=0;uint64_t ticket=0;bool active=false;};
class KeyRows {
    uint64_t handle=0;std::vector<std::string> ids;std::vector<RowEdit> edits;
public:
    void Clear(){handle=0;ids.clear();edits.clear();}
    void Bind(const mhmcontrol::Target& target,std::span<const mhmcontrol::Key> keys){
        bool same=handle==target.info.handle&&ids.size()==keys.size();
        for(size_t i=0;same&&i<keys.size();++i)same=ids[i]==keys[i].info.id;
        if(same)return;Clear();handle=target.info.handle;edits.resize(keys.size());
        for(const auto& key:keys)ids.emplace_back(key.info.id);
    }
    RowEdit& At(uint32_t originalIndex){return edits.at(originalIndex);}
    bool Pending(uint64_t completed)const{return std::any_of(edits.begin(),edits.end(),[&](const auto& edit){return edit.ticket>completed;});}
    std::vector<uint32_t> Visible(const KeyFilter& filter,std::span<const mhmcontrol::Key> keys,uint64_t completed)const{
        std::vector<uint32_t> result;for(uint32_t i=0;i<keys.size();++i){const auto& k=keys[i];const auto& e=edits.at(i);if(filter.Visible(k.info.id,k.owned!=0,k.contribution,e.active,e.ticket,completed))result.push_back(i);}return result;
    }
};
}
