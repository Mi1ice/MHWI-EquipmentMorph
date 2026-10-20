#pragma once
#include "Control.hpp"
#include "Ui.hpp"
#include <imgui.h>
#include <cstring>

namespace morphgui {
struct PresetImportView {
    struct Edit {bool include=false;char name[129]{};};
    std::map<std::string,Edit> edits;std::string selected,error;
    std::shared_ptr<const std::vector<morphpreset::Incoming>> seen;
    bool opening=false;
    void Submit(Control& control,morphpreset::Request request,uint64_t& waiting){waiting=control.PostPreset(std::move(request));error=waiting?"":"预设请求过多，请稍后重试";}
    void Open(Control& control,uint64_t& waiting){opening=true;Submit(control,{morphpreset::Action::ScanImports},waiting);}
    void Draw(Control& control,const ControlSnapshot& snapshot,uint64_t& waiting){
        if(opening){ImGui::OpenPopup("导入预设");opening=false;}
        const auto& state=snapshot.presets;
        if(seen!=state.incoming){
            seen=state.incoming;std::set<std::string> tokens;
            for(const auto& item:*seen){tokens.insert(item.token);if(!edits.contains(item.token)){Edit edit;strcpy_s(edit.name,item.name.c_str());edits.emplace(item.token,edit);}}
            std::erase_if(edits,[&](const auto& entry){return !tokens.contains(entry.first);});
            if(!tokens.contains(selected))selected=seen->empty()?std::string{}:seen->front().token;
        }
        const auto extent=ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(ImVec2((std::min)(900.f,extent.x),(std::min)(600.f,extent.y)),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2((std::min)(640.f,extent.x),(std::min)(420.f,extent.y)),extent);bool opened=true;
        if(!ImGui::BeginPopupModal("导入预设",&opened)){return;}
        ui::Wrapped("将 JSON 放入 imports 文件夹后刷新。导入使用此次预览内容，不会自动应用。",ui::Tone::Muted);
        ui::Line("目录：nativePC/plugins/MHWEquipmentMorph/imports/",ui::Tone::Muted);
        ImGui::BeginDisabled(waiting!=0);
        if(ImGui::Button("刷新预览"))Submit(control,{morphpreset::Action::ScanImports},waiting);
        ui::NextButton("全选有效项");if(ImGui::Button("全选有效项"))for(const auto& item:*seen)if(item.document)edits[item.token].include=true;
        ui::NextButton("取消全选");if(ImGui::Button("取消全选"))for(auto& [token,edit]:edits){(void)token;edit.include=false;}
        ImGui::EndDisabled();
        ui::Notice(state,waiting,error);
        ImGui::Separator();const float height=(std::max)(120.f,ImGui::GetContentRegionAvail().y-ImGui::GetFrameHeightWithSpacing()-8);
        ImGui::BeginDisabled(waiting!=0);
        if(ImGui::BeginChild("import-list",ImVec2(ImGui::GetContentRegionAvail().x*.44f,height),ImGuiChildFlags_Borders)){
            if(seen->empty())ImGui::TextWrapped("没有待导入文件。仅扫描 imports 根目录内的 JSON，不包含子文件夹。");
            const float line=ImGui::GetTextLineHeightWithSpacing();ImGuiListClipper clipper;clipper.Begin(static_cast<int>(seen->size()),line*2+ImGui::GetStyle().ItemSpacing.y);
            while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;i++){
                const auto& item=(*seen)[static_cast<size_t>(i)];auto& edit=edits[item.token];ImGui::PushID(item.token.c_str());
                ImGui::BeginDisabled(!item.document);ImGui::Checkbox("##include",&edit.include);ImGui::EndDisabled();ImGui::SameLine();
                if(ImGui::Selectable("##preview",selected==item.token,0,ImVec2(0,line*2)))selected=item.token;
                const auto position=ImGui::GetItemRectMin();const float rowWidth=ImGui::GetItemRectMax().x-position.x;
                ui::TextAt(position,rowWidth,item.document?edit.name:item.file.c_str(),ImGui::GetColorU32(ImGuiCol_Text));
                ui::TextAt(ImVec2(position.x,position.y+line),rowWidth,item.document?item.file.c_str():"无效文件，查看原因",ImGui::GetColorU32(ImGuiCol_TextDisabled));
                if(ImGui::IsItemHovered())ui::Tooltip((std::string(item.document?edit.name:"无法导入")+"\n"+item.file).c_str());
                ImGui::PopID();
            }
        }ImGui::EndChild();ImGui::SameLine();
        if(ImGui::BeginChild("import-details",ImVec2(0,height),ImGuiChildFlags_Borders)){
            for(const auto& item:*seen)if(item.token==selected){
                ui::Line((std::string("文件：")+item.file).c_str(),ui::Tone::Muted);
                if(!item.document){ui::Wrapped((std::string("无法导入：")+item.error).c_str(),ui::Tone::Error);continue;}
                ImGui::TextUnformatted("导入后的名称");auto& edit=edits[item.token];ImGui::SetNextItemWidth(-1);ImGui::InputText("##import-name",edit.name,sizeof(edit.name));
                if(!morphpreset::Utf8(edit.name))ui::Wrapped("请输入有效名称，最多 128 个 UTF-8 字节。",ui::Tone::Error);
                if(item.duplicate&&std::strcmp(edit.name,item.name.c_str())==0)ui::Wrapped("此名称与内容已存在，确认导入时会跳过。",ui::Tone::Warning);
                ui::Wrapped(item.status.c_str());ui::Wrapped("不匹配的预设仍可入库，应用时重新校验。",ui::Tone::Muted);
                for(const auto& part:item.document->parts){ImGui::Separator();ImGui::Text("%s · %zu 个键",morphpreset::PartName(part.part),part.keys.size());ImGui::TextWrapped("%s",part.model.c_str());}
                ImGui::TextWrapped("重复项会跳过；同名不同内容请在上方修改名称。原有预设不会被覆盖。");
            }
        }ImGui::EndChild();ImGui::EndDisabled();
        size_t count=0;for(const auto& [token,edit]:edits){(void)token;if(edit.include)count++;}
        ImGui::BeginDisabled(waiting!=0||!count);
        if(ImGui::Button("导入勾选项")){morphpreset::Request request;request.action=morphpreset::Action::Import;
            for(const auto& item:*seen){const auto& edit=edits[item.token];if(edit.include)request.imports.push_back({item.token,edit.name});}Submit(control,std::move(request),waiting);}
        ImGui::EndDisabled();ui::NextButton("关闭");if(ImGui::Button("关闭"))ImGui::CloseCurrentPopup();ImGui::SameLine();ImGui::Text("已勾选 %zu 项",count);
        ImGui::EndPopup();
    }
};
}
