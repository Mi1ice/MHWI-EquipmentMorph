#pragma once
#include "Control.hpp"
#include "PresetBrowser.hpp"
#include "Ui.hpp"
#include <imgui.h>
#include <string>

namespace morphgui {
inline const char* ResultText(MhmResult result){
    switch(result){case MHM_OK:return "正常";case MHM_INVALID:return "数值无效或超出范围";case MHM_STALE:return "目标或控制来源已失效，请重新调节";
    case MHM_CAPACITY:return "超出接口容量";case MHM_CONFLICT:return "互斥形态键冲突，原权重保持";case MHM_NOT_READY:return "等待核心或装备就绪";
    case MHM_VERSION:return "核心 API 不兼容";default:return "接口暂时不可用";}
}
struct View {
    uint64_t selected=0;
    std::string selectedPreset,presetError;char presetName[129]{},presetSearch[129]{};uint64_t presetSeen=0,presetWaiting=0,saveTicket=0;
    PresetBrowser browser;
    struct Edit {float value=0;uint64_t ticket=0;bool active=false;};
    std::array<Edit,64> edits{};
    static const char* Part(uint32_t part){const char* names[]={"头部","身体","腕部","腰部","腿部"};return part<5?names[part]:"装备";}
    static std::string Label(const mhmcontrol::Target& target){return std::string(Part(target.info.part))+" · "+target.info.model_id;}
    std::optional<morphpreset::Request> PresetBar(const ControlSnapshot& snapshot){
        const auto& presets=snapshot.presets;
        if(presetWaiting&&snapshot.completed>=presetWaiting)presetWaiting=0;
        if(presets.completed!=presetSeen){presetSeen=presets.completed;if(presets.success&&presets.selectLast&&presets.completed!=saveTicket)selectedPreset=presets.lastId;}
        const morphpreset::Choice* chosen=nullptr;
        for(const auto& choice:*presets.choices)if(choice.id==selectedPreset)chosen=&choice;
        if(!chosen)selectedPreset.clear();
        const auto available=std::count_if(presets.choices->begin(),presets.choices->end(),[](const auto& choice){return choice.compatible;});
        const auto preview=chosen?chosen->name+(chosen->compatible?"":"（当前不可用）"):std::string(available?"选择可用预设":"当前装备没有匹配预设");
        const bool ready=snapshot.ready&&presets.available&&!presetWaiting;
        bool owned=false;for(uint32_t i=0;i<snapshot.data.keyCount;i++)owned=owned||snapshot.data.keys[i].owned!=0;
        std::optional<morphpreset::Request> intent;
        ImGui::SeparatorText("整套预设");
        ImGui::TextUnformatted("浏览预设");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##presets",preview.c_str())){
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##preset-search","搜索可用预设名称",presetSearch,sizeof(presetSearch));
            unsigned shown=0;
            for(const auto& choice:*presets.choices){if(!morphpreset::Visible(choice,presetSearch,morphpreset::Filter::Complete))continue;shown++;ImGui::PushID(choice.id.c_str());
                if(ImGui::Selectable("##preset",choice.id==selectedPreset)){selectedPreset=choice.id;chosen=&choice;presetError.clear();}
                const auto position=ImGui::GetItemRectMin();ui::TextAt(position,ImGui::GetItemRectMax().x-position.x,choice.name.c_str(),ImGui::GetColorU32(ImGuiCol_Text));
                if(ImGui::IsItemHovered())ui::Tooltip((choice.name+"\n"+choice.status).c_str());ImGui::PopID();
            }
            if(!shown)ImGui::TextWrapped(available?"没有符合名称的预设。":"当前装备没有匹配预设。");
            ImGui::Separator();if(ImGui::Button("管理全部预设")){browser.open=true;ImGui::CloseCurrentPopup();}ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!ready||!chosen||!chosen->compatible);
        if(ImGui::Button("应用所选")){intent=morphpreset::Request{morphpreset::Action::Apply,selectedPreset,{}};edits={};}ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip(!ready?"等待核心与装备就绪。":!chosen?"先从下拉框选择可用预设。":!chosen->compatible?chosen->status.c_str():"应用正在浏览的预设，替换本面板全部贡献。仅选择不改变权重。");
        ui::NextButton("保存当前");ImGui::BeginDisabled(!ready||presets.activeId.empty()||!owned);
        if(ImGui::Button("保存当前"))intent=morphpreset::Request{morphpreset::Action::Save,presets.activeId,{}};ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip(presets.activeId.empty()?"当前配置尚未命名，请先另存为新预设。":!owned?"请先接管形态键，再保存当前配置。":"将当前接管的目标权重保存到已应用的预设；与正在浏览哪一项无关。");
        ui::NextButton("另存为…");ImGui::BeginDisabled(!ready||!owned);
        if(ImGui::Button("另存为…")){presetName[0]=0;presetError.clear();ImGui::OpenPopup("另存为整套预设");}ImGui::EndDisabled();
        ui::NextButton("管理…");if(ImGui::Button("管理…"))browser.open=true;
        if(!presets.available)ui::Line("预设需要配套核心提供 MOD 身份信息；普通调节仍可使用。",ui::Tone::Warning);
        else if(chosen)ui::Line((std::string("浏览匹配 · ")+chosen->status).c_str(),chosen->compatible?ui::Tone::Muted:ui::Tone::Warning);
        else ui::Line("选择只用于浏览；点击“应用所选”才会改变形态键。",ui::Tone::Muted);
        const std::string name=presets.activeId.empty()?(owned?"未命名配置":"尚未接管形态键"):presets.activeName;
        const char* state=!owned?"未接管／已释放":presets.dirty?"未保存":"已保存";
        ui::Line((std::string("当前配置 · ")+state+" · "+name).c_str(),owned&&presets.dirty?ui::Tone::Warning:owned?ui::Tone::Success:ui::Tone::Muted);
        if(chosen&&chosen->id!=presets.activeId)ui::Line(!presets.activeId.empty()&&owned?"正在浏览另一份预设；当前形变保持，保存当前仍更新当前配置。":"尚未应用此预设；浏览不会接管形态键。",ui::Tone::Muted);
        ui::Notice(presets,presetWaiting,presetError);
        if(ImGui::BeginPopupModal("另存为整套预设",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted("保存本面板已接管的所有部位，包含零权重。");
            ImGui::SetNextItemWidth(320);const bool enter=ImGui::InputText("名称",presetName,sizeof(presetName),ImGuiInputTextFlags_EnterReturnsTrue);
            const bool save=ImGui::Button("保存新预设");ImGui::SameLine();const bool cancel=ImGui::Button("取消");
            if(save||enter){
                if(!morphpreset::Utf8(presetName))presetError="请输入有效名称（最多 128 个 UTF-8 字节）";
                else{intent=morphpreset::Request{morphpreset::Action::SaveAs,{},presetName};ImGui::CloseCurrentPopup();}
            }
            if(cancel)ImGui::CloseCurrentPopup();if(!presetError.empty())ImGui::TextWrapped("%s",presetError.c_str());ImGui::EndPopup();
        }
        return intent;
    }
    void FinishPreset(Control& control,const std::optional<morphpreset::Request>& intent){
        if(intent){presetError.clear();presetWaiting=control.PostPreset(*intent);if(intent->action==morphpreset::Action::Save)saveTicket=presetWaiting;if(!presetWaiting)presetError="预设请求过多，请稍后重试";}
    }
    void Draw(Control& control){
        const auto snapshot=control.Snapshot();const auto& data=snapshot->data;
        const auto preset=PresetBar(*snapshot);
        browser.Draw(control,*snapshot,presetWaiting);
        ImGui::SeparatorText("装备与形态键");
        if(!snapshot->connected){ui::Wrapped(ResultText(snapshot->scan),ui::Tone::Warning);FinishPreset(control,preset);return;}
        if(!snapshot->ready){ui::Wrapped(ResultText(snapshot->scan),ui::Tone::Warning);FinishPreset(control,preset);return;}
        const mhmcontrol::Target* target=nullptr;
        for(uint32_t i=0;i<data.targetCount;i++)if(data.targets[i].info.handle==selected)target=&data.targets[i];
        if(!target){selected=data.targetCount?data.targets[0].info.handle:0;edits={};if(data.targetCount)target=&data.targets[0];}
        const auto label=target?Label(*target):std::string("当前没有已绑定的形态键装备");
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##equipment",label.c_str())){
            for(uint32_t i=0;i<data.targetCount;i++){
                const auto& item=data.targets[i];const bool chosen=item.info.handle==selected;
                ImGui::PushID(static_cast<int>(i));
                if(ImGui::Selectable(Label(item).c_str(),chosen)){selected=item.info.handle;edits={};target=&item;}
                if(chosen)ImGui::SetItemDefaultFocus();ImGui::PopID();
            }ImGui::EndCombo();
        }
        if(ImGui::IsItemHovered())ui::Tooltip(label.c_str());
        ui::Wrapped("勾选表示本面板接管，取消勾选释放单键；精确输入按回车应用。",ui::Tone::Muted);
        const bool release=ImGui::Button("释放全部控制");
        ui::NextButton("操作提示");ui::Line("Ctrl + Shift + M 隐藏保留权重；× 释放并关闭。",ui::Tone::Muted);
        if(snapshot->operation!=MHM_OK)ui::Wrapped(ResultText(snapshot->operation),ui::Tone::Error);
        if(!target){if(release)control.Post(mhmcontrol::Action::Release);else FinishPreset(control,preset);return;}
        if(ImGui::BeginTable("shape-keys",4,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,ImVec2(0,-1))){
            ImGui::TableSetupColumn("形态键",ImGuiTableColumnFlags_WidthStretch,1.3f);
            ImGui::TableSetupColumn("目标权重",ImGuiTableColumnFlags_WidthStretch,1.8f);
            ImGui::TableSetupColumn("精确输入",ImGuiTableColumnFlags_WidthFixed,120);
            ImGui::TableSetupColumn("实际权重",ImGuiTableColumnFlags_WidthFixed,90);ImGui::TableHeadersRow();
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(target->info.handle)));
            for(uint32_t i=0;i<target->count;i++){
                const auto& key=data.keys[target->first+i];auto& edit=edits[i];
                if(!edit.active&&snapshot->completed>=edit.ticket)edit.value=key.owned?key.contribution:key.state.requested;
                ImGui::PushID(static_cast<int>(key.index));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                bool owned=key.owned!=0;
                if(ImGui::Checkbox("##owned",&owned))edit.ticket=control.Post(owned?mhmcontrol::Action::Set:mhmcontrol::Action::Drop,key.target,key.index,edit.value);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s：勾选时保留本面板贡献，零权重也可接管；取消仅释放本键。",key.info.id);
                ImGui::SameLine();ImGui::SetCursorPosY(ImGui::GetCursorPosY()+ImGui::GetStyle().FramePadding.y);ui::Line(key.info.id);
                ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderFloat("##slider",&edit.value,key.info.minimum,key.info.maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoRoundToFormat|ImGuiSliderFlags_NoInput))
                    edit.ticket=control.Post(mhmcontrol::Action::Set,key.target,key.index,edit.value);
                bool active=ImGui::IsItemActive();
                ImGui::TableSetColumnIndex(2);ImGui::SetNextItemWidth(-1);
                const bool entered=ImGui::InputFloat("##weight",&edit.value,0,0,"%.9g",ImGuiInputTextFlags_EnterReturnsTrue);
                if(entered)edit.ticket=control.Post(mhmcontrol::Action::Set,key.target,key.index,edit.value);
                edit.active=active||ImGui::IsItemActive();
                ImGui::TableSetColumnIndex(3);ImGui::Text("%.3f",key.state.applied);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("请求：%.9g\n已应用：%.9g\n%s",key.state.requested,key.state.applied,key.owned&&key.state.source!=data.source?"其他来源优先":"");
                ImGui::PopID();
            }ImGui::PopID();ImGui::EndTable();
        }
        if(release)control.Post(mhmcontrol::Action::Release);else FinishPreset(control,preset);
    }
};
}
