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
    PresetBrowser browser;KeyFilter keys;ui::Notice notice;
    bool closeRequested=false,hideReady=false;uint64_t closeTicket=0;
    morphpreset::Action pendingAction=morphpreset::Action::Select;std::string pendingId,pendingName,pendingEditor;
    struct Edit {float value=0;uint64_t ticket=0;bool active=false;};
    std::array<Edit,64> edits{};
    static const char* Part(uint32_t part){const char* names[]={"头部","身体","腕部","腰部","腿部"};return part<5?names[part]:"装备";}
    static std::string Label(const mhmcontrol::Target& target){return std::string(Part(target.info.part))+" · "+morphequipment::Label(target.info.model_id);}
    std::optional<morphpreset::Request> PresetBar(const ControlSnapshot& snapshot){
        const auto& presets=snapshot.presets;
        if(presetWaiting&&snapshot.completed>=presetWaiting)presetWaiting=0;
        selectedPreset=presets.activeId;
        const morphpreset::Choice* chosen=nullptr;for(const auto& choice:*presets.choices)if(choice.id==selectedPreset)chosen=&choice;
        const auto available=std::count_if(presets.choices->begin(),presets.choices->end(),[](const auto& choice){return choice.matched>0;});
        const auto preview=chosen?chosen->name+(presets.dirty?"*":"")+(chosen->matched?"":"（等待匹配装备）"):std::string(presets.dirty?"未命名*":available?"选择预设":"无匹配预设");
        const bool ready=snapshot.ready&&presets.available&&presets.enabled&&!presetWaiting;
        bool owned=false;for(uint32_t i=0;i<snapshot.data.keyCount;i++)owned=owned||snapshot.data.keys[i].owned!=0;
        std::optional<morphpreset::Request> intent;bool ask=false;
        const auto& style=ImGui::GetStyle();
        const float manageWidth=ui::ButtonWidth("收起管理");
        const float tools=ui::ButtonWidth("保存当前")+ui::ButtonWidth("另存为…")+manageWidth+ImGui::CalcTextSize("启用捏人").x+ImGui::GetFrameHeight()+style.ItemInnerSpacing.x+style.ItemSpacing.x*4;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("预览预设");ImGui::SameLine();
        const bool inlineTools=ImGui::GetContentRegionAvail().x>=tools+180.f;
        ImGui::SetNextItemWidth(inlineTools?ImGui::GetContentRegionAvail().x-tools:-1.f);
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
                if(ImGui::IsItemHovered())ui::Tooltip((choice.name+"\n"+choice.status+"\n选中后立即预览匹配部位；关闭主窗口恢复穿戴默认。").c_str());ImGui::PopID();
            }
            if(!shown)ui::Wrapped(available?"没有符合搜索条件的预设。":"没有匹配部位。可在管理中查看全部预设。",ui::Tone::Muted);
            ImGui::Separator();if(ImGui::Button("管理全部预设")){browser.open=true;ImGui::CloseCurrentPopup();}ImGui::EndCombo();
        }ImGui::EndDisabled();
        if(inlineTools)ImGui::SameLine();
        if(presets.dirty)ImGui::PushStyleColor(ImGuiCol_Button,style.Colors[ImGuiCol_ButtonActive]);
        ImGui::BeginDisabled(!ready||!presets.canSave);
        if(ImGui::Button("保存当前"))intent=morphpreset::Request{morphpreset::Action::Save,presets.activeId};ImGui::EndDisabled();if(presets.dirty)ImGui::PopStyleColor();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip("手动保存当前预设的编辑。其他自动预设控制的部位不会混入；未穿戴部位的已保存内容保留。");
        ui::NextButton("另存为…");ImGui::BeginDisabled(!ready||!owned);
        if(ImGui::Button("另存为…")){presetName[0]=0;presetError.clear();ImGui::OpenPopup("另存为整套预设");}ImGui::EndDisabled();
        ui::NextItem(manageWidth);
        if(ImGui::Button(browser.open?"收起管理###manager-toggle":"管理…###manager-toggle",ImVec2(manageWidth,0)))browser.open=!browser.open;
        if(ImGui::IsItemHovered())ui::Tooltip(browser.open?"收起预设管理侧栏；当前编辑和权重保持。":"打开预设管理侧栏。");
        ui::NextItem(ImGui::CalcTextSize("启用捏人").x+ImGui::GetFrameHeight()+style.ItemInnerSpacing.x);
        ImGui::BeginDisabled(presetWaiting!=0);bool enabled=presets.enabled;
        if(ImGui::Checkbox("启用捏人",&enabled)){
            const bool pending=std::any_of(edits.begin(),edits.end(),[&](const auto& edit){return edit.ticket>snapshot.completed;});
            if(!enabled&&(presets.dirty||pending)){pendingAction=morphpreset::Action::DisableMorph;pendingId.clear();pendingName.clear();pendingEditor=presets.activeId;ask=true;}
            else{morphpreset::Request request{enabled?morphpreset::Action::EnableMorph:morphpreset::Action::DisableMorph};request.editor=presets.activeId;request.checkEditor=!enabled;intent=std::move(request);}
        }ImGui::EndDisabled();

        if(chosen&&!chosen->compatible)ui::Line(chosen->status.c_str(),ui::Tone::Warning);
        const auto registered=morphpreset::DefaultParts(*presets.defaults,presets.activeId);
        ImGui::BeginDisabled(presetWaiting||presets.activeId.empty());
        if(ImGui::Button("应用预设"))intent=morphpreset::Request{morphpreset::Action::SetDefault,presets.activeId};ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ui::Tooltip("为对应装备登记自动使用的预设；使用已保存的权重。");
        ui::NextButton("取消应用");ImGui::BeginDisabled(presetWaiting||registered.empty());
        if(ImGui::Button("取消应用"))intent=morphpreset::Request{morphpreset::Action::ClearDefault,presets.activeId};ImGui::EndDisabled();
        if(!presets.autoMessage.empty()&&presets.enabled){ui::NextButton("重试");ImGui::BeginDisabled(presetWaiting!=0);if(ImGui::Button("重试"))intent=morphpreset::Request{morphpreset::Action::Resume};ImGui::EndDisabled();}
        ImGui::SeparatorText("各部位当前配置");bool any=false;
        for(const auto& line:presets.worn)if(!line.empty()){ui::Wrapped(line.c_str());any=true;}
        if(!any)ui::Line("无形态键装备",ui::Tone::Muted);
        std::string shownError=presetError.empty()?presets.autoMessage:presetError;
        if(shownError.empty()&&snapshot.ready&&presets.enabled&&snapshot.operation!=MHM_OK&&(!presets.completed||presets.success))shownError=ResultText(snapshot.operation);
        if(!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)&&(!browser.open||!shownError.empty()))notice.Draw(presets,presetWaiting,shownError);
        if(closeRequested&&!presetWaiting&&!intent&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)){
            closeRequested=false;presetError.clear();
            const bool pending=std::any_of(edits.begin(),edits.end(),[&](const auto& edit){return edit.ticket>snapshot.completed;});
            if(presets.dirty||pending){pendingAction=morphpreset::Action::EndPreview;pendingId.clear();pendingName.clear();pendingEditor=presets.activeId;ask=true;}
            else{morphpreset::Request request{morphpreset::Action::EndPreview};request.editor=presets.activeId;request.checkEditor=true;intent=std::move(request);}
        }
        if(ask)ImGui::OpenPopup("未保存的修改");
        if(ImGui::BeginPopupModal("未保存的修改",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+400);
            ui::Wrapped(pendingAction==morphpreset::Action::Select?("切换到“"+pendingName+"”前，如何处理未保存修改？").c_str():pendingAction==morphpreset::Action::EndPreview?"关闭预览前，如何处理未保存修改？":"关闭捏人前，如何处理未保存修改？");
            if(pendingEditor.empty())ui::Wrapped("当前配置尚未命名，可取消后另存为。",ui::Tone::Warning);
            ImGui::PopTextWrapPos();ImGui::BeginDisabled(presetWaiting||pendingEditor.empty()||!presets.canSave);
            if(ImGui::Button(pendingAction==morphpreset::Action::Select?"保存并切换":pendingAction==morphpreset::Action::EndPreview?"保存并关闭":"保存并关闭捏人")){
                morphpreset::Request request{pendingAction,pendingId};request.saveBefore=true;request.editor=pendingEditor;request.checkEditor=true;intent=std::move(request);ImGui::CloseCurrentPopup();
            }ImGui::EndDisabled();ui::NextButton("放弃修改");ImGui::BeginDisabled(presetWaiting!=0);
            if(ImGui::Button(pendingAction==morphpreset::Action::Select?"放弃并切换":pendingAction==morphpreset::Action::EndPreview?"放弃并关闭":"放弃并关闭捏人")){
                morphpreset::Request request{pendingAction,pendingId};request.discard=true;request.editor=pendingEditor;request.checkEditor=true;intent=std::move(request);ImGui::CloseCurrentPopup();
            }ImGui::EndDisabled();ui::NextButton("取消");if(ImGui::Button("取消")){closeRequested=false;ImGui::CloseCurrentPopup();}ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("另存为整套预设",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::SetNextItemWidth(320);const bool enter=ImGui::InputText("名称",presetName,sizeof(presetName),ImGuiInputTextFlags_EnterReturnsTrue);
            const bool save=ImGui::Button("保存新预设");ImGui::SameLine();const bool cancel=ImGui::Button("取消");
            if(save||enter){if(!morphpreset::Utf8(presetName))presetError="请输入有效名称（最多 128 个 UTF-8 字节）";else{intent=morphpreset::Request{morphpreset::Action::SaveAs,{},presetName};ImGui::CloseCurrentPopup();}}
            if(cancel)ImGui::CloseCurrentPopup();if(!presetError.empty())ui::Wrapped(presetError.c_str(),ui::Tone::Error);ImGui::EndPopup();
        }return intent;
    }

    void FinishPreset(Control& control,const std::optional<morphpreset::Request>& intent){
        if(intent){presetError.clear();presetWaiting=control.PostPreset(*intent);if(intent->action==morphpreset::Action::EndPreview)closeTicket=presetWaiting;if(intent->action==morphpreset::Action::Select)edits={};if(!presetWaiting)presetError="预设请求过多，请稍后重试";}
    }
    void Draw(Control& control){
        const auto snapshot=control.Snapshot();const auto& data=snapshot->data;
        if(closeTicket){
            if(snapshot->completed<closeTicket){ui::Wrapped("正在结束预览并恢复穿戴默认…",ui::Tone::Muted);return;}
            hideReady=snapshot->presets.completed==closeTicket&&snapshot->presets.success;closeTicket=0;closeRequested=false;
            if(hideReady){edits={};return;}
        }
        const auto preset=PresetBar(*snapshot);
        browser.Draw(control,*snapshot,presetWaiting);
        ImGui::Separator();
        if(!snapshot->connected){ui::Wrapped(ResultText(snapshot->scan),ui::Tone::Warning);FinishPreset(control,preset);return;}
        if(!snapshot->ready){ui::Wrapped(ResultText(snapshot->scan),ui::Tone::Warning);FinishPreset(control,preset);return;}
        const mhmcontrol::Target* target=nullptr;
        for(uint32_t i=0;i<data.targetCount;i++)if(data.targets[i].info.handle==selected)target=&data.targets[i];
        if(!target){selected=data.targetCount?data.targets[0].info.handle:0;edits={};if(data.targetCount)target=&data.targets[0];}
        keys.Select(selected);
        const auto label=target?Label(*target):std::string("当前没有已绑定的形态键装备");
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##equipment",label.c_str())){
            for(uint32_t i=0;i<data.targetCount;i++){
                const auto& item=data.targets[i];const bool chosen=item.info.handle==selected;
                ImGui::PushID(static_cast<int>(i));
                if(ImGui::Selectable(Label(item).c_str(),chosen)){selected=item.info.handle;edits={};keys.Select(selected);target=&item;}
                if(chosen)ImGui::SetItemDefaultFocus();ImGui::PopID();
            }ImGui::EndCombo();
        }
        if(ImGui::IsItemHovered()&&target)ui::Tooltip(target->info.model_id);
        if(!target){FinishPreset(control,preset);return;}
        const float filterWidth=ui::ButtonWidth("非零（本面板）")+ImGui::GetFrameHeight();
        const float availableWidth=ImGui::GetContentRegionAvail().x,countWidth=ImGui::CalcTextSize("显示 64／64").x;
        ImGui::SetNextItemWidth((std::max)(120.f,availableWidth-filterWidth-ui::ButtonWidth("清除")-countWidth-ImGui::GetStyle().ItemSpacing.x*3));
        ImGui::InputTextWithHint("##key-search","搜索形态键",keys.search,sizeof(keys.search));
        ui::NextItem(filterWidth);ImGui::SetNextItemWidth(filterWidth);
        ImGui::Combo("##key-filter",&keys.mode,"全部\0已接管\0未接管\0非零（本面板）\0");
        if(ImGui::IsItemHovered())ui::Tooltip("非零按本面板目标权重筛选；已接管包括零权重，未接管不代表其他插件未控制。");
        ui::NextButton("清除");if(ImGui::Button("清除"))keys.Clear();
        std::array<uint32_t,64> visible{};uint32_t count=0;
        for(uint32_t i=0;i<target->count;i++){
            const auto& key=data.keys[target->first+i];const auto& edit=edits[i];
            if(keys.Visible(key.info.id,key.owned!=0,key.contribution,edit.active,edit.ticket,snapshot->completed))visible[count++]=i;
        }
        ui::NextItem(countWidth);ImGui::AlignTextToFramePadding();ImGui::TextDisabled("显示 %u／%u",count,target->count);
        if(!count){ui::Line("没有符合条件的形态键",ui::Tone::Muted);if(ImGui::Button("清除筛选"))keys.Clear();FinishPreset(control,preset);return;}
        ImGui::BeginDisabled(!snapshot->presets.enabled);
        if(ImGui::BeginTable("shape-keys",2,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,ImVec2(0,-1))){
            ImGui::TableSetupColumn("形态键",ImGuiTableColumnFlags_WidthStretch,1.3f);
            ImGui::TableSetupColumn("权重",ImGuiTableColumnFlags_WidthStretch,1.8f);
            ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(target->info.handle)));
            for(uint32_t row=0;row<count;row++){
                const auto i=visible[row];const auto& key=data.keys[target->first+i];auto& edit=edits[i];
                if(!edit.active&&snapshot->completed>=edit.ticket)edit.value=key.owned?key.contribution:key.state.requested;
                ImGui::PushID(static_cast<int>(key.index));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                bool owned=key.owned!=0;
                if(ImGui::Checkbox("##owned",&owned))edit.ticket=control.Post(owned?mhmcontrol::Action::Set:mhmcontrol::Action::Drop,key.target,key.index,edit.value);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s：勾选时保留本面板贡献，零权重也可接管；取消仅释放本键。",key.info.id);
                ImGui::SameLine();ImGui::SetCursorPosY(ImGui::GetCursorPosY()+ImGui::GetStyle().FramePadding.y);ui::Line(key.info.id);
                ImGui::TableSetColumnIndex(1);
                const float width=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemInnerSpacing.x;
                const bool stacked=width<190.f;
                const float inputWidth=stacked?width:(std::min)(120.f,(std::max)(90.f,width*.3f));
                const auto details=[&](){
                    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("编辑目标：%.9g\n当前请求：%.9g\n实际权重：%.9g\n%s",edit.value,key.state.requested,key.state.applied,key.owned&&key.state.source!=data.source?"其他来源优先":"");
                };
                ImGui::SetNextItemWidth(stacked?width:(std::max)(1.f,width-inputWidth-gap));
                if(ImGui::SliderFloat("##slider",&edit.value,key.info.minimum,key.info.maximum,"",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoRoundToFormat|ImGuiSliderFlags_NoInput))
                    edit.ticket=control.Post(mhmcontrol::Action::Set,key.target,key.index,edit.value);
                const bool sliderActive=ImGui::IsItemActive();details();
                if(!stacked)ImGui::SameLine(0,gap);ImGui::SetNextItemWidth(inputWidth);
                const bool entered=ImGui::InputFloat("##weight",&edit.value,0,0,"%.9g",ImGuiInputTextFlags_EnterReturnsTrue);
                if(entered)edit.ticket=control.Post(mhmcontrol::Action::Set,key.target,key.index,edit.value);
                edit.active=sliderActive||ImGui::IsItemActive();details();
                ImGui::PopID();
            }ImGui::PopID();ImGui::EndTable();
        }
        ImGui::EndDisabled();FinishPreset(control,preset);
    }
};
}
