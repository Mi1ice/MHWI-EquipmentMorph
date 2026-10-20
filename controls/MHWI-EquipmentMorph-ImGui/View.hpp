#pragma once
#include "Control.hpp"
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
    std::string selectedPreset,presetError;char presetName[129]{};uint64_t presetSeen=0,presetWaiting=0;
    struct Edit {float value=0;uint64_t ticket=0;bool active=false;};
    std::array<Edit,64> edits{};
    static const char* Part(uint32_t part){const char* names[]={"头部","身体","腕部","腰部","腿部"};return part<5?names[part]:"装备";}
    static std::string Label(const mhmcontrol::Target& target){return std::string(Part(target.info.part))+" · "+target.info.model_id;}
    std::optional<morphpreset::Request> PresetBar(const ControlSnapshot& snapshot){
        const auto& presets=snapshot.presets;
        if(presetWaiting&&snapshot.completed>=presetWaiting)presetWaiting=0;
        if(presets.completed!=presetSeen){presetSeen=presets.completed;if(presets.success)selectedPreset=presets.lastId;}
        const morphpreset::Choice* chosen=nullptr;
        for(const auto& choice:*presets.choices)if(choice.id==selectedPreset)chosen=&choice;
        if(!chosen&&!presets.choices->empty()){chosen=&presets.choices->front();selectedPreset=chosen->id;}
        const bool ready=snapshot.ready&&presets.available&&!presetWaiting;
        bool owned=false;for(uint32_t i=0;i<snapshot.data.keyCount;i++)owned=owned||snapshot.data.keys[i].owned!=0;
        std::optional<morphpreset::Request> intent;
        ImGui::TextUnformatted("整套预设");ImGui::SameLine();
        const float buttons=ImGui::CalcTextSize("应用保存另存为…").x+ImGui::GetStyle().FramePadding.x*6+ImGui::GetStyle().ItemSpacing.x*3;
        ImGui::SetNextItemWidth((std::max)(120.f,ImGui::GetContentRegionAvail().x-buttons));
        if(ImGui::BeginCombo("##presets",chosen?chosen->name.c_str():"尚无预设")){
            for(const auto& choice:*presets.choices){ImGui::PushID(choice.id.c_str());
                const auto label=choice.name+(choice.compatible?"":"（不匹配）");
                if(ImGui::Selectable(label.c_str(),choice.id==selectedPreset)){selectedPreset=choice.id;chosen=&choice;presetError.clear();}
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",choice.status.c_str());ImGui::PopID();
            }ImGui::EndCombo();
        }
        ImGui::SameLine();ImGui::BeginDisabled(!ready||!chosen||!chosen->compatible);
        if(ImGui::Button("应用")){intent=morphpreset::Request{morphpreset::Action::Apply,selectedPreset,{}};edits={};}ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(!ready||presets.activeId.empty()||selectedPreset!=presets.activeId||!owned);
        if(ImGui::Button("保存"))intent=morphpreset::Request{morphpreset::Action::Save,presets.activeId,{}};ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(!ready||!owned);
        if(ImGui::Button("另存为…")){presetName[0]=0;presetError.clear();ImGui::OpenPopup("另存为整套预设");}ImGui::EndDisabled();
        if(!presets.available)ImGui::TextWrapped("预设需要配套核心提供 MOD 身份信息；普通形态键调节仍可使用。");
        else if(chosen)ImGui::TextWrapped("%s",chosen->status.c_str());
        else ImGui::TextWrapped("调节并接管形态键后，可将多个部位一起另存为预设。");
        if(!presets.activeId.empty())ImGui::TextWrapped("当前预设：%s%s",presets.activeName.c_str(),presets.dirty?" · 已修改，尚未保存":"");
        if(presetWaiting)ImGui::TextUnformatted("正在处理预设…");
        else if(!presets.message.empty())ImGui::TextWrapped("%s",presets.message.c_str());
        if(!presetError.empty())ImGui::TextWrapped("%s",presetError.c_str());
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
        ImGui::Separator();return intent;
    }
    void FinishPreset(Control& control,const std::optional<morphpreset::Request>& intent){
        if(intent){presetError.clear();presetWaiting=control.PostPreset(*intent);if(!presetWaiting)presetError="预设请求过多，请稍后重试";}
    }
    void Draw(Control& control){
        const auto snapshot=control.Snapshot();const auto& data=snapshot->data;
        const auto preset=PresetBar(*snapshot);
        ImGui::TextUnformatted("Ctrl + Shift + M 隐藏并保留权重；× 释放本面板控制。");
        if(!snapshot->connected){ImGui::TextUnformatted(ResultText(snapshot->scan));return;}
        if(!snapshot->ready){ImGui::TextUnformatted(ResultText(snapshot->scan));return;}
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
        ImGui::TextDisabled("修改即接管；取消勾选释放单键。输入值按回车应用。");
        const bool release=ImGui::Button("释放全部控制");
        if(snapshot->operation!=MHM_OK){ImGui::SameLine();ImGui::TextUnformatted(ResultText(snapshot->operation));}
        if(!target){if(release)control.Post(mhmcontrol::Action::Release);else FinishPreset(control,preset);return;}
        ImGui::TextDisabled("%s",target->info.model_id);
        if(ImGui::BeginTable("shape-keys",4,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,ImVec2(0,-1))){
            ImGui::TableSetupColumn("形态键名",ImGuiTableColumnFlags_WidthStretch,1.3f);
            ImGui::TableSetupColumn("权重滑块",ImGuiTableColumnFlags_WidthStretch,1.8f);
            ImGui::TableSetupColumn("权重输入框",ImGuiTableColumnFlags_WidthFixed,125);
            ImGui::TableSetupColumn("当前权重值",ImGuiTableColumnFlags_WidthFixed,115);ImGui::TableHeadersRow();
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(target->info.handle)));
            for(uint32_t i=0;i<target->count;i++){
                const auto& key=data.keys[target->first+i];auto& edit=edits[i];
                if(!edit.active&&snapshot->completed>=edit.ticket)edit.value=key.owned?key.contribution:key.state.requested;
                ImGui::PushID(static_cast<int>(key.index));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                bool owned=key.owned!=0;
                if(ImGui::Checkbox("##owned",&owned))edit.ticket=control.Post(owned?mhmcontrol::Action::Set:mhmcontrol::Action::Drop,key.target,key.index,edit.value);
                ImGui::SameLine();ImGui::TextUnformatted(key.info.id);
                ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderFloat("##slider",&edit.value,key.info.minimum,key.info.maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoRoundToFormat|ImGuiSliderFlags_NoInput))
                    edit.ticket=control.Post(mhmcontrol::Action::Set,key.target,key.index,edit.value);
                bool active=ImGui::IsItemActive();
                ImGui::TableSetColumnIndex(2);ImGui::SetNextItemWidth(-1);
                const bool entered=ImGui::InputFloat("##weight",&edit.value,0,0,"%.9g",ImGuiInputTextFlags_EnterReturnsTrue);
                if(entered)edit.ticket=control.Post(mhmcontrol::Action::Set,key.target,key.index,edit.value);
                edit.active=active||ImGui::IsItemActive();
                ImGui::TableSetColumnIndex(3);ImGui::Text("%.6g",key.state.applied);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("请求：%.9g\n已应用：%.9g\n%s",key.state.requested,key.state.applied,key.owned&&key.state.source!=data.source?"其他来源优先":"");
                ImGui::PopID();
            }ImGui::PopID();ImGui::EndTable();
        }
        if(release)control.Post(mhmcontrol::Action::Release);else FinishPreset(control,preset);
    }
};
}
