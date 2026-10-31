#pragma once
#include "Control.hpp"
#include "KeyFilter.hpp"
#include <imgui.h>
#include <cstring>

namespace morphgui::ui {
enum class Tone {Normal,Muted,Success,Warning,Error};
inline ImVec4 Color(Tone tone){
    switch(tone){case Tone::Muted:return ImVec4(.68f,.74f,.82f,1);case Tone::Success:return ImVec4(.57f,.86f,.67f,1);
    case Tone::Warning:return ImVec4(1,.79f,.43f,1);case Tone::Error:return ImVec4(1,.68f,.68f,1);default:return ImVec4(.94f,.96f,.98f,1);}
}
inline void Style(){
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();
    style.WindowPadding=ImVec2(12,10);style.FramePadding=ImVec2(8,4);style.ItemSpacing=ImVec2(8,5);style.ItemInnerSpacing=ImVec2(6,4);
    style.CellPadding=ImVec2(7,4);style.WindowRounding=4;style.FrameRounding=3;style.GrabRounding=3;style.ScrollbarRounding=3;
    auto* colors=style.Colors;colors[ImGuiCol_Text]=Color(Tone::Normal);colors[ImGuiCol_TextDisabled]=Color(Tone::Muted);
    colors[ImGuiCol_WindowBg]=ImVec4(.085f,.095f,.115f,1);colors[ImGuiCol_ChildBg]=ImVec4(.07f,.08f,.10f,1);
    colors[ImGuiCol_PopupBg]=ImVec4(.095f,.11f,.135f,1);colors[ImGuiCol_Border]=ImVec4(.24f,.29f,.35f,1);
    colors[ImGuiCol_FrameBg]=ImVec4(.13f,.16f,.20f,1);colors[ImGuiCol_FrameBgHovered]=ImVec4(.18f,.25f,.33f,1);colors[ImGuiCol_FrameBgActive]=ImVec4(.20f,.30f,.41f,1);
    colors[ImGuiCol_Button]=ImVec4(.17f,.25f,.35f,1);colors[ImGuiCol_ButtonHovered]=ImVec4(.23f,.36f,.50f,1);colors[ImGuiCol_ButtonActive]=ImVec4(.23f,.43f,.64f,1);
    colors[ImGuiCol_Header]=ImVec4(.16f,.26f,.38f,1);colors[ImGuiCol_HeaderHovered]=ImVec4(.18f,.28f,.40f,1);colors[ImGuiCol_HeaderActive]=ImVec4(.18f,.30f,.42f,1);
    colors[ImGuiCol_TitleBg]=ImVec4(.10f,.13f,.17f,1);colors[ImGuiCol_TitleBgActive]=ImVec4(.14f,.21f,.30f,1);
    colors[ImGuiCol_TableHeaderBg]=ImVec4(.13f,.17f,.22f,1);colors[ImGuiCol_TableRowBgAlt]=ImVec4(1,1,1,.035f);
}
// Draw literal UTF-8 names, keeping long labels inside their own row/column.
inline void TextAt(ImVec2 position,float width,const char* text,ImU32 color){
    if(width<=0)return;const float size=ImGui::GetFontSize();auto* font=ImGui::GetFont();auto* draw=ImGui::GetWindowDrawList();
    const char* end=text+std::strlen(text);const char* remaining=end;
    if(ImGui::CalcTextSize(text).x>width){
        const float ellipsis=ImGui::CalcTextSize("…").x;
        font->CalcTextSizeA(size,(std::max)(0.f,width-ellipsis),0,text,end,&remaining);
        const float used=ImGui::CalcTextSize(text,remaining).x;
        const ImVec4 clip(position.x,position.y,position.x+width,position.y+ImGui::GetTextLineHeight());
        if(remaining>text)draw->AddText(font,size,position,color,text,remaining,0,&clip);
        draw->AddText(font,size,ImVec2(position.x+used,position.y),color,"…",nullptr,0,&clip);
    }else draw->AddText(position,color,text,end);
}
inline void Line(const char* text,Tone tone=Tone::Normal){
    const auto position=ImGui::GetCursorScreenPos();const float width=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(width,ImGui::GetTextLineHeight()));TextAt(position,width,text,ImGui::ColorConvertFloat4ToU32(Color(tone)));
    if(ImGui::IsItemHovered()&&ImGui::CalcTextSize(text).x>width){ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*32);ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();}
}
inline void Tooltip(const char* text){ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*32);ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();}
inline void Wrapped(const char* text,Tone tone=Tone::Normal){ImGui::PushStyleColor(ImGuiCol_Text,Color(tone));ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();}
inline void NextButton(const char* label){
    const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2<=right)ImGui::SameLine();
}
inline float ButtonWidth(const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;}
inline void NextItem(float width){
    const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+width<=right)ImGui::SameLine();
}
struct Notice {
    FeedbackTime time;
    void Draw(const morphpreset::State& state,uint64_t waiting,const std::string& error){
        const bool show=time.Show(state.completed,state.success,ImGui::GetTime());
        const auto tone=!error.empty()?Tone::Error:waiting?Tone::Muted:!state.completed?Tone::Warning:state.success?Tone::Success:Tone::Error;
        const std::string message=!error.empty()?error:waiting?"正在处理预设，请稍候…":show?state.message:std::string{};
        if(message.empty())return;
        ImGui::PushID("operation-result");
        const bool details=(!error.empty()||(!waiting&&!state.success))&&ImGui::CalcTextSize(message.c_str()).x>ImGui::GetContentRegionAvail().x;
        Line(message.c_str(),tone);
        if(details&&ImGui::TreeNode("错误详情")){Wrapped(message.c_str(),tone);ImGui::TreePop();}ImGui::PopID();
    }
};
}
