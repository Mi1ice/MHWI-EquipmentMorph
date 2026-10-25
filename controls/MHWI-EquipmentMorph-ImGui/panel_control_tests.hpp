#pragma once
#include "Control.hpp"
#include "KeyRows.hpp"
namespace paneltest {
inline void TestLastKeyAndFiltering(){
    for(auto count:{489u,1024u}){const auto h=Attach(MHM_BODY,count);morphgui::Control control;Check(control.Connect(Api())==MHM_OK,"control connects large target");
        const auto s=control.Snapshot();morphgui::KeyRows rows;const auto& t=s->data.targets[0];const auto table=std::span(s->data.keys).subspan(t.first,t.count);rows.Bind(t,table);
        morphgui::KeyFilter filter;const auto query="Key_"+std::to_string(count-1);strcpy_s(filter.search,query.c_str());const auto visible=rows.Visible(filter,table,0);
        Check(visible.size()==1&&visible[0]==count-1,"filtered last row uses original index");
        const auto ticket=control.Post(mhmcontrol::Action::Set,h,visible[0],.123456791f);control.Service(10);
        Check(ticket&&control.Snapshot()->completed==ticket&&control.Snapshot()->data.keys.back().owned&&control.Snapshot()->data.keys.back().contribution==.123456791f&&!control.Snapshot()->data.keys.front().owned,"last key precise edit only");
        rows.At(0).active=true;rows.At(1).ticket=2;const auto retained=rows.Visible(filter,table,1);Check(retained.size()==3&&retained[0]==0&&retained[1]==1,"active and pending survive filter");
        auto next=t;++next.info.handle;rows.Bind(next,table);Check(!rows.At(0).active&&!rows.At(1).ticket,"new generation clears drafts");Core().Detach(h);
    }
}
inline void TestQueuedBatchIsAtomic(){
    const auto h=Attach(MHM_BODY,489,true);morphgui::Control control;Check(control.Connect(Api())==MHM_OK,"batch control connects");
    control.Post(mhmcontrol::Action::Set,h,0,.4f);control.Service(0);const auto before=control.Snapshot()->data.revision;
    control.Post(mhmcontrol::Action::Set,h,488,.5f);control.Post(mhmcontrol::Action::Set,h,1,.7f);control.Service(1);
    Check(control.Snapshot()->operation==MHM_CONFLICT&&control.Snapshot()->data.revision==before&&!control.Snapshot()->data.keys.back().owned&&control.Snapshot()->data.keys.front().contribution==.4f,"queued conflict leaves entire batch unchanged");
    control.Post(mhmcontrol::Action::Drop,h,0);control.Post(mhmcontrol::Action::Set,h,488,.8f);control.Service(2);
    Check(control.Snapshot()->operation==MHM_OK&&!control.Snapshot()->data.keys.front().owned&&control.Snapshot()->data.keys.back().contribution==.8f,"drop and set validated as one replacement");Core().Detach(h);
}
inline void TestQueueFullRejects(){morphgui::Control control;bool accepted=true;for(uint32_t i=0;i<5120;++i)accepted=accepted&&control.Post(mhmcontrol::Action::Set,1,i,.1f)!=0;Check(accepted,"queue accepts all 5120 entries");Check(control.Post(mhmcontrol::Action::Set,1,5120,.1f)==0,"queue overflow returns refusal");}
inline void ControlTests(){TestLastKeyAndFiltering();TestQueuedBatchIsAtomic();TestQueueFullRejects();}
}
