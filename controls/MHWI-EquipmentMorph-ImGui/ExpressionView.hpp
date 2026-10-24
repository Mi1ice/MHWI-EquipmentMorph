#pragma once
#include "Ui.hpp"

namespace morphgui {
struct ExpressionView {
    uint64_t selected=0,waiting=0;
    uint32_t key=0;
    bool initialized=false;
    static bool Eligible(const MhmKeyInfo& info){return info.minimum<=0&&info.maximum>=1;}
    void Draw(Control& control){
        const auto snapshot=control.Snapshot();const auto& data=snapshot->data;const auto& remote=snapshot->expression;const auto& state=remote.state;
        if(waiting&&snapshot->completed>=waiting)waiting=0;
        if(!remote.connected){
            ui::Wrapped(remote.result==MHM_VERSION?"自动表情插件版本不兼容，请更新独立眨眼插件。":"自动表情插件未就绪，请确认独立眨眼插件已安装并正常运行。",ui::Tone::Warning);return;
        }
        const bool busy=waiting||remote.busy;
        ImGui::BeginDisabled(busy||!state.ready);bool enabled=state.config.enabled!=0;
        if(ImGui::Checkbox("启用自动表情",&enabled))waiting=control.PostExpression({morphexpression::Action::Enable,enabled});
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip("独立于形态键控制；设置保存后在重启游戏时恢复。关闭面板不会停止自动表情。");
        ImGui::SeparatorText("眨眼");
        if(snapshot->ready&&selected){bool exists=false;for(uint32_t i=0;i<data.targetCount;++i)exists=exists||data.targets[i].info.handle==selected;
            if(!exists){selected=0;initialized=false;}}
        const mhmcontrol::Target* target=nullptr;
        for(uint32_t i=0;i<data.targetCount&&snapshot->ready;++i){
            const auto& item=data.targets[i];
            if(!initialized&&state.config.mapped&&item.info.part==state.config.part&&!std::strcmp(item.info.model_id,state.config.model)){
                selected=item.info.handle;key=0;for(uint32_t k=0;k<item.count;++k)if(!std::strcmp(data.keys[item.first+k].info.id,state.config.key))key=k;initialized=true;
            }
            if(item.info.handle==selected)target=&item;
        }
        const auto label=target?std::string(morphpreset::PartName(target->info.part))+" · "+morphequipment::Label(target->info.model_id):std::string("选择装备");
        ImGui::BeginDisabled(busy||!state.ready);ImGui::BeginDisabled(!snapshot->ready);
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("装备");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##expression-equipment",label.c_str())){
            for(uint32_t i=0;i<data.targetCount;++i){const auto& item=data.targets[i];
                const auto name=std::string(morphpreset::PartName(item.info.part))+" · "+morphequipment::Label(item.info.model_id);
                ImGui::PushID(static_cast<int>(i));if(ImGui::Selectable(name.c_str(),selected==item.info.handle)){selected=item.info.handle;key=0;target=&item;initialized=true;
                    for(uint32_t k=0;k<item.count;++k)if(!std::strcmp(data.keys[item.first+k].info.id,"vrc.blink"))key=k;}
                ImGui::PopID();
            }ImGui::EndCombo();
        }
        if(target&&ImGui::IsItemHovered())ui::Tooltip(target->info.model_id);
        const mhmcontrol::Key* chosen=target&&key<target->count?&data.keys[target->first+key]:nullptr;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("目标形态键");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##expression-key",chosen?chosen->info.id:"选择合并双眼的眨眼形态键")){
            unsigned shown=0;
            if(target)for(uint32_t k=0;k<target->count;++k){const auto& item=data.keys[target->first+k];if(!Eligible(item.info))continue;++shown;
                ImGui::PushID(static_cast<int>(k));if(ImGui::Selectable("##key",key==k))key=k;
                const auto pos=ImGui::GetItemRectMin();ui::TextAt(pos,ImGui::GetItemRectMax().x-pos.x,item.info.id,ImGui::GetColorU32(ImGuiCol_Text));ImGui::PopID();
            }
            if(!shown)ui::Wrapped("没有支持 0～1 权重的形态键。",ui::Tone::Muted);ImGui::EndCombo();
        }
        chosen=target&&key<target->count?&data.keys[target->first+key]:nullptr;
        if(ImGui::IsItemHovered())ui::Tooltip("当前为一条合并双眼的眨眼映射。请选择表达双眼闭合的形态键；不在这里配置视线或口型。");
        ImGui::BeginDisabled(!target||!chosen||!Eligible(chosen->info));
        if(ImGui::Button("保存映射"))waiting=control.PostExpression({morphexpression::Action::Map,false,selected,key});ImGui::EndDisabled();ImGui::EndDisabled();
        ui::NextButton("清除映射");ImGui::BeginDisabled(!state.config.mapped);
        if(ImGui::Button("清除映射"))waiting=control.PostExpression({morphexpression::Action::Clear});ImGui::EndDisabled();ImGui::EndDisabled();
        if(state.config.mapped){
            const auto saved=std::string(morphpreset::PartName(state.config.part))+" · "+morphequipment::Label(state.config.model)+" → "+state.config.key;
            ui::Line(saved.c_str(),ui::Tone::Muted);
            if(ImGui::IsItemHovered())ui::Tooltip("已保存的映射。按装备资源路径、模型摘要及形态键包摘要匹配；更换 MOD 后需要重新保存映射。");
        }
        using Status=morphexpression::Status;
        const char* status="等待插件初始化";
        switch(state.status){
        case Status::Disabled:status="已关闭";break;
        case Status::WaitingTarget:status=state.config.mapped?"等待匹配装备":"未配置映射";break;
        case Status::Tracking:status=state.source&&state.winner&&state.source!=state.winner?"手动或其他高优先来源接管":"运行中";break;
        case Status::WaitingSource:status="等待角色面部就绪";break;
        case Status::Inconsistent:status="等待一致的面部姿态";break;
        case Status::Unsupported:status="当前面部来源暂不支持";break;
        case Status::ApiError:status="接口异常，自动控制已暂停";break;
        default:break;
        }
        ImGui::Separator();ui::Line(status,state.status==Status::Tracking?ui::Tone::Success:ui::Tone::Muted);
        if(ImGui::IsItemHovered()&&state.detail[0])ui::Tooltip(state.detail);
        if(state.status==Status::Tracking)ImGui::Text("闭合程度  %.3f",state.closure);
        if(busy)ui::Line("正在保存…",ui::Tone::Muted);
        else if(remote.result!=MHM_OK)ui::Wrapped(remote.result==MHM_STALE?"装备已变化，请重新选择后保存。":remote.result==MHM_INVALID?"自动表情配置无效，请重新选择并保存映射。":"配置未保存，请检查目录权限或稍后重试。",ui::Tone::Error);
    }
};
}
