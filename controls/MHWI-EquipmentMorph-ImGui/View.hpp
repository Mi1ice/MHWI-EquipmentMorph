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
    std::string selectedPreset,presetError;char presetName[129]{},presetSearch[129]{};uint64_t presetWaiting=0;
    PresetBrowser browser;
    morphpreset::Action pendingAction=morphpreset::Action::Select;std::string pendingId,pendingName,pendingEditor;
    struct Edit {float value=0;uint64_t ticket=0;bool active=false;};
    std::array<Edit,64> edits{};
    static const char* Part(uint32_t part){const char* names[]={"头部","身体","腕部","腰部","腿部"};return part<5?names[part]:"装备";}
    static std::string Label(const mhmcontrol::Target& target){return std::string(Part(target.info.part))+" · "+target.info.model_id;}
    std::optional<morphpreset::Request> PresetBar(const ControlSnapshot& snapshot){
        const auto& presets=snapshot.presets;
        if(presetWaiting&&snapshot.completed>=presetWaiting)presetWaiting=0;
        selectedPreset=presets.activeId;
        const morphpreset::Choice* chosen=nullptr;for(const auto& choice:*presets.choices)if(choice.id==selectedPreset)chosen=&choice;
        const auto available=std::count_if(presets.choices->begin(),presets.choices->end(),[](const auto& choice){return choice.matched>0;});
        const auto preview=chosen?chosen->name+(chosen->matched?"":"（等待匹配装备）"):std::string(available?"选择预设，立即应用匹配部位":"当前没有匹配的预设");
        const bool ready=snapshot.ready&&presets.available&&!presetWaiting;
        bool owned=false;for(uint32_t i=0;i<snapshot.data.keyCount;i++)owned=owned||snapshot.data.keys[i].owned!=0;
        std::optional<morphpreset::Request> intent;bool ask=false;
        ImGui::SeparatorText("预设与自动使用");ImGui::TextUnformatted("当前预设");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(!ready);
        if(ImGui::BeginCombo("##presets",preview.c_str())){
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##preset-search","按名称搜索匹配预设",presetSearch,sizeof(presetSearch));unsigned shown=0;
            for(const auto& choice:*presets.choices){
                if(!choice.matched||!morphpreset::Visible(choice,presetSearch,morphpreset::Filter::All))continue;shown++;ImGui::PushID(choice.id.c_str());
                if(ImGui::Selectable("##preset",choice.id==selectedPreset)){
                    presetError.clear();
                    if(presets.dirty){pendingAction=morphpreset::Action::Select;pendingId=choice.id;pendingName=choice.name;pendingEditor=presets.activeId;ask=true;}
                    else{morphpreset::Request request{morphpreset::Action::Select,choice.id};request.editor=presets.activeId;request.checkEditor=true;intent=std::move(request);}
                }
                const auto position=ImGui::GetItemRectMin();ui::TextAt(position,ImGui::GetItemRectMax().x-position.x,choice.name.c_str(),ImGui::GetColorU32(ImGuiCol_Text));
                if(ImGui::IsItemHovered())ui::Tooltip((choice.name+"\n"+choice.status+"\n选中后立即应用匹配部位；其他部位保持。").c_str());ImGui::PopID();
            }
            if(!shown)ui::Wrapped(available?"没有符合搜索条件的预设。":"没有匹配部位。可在管理中查看全部预设。",ui::Tone::Muted);
            ImGui::Separator();if(ImGui::Button("管理全部预设")){browser.open=true;ImGui::CloseCurrentPopup();}ImGui::EndCombo();
        }ImGui::EndDisabled();
        ImGui::BeginDisabled(!ready||!presets.canSave);
        if(ImGui::Button("保存当前"))intent=morphpreset::Request{morphpreset::Action::Save,presets.activeId};ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip("手动保存当前预设的编辑。其他自动预设控制的部位不会混入；未穿戴部位的已保存内容保留。");
        ui::NextButton("另存为…");ImGui::BeginDisabled(!ready||!owned);
        if(ImGui::Button("另存为…")){presetName[0]=0;presetError.clear();ImGui::OpenPopup("另存为整套预设");}ImGui::EndDisabled();
        ui::NextButton("管理…");if(ImGui::Button("管理…"))browser.open=true;
        const auto name=presets.activeId.empty()?(owned?std::string("组合控制；选择预设后编辑，或另存为"):std::string("尚未接管形态键")):presets.activeName;
        const char* status=presets.paused?"自动已暂停":presets.dirty?"未保存":presets.canSave?"已保存":"等待匹配部位";
        ui::Line((std::string("当前编辑 · ")+status+" · "+name).c_str(),presets.dirty||presets.paused?ui::Tone::Warning:ui::Tone::Normal);
        if(chosen)ui::Line(chosen->status.c_str(),chosen->matched?ui::Tone::Muted:ui::Tone::Warning);
        const auto registered=morphpreset::DefaultParts(*presets.defaults,presets.activeId);
        ImGui::BeginDisabled(presetWaiting||presets.activeId.empty());
        if(ImGui::Button("设为穿戴默认"))intent=morphpreset::Request{morphpreset::Action::SetDefault,presets.activeId};ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip("记录此预设涉及部位的自动关联；相同模型的旧关联被替换。启动／换装恢复已保存版本，不自动保存当前修改。");
        ui::NextButton("取消默认");ImGui::BeginDisabled(presetWaiting||registered.empty());
        if(ImGui::Button("取消默认"))intent=morphpreset::Request{morphpreset::Action::ClearDefault,presets.activeId};ImGui::EndDisabled();
        ui::Line((registered.empty()?std::string("此预设尚未设为穿戴默认"):"穿戴默认部位 · "+registered).c_str(),ui::Tone::Muted);
        ImGui::BeginDisabled(presetWaiting!=0);
        if(ImGui::Button(presets.paused?"恢复自动使用":!presets.autoMessage.empty()?"重试自动使用":"暂时还原")){
            if(presets.paused||!presets.autoMessage.empty())intent=morphpreset::Request{morphpreset::Action::Resume};
            else if(presets.dirty){pendingAction=morphpreset::Action::Pause;pendingId.clear();pendingName.clear();pendingEditor=presets.activeId;ask=true;}
            else intent=morphpreset::Request{morphpreset::Action::Pause};
        }ImGui::EndDisabled();
        if(!presets.autoMessage.empty())ui::Line(presets.autoMessage.c_str(),ui::Tone::Warning);
        else ui::Line(presets.paused?"本次游戏自动使用已暂停；重启后按穿戴默认恢复。":presets.defaults->empty()?"未设置穿戴默认；窗口隐藏不会改变当前形变。":"自动使用已启用；启动／换装按部位匹配，读取已保存版本。",presets.paused?ui::Tone::Warning:ui::Tone::Muted);
        if(ImGui::TreeNode("各部位当前配置")){bool any=false;for(const auto& line:presets.worn)if(!line.empty()){ui::Line(line.c_str());any=true;}if(!any)ui::Wrapped("当前没有接管的部位。",ui::Tone::Muted);ImGui::TreePop();}
        ui::Notice(presets,presetWaiting,presetError);
        if(ask)ImGui::OpenPopup("未保存的修改");
        if(ImGui::BeginPopupModal("未保存的修改",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+400);
            ui::Wrapped(pendingAction==morphpreset::Action::Select?("切换到“"+pendingName+"”前，如何处理当前未保存修改？").c_str():"暂时还原前，如何处理当前未保存修改？");
            if(pendingEditor.empty())ui::Wrapped("当前配置尚未命名，可取消后另存为。",ui::Tone::Warning);
            ImGui::PopTextWrapPos();ImGui::BeginDisabled(presetWaiting||pendingEditor.empty()||!presets.canSave);
            if(ImGui::Button(pendingAction==morphpreset::Action::Select?"保存并切换":"保存并还原")){
                morphpreset::Request request{pendingAction,pendingId};request.saveBefore=true;request.editor=pendingEditor;request.checkEditor=true;intent=std::move(request);ImGui::CloseCurrentPopup();
            }ImGui::EndDisabled();ui::NextButton("放弃修改");ImGui::BeginDisabled(presetWaiting!=0);
            if(ImGui::Button(pendingAction==morphpreset::Action::Select?"放弃并切换":"放弃并还原")){
                morphpreset::Request request{pendingAction,pendingId};request.discard=true;request.editor=pendingEditor;request.checkEditor=true;intent=std::move(request);ImGui::CloseCurrentPopup();
            }ImGui::EndDisabled();ui::NextButton("取消");if(ImGui::Button("取消"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("另存为整套预设",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ui::Wrapped("将本面板当前接管的全部部位另存为，包含零权重；不会自动设为穿戴默认。");
            ImGui::SetNextItemWidth(320);const bool enter=ImGui::InputText("名称",presetName,sizeof(presetName),ImGuiInputTextFlags_EnterReturnsTrue);
            const bool save=ImGui::Button("保存新预设");ImGui::SameLine();const bool cancel=ImGui::Button("取消");
            if(save||enter){if(!morphpreset::Utf8(presetName))presetError="请输入有效名称（最多 128 个 UTF-8 字节）";else{intent=morphpreset::Request{morphpreset::Action::SaveAs,{},presetName};ImGui::CloseCurrentPopup();}}
            if(cancel)ImGui::CloseCurrentPopup();if(!presetError.empty())ui::Wrapped(presetError.c_str(),ui::Tone::Error);ImGui::EndPopup();
        }return intent;
    }

    void FinishPreset(Control& control,const std::optional<morphpreset::Request>& intent){
        if(intent){presetError.clear();presetWaiting=control.PostPreset(*intent);if(intent->action==morphpreset::Action::Select)edits={};if(!presetWaiting)presetError="预设请求过多，请稍后重试";}
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
        ui::Line("Ctrl + Shift + M 或 × 隐藏窗口并保留形变。",ui::Tone::Muted);
        if(target&&!snapshot->presets.worn[target->info.part].empty())ui::Line(snapshot->presets.worn[target->info.part].c_str());
        if(snapshot->operation!=MHM_OK)ui::Wrapped(ResultText(snapshot->operation),ui::Tone::Error);
        if(!target){FinishPreset(control,preset);return;}
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
        FinishPreset(control,preset);
    }
};
}
