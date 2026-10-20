#pragma once
#include "Control.hpp"
#include "PanelLayout.hpp"
#include <imgui.h>
#include <cstring>

namespace morphgui {
struct PresetBrowser {
    bool open=false,trash=false,renameDeleted=false;int filter=0;
    char search[129]{},name[129]{};std::string selected,selectedTrash,renameId,error,purgeId,purgeName;bool purgeAll=false;
    void Submit(Control& control,morphpreset::Request request,uint64_t& waiting){
        error.clear();waiting=control.PostPreset(std::move(request));if(!waiting)error="预设请求过多，请稍后重试";
    }
    void Draw(Control& control,const ControlSnapshot& snapshot,uint64_t& waiting){
        if(!open)return;
        const auto* viewport=ImGui::GetMainViewport();const auto mainPosition=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
        const auto pair=ArrangePanels({viewport->WorkPos.x,viewport->WorkPos.y,viewport->WorkSize.x,viewport->WorkSize.y},{mainPosition.x,mainPosition.y,size.x,size.y},ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetWindowPos(ImVec2(pair.main.x,pair.main.y));ImGui::SetWindowSize(ImVec2(pair.main.w,pair.main.h));
        ImGui::SetNextWindowPos(ImVec2(pair.side.x,pair.side.y));ImGui::SetNextWindowSize(ImVec2(pair.side.w,pair.side.h));
        if(!ImGui::Begin("管理预设",&open,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse)){ImGui::End();return;}
        const auto& state=snapshot.presets;
        const bool oldTrash=trash;
        if(ImGui::RadioButton("全部预设",!trash))trash=false;ImGui::SameLine();
        if(ImGui::RadioButton("回收区",trash))trash=true;
        if(oldTrash!=trash)filter=0;
        bool confirm=false;
        if(trash){ImGui::SameLine();ImGui::BeginDisabled(waiting!=0);if(ImGui::Button("删除全部")){purgeAll=true;purgeId.clear();confirm=true;error.clear();}ImGui::EndDisabled();}
        ImGui::TextDisabled("管理文件不会改变当前形态键权重");
        ImGui::SetNextItemWidth((std::max)(180.f,ImGui::GetContentRegionAvail().x-260));
        ImGui::InputTextWithHint("##search","按名称搜索",search,sizeof(search));ImGui::SameLine();ImGui::SetNextItemWidth(170);
        ImGui::Combo("匹配状态",&filter,"全部\0完全匹配\0部分匹配\0无匹配\0");
        const auto& choices=trash?*state.removed:*state.choices;const auto& library=trash?*state.trash:*state.library;
        std::vector<const morphpreset::Choice*> visible;
        for(const auto& choice:choices)if(morphpreset::Visible(choice,search,static_cast<morphpreset::Filter>(filter)))visible.push_back(&choice);
        auto& chosenId=trash?selectedTrash:selected;const morphpreset::Choice* chosen=nullptr;
        for(const auto* choice:visible)if(choice->id==chosenId)chosen=choice;
        if(!chosen){chosenId=visible.empty()?std::string{}:visible.front()->id;if(!visible.empty())chosen=visible.front();}
        if(waiting)ImGui::TextUnformatted("正在处理预设…");else if(!state.message.empty())ImGui::TextWrapped("%s",state.message.c_str());
        if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
        ImGui::TextDisabled("显示 %zu／%zu 项",visible.size(),choices.size());ImGui::Separator();
        const float listWidth=(std::max)(150.f,ImGui::GetContentRegionAvail().x*.36f);
        if(ImGui::BeginChild("preset-list",ImVec2(listWidth,0),ImGuiChildFlags_Borders)){
            if(visible.empty())ImGui::TextWrapped(choices.empty()?(trash?"回收区为空。":"尚无预设。调节形态键后可在主窗口另存为。"):"没有符合条件的预设，请清空搜索或切换匹配筛选。");
            const float line=ImGui::GetTextLineHeightWithSpacing();ImGuiListClipper clipper;clipper.Begin(static_cast<int>(visible.size()),line*2+ImGui::GetStyle().ItemSpacing.y);
            while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;i++){
                const auto* choice=visible[static_cast<size_t>(i)];ImGui::PushID(choice->id.c_str());
                if(ImGui::Selectable("##choose",choice->id==chosenId,0,ImVec2(0,line*2))){chosenId=choice->id;chosen=choice;error.clear();}
                const auto position=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();
                draw->AddText(ImVec2(position.x+4,position.y),ImGui::GetColorU32(ImGuiCol_Text),choice->name.c_str());
                draw->AddText(ImVec2(position.x+4,position.y+line),ImGui::GetColorU32(ImGuiCol_TextDisabled),choice->status.c_str());
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",choice->name.c_str(),choice->status.c_str());ImGui::PopID();
            }
        }ImGui::EndChild();ImGui::SameLine();bool rename=false;
        if(ImGui::BeginChild("preset-details",ImVec2(0,0),ImGuiChildFlags_Borders)){
            const morphpreset::Document* doc=nullptr;if(chosen)for(const auto& item:library)if(item.id==chosenId)doc=&item;
            if(doc){
                ImGui::TextWrapped("%s",doc->name.c_str());ImGui::TextWrapped("%s",chosen->status.c_str());
                ImGui::BeginDisabled(waiting!=0);
                if(ImGui::Button("重命名")){rename=true;renameId=doc->id;renameDeleted=trash;strcpy_s(name,doc->name.c_str());error.clear();}
                ImGui::SameLine();
                if(trash){if(ImGui::Button("恢复"))Submit(control,{morphpreset::Action::Restore,doc->id,{},true},waiting);
                    ImGui::SameLine();if(ImGui::Button("永久删除")){purgeAll=false;purgeId=doc->id;purgeName=doc->name;confirm=true;error.clear();}}
                else if(ImGui::Button("删除到回收区"))Submit(control,{morphpreset::Action::Trash,doc->id,{}},waiting);
                ImGui::EndDisabled();ImGui::Separator();
                for(const auto& part:doc->parts){
                    ImGui::PushID(static_cast<int>(part.part));
                    if(ImGui::CollapsingHeader(morphpreset::PartName(part.part),ImGuiTreeNodeFlags_DefaultOpen)){
                        ImGui::TextWrapped("模型：%s",part.model.c_str());
                        for(const auto& result:chosen->parts)if(result.part==part.part)ImGui::TextWrapped("状态：%s",result.reason.c_str());
                        ImGui::TextDisabled("保存了 %zu 个接管键（包括零权重）",part.keys.size());
                        if(ImGui::TreeNode("保存的权重")){for(const auto& key:part.keys)ImGui::Text("%s：%.9g",key.id.c_str(),key.weight);ImGui::TreePop();}
                        if(ImGui::TreeNode("资产指纹")){ImGui::TextWrapped("MOD3：%s",part.modelSha.c_str());ImGui::TextWrapped("形态键数据：%s",part.packSha.c_str());ImGui::TreePop();}
                    }ImGui::PopID();
                }
                ImGui::TextDisabled("编号：%.8s",doc->id.c_str());
            }else ImGui::TextWrapped("从左侧选择预设查看详情。");
        }ImGui::EndChild();
        if(rename)ImGui::OpenPopup("重命名预设");
        if(confirm)ImGui::OpenPopup("确认永久删除");
        if(ImGui::BeginPopupModal("确认永久删除",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+360);
            if(purgeAll){ImGui::TextUnformatted("永久删除回收区内的全部预设？");ImGui::TextUnformatted("包括被当前名称搜索或匹配筛选隐藏的预设。");}
            else{ImGui::TextUnformatted("永久删除此预设？");ImGui::TextWrapped("%s",purgeName.c_str());}
            ImGui::TextUnformatted("此操作无法撤销。当前形态键权重保持不变。");ImGui::PopTextWrapPos();
            ImGui::BeginDisabled(waiting!=0);
            if(ImGui::Button(purgeAll?"确认删除全部":"确认永久删除")){
                Submit(control,{purgeAll?morphpreset::Action::PurgeAll:morphpreset::Action::Purge,purgeId,{},true},waiting);ImGui::CloseCurrentPopup();
            }ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("取消"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("重命名预设",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted("只修改名称，保留原预设的装备身份和权重。");
            ImGui::SetNextItemWidth(320);const bool enter=ImGui::InputText("名称",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue);
            const bool save=ImGui::Button("确定");ImGui::SameLine();const bool cancel=ImGui::Button("取消");
            if(save||enter){if(!morphpreset::Utf8(name))error="请输入有效名称（最多 128 个 UTF-8 字节）";
                else{Submit(control,{morphpreset::Action::Rename,renameId,name,renameDeleted},waiting);ImGui::CloseCurrentPopup();}}
            if(cancel)ImGui::CloseCurrentPopup();if(!error.empty())ImGui::TextWrapped("%s",error.c_str());ImGui::EndPopup();
        }
        ImGui::End();
    }
};
}
