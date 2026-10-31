#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace morphgui {
// Display only: never submit, release, reorder, or quantize contributions.
struct KeyFilter {
    char search[129]{};int mode=0;uint64_t target=0;
    void Clear(){search[0]=0;mode=0;}
    void Select(uint64_t handle){if(handle!=target){target=handle;Clear();}}
    bool Matches(std::string_view name,bool owned,float weight)const{
        std::string_view query=search;
        while(!query.empty()&&static_cast<unsigned char>(query.front())<=32)query.remove_prefix(1);
        while(!query.empty()&&static_cast<unsigned char>(query.back())<=32)query.remove_suffix(1);
        const auto fold=[](unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+'a'-'A'):c;};
        const bool named=query.empty()||std::search(name.begin(),name.end(),query.begin(),query.end(),[&](unsigned char a,unsigned char b){return fold(a)==fold(b);})!=name.end();
        return named&&(mode==0||(mode==1&&owned)||(mode==2&&!owned)||(mode==3&&owned&&weight!=0.f));
    }
    bool Visible(std::string_view name,bool owned,float weight,bool active,uint64_t ticket,uint64_t completed)const{
        return active||ticket>completed||Matches(name,owned,weight);
    }
};
struct FeedbackTime {
    uint64_t completed=0;double since=0;
    bool Show(uint64_t ticket,bool success,double now){
        if(completed!=ticket){completed=ticket;since=now;}
        return !success||!ticket||now-since<3.;
    }
};
}
