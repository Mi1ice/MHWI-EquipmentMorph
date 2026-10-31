#pragma once
#include "Controller.hpp"
#include <climits>
#include <deque>

namespace mhmvalidation {
struct WindowCommands {
    std::deque<Action> actions;bool closing=false;
    void Push(Action action){if(closing)return;if(action==Action::End){closing=true;actions.clear();}else if(action==Action::ZeroAll)actions.clear();actions.push_back(action);}
};
inline std::wstring Wide(std::string_view input){
    if(input.empty())return {};Require(input.size()<=INT_MAX,"UI text too large");const auto length=static_cast<int>(input.size());
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),length,nullptr,0);Require(size>0,"UI text is not valid UTF8");std::wstring result(static_cast<size_t>(size),L'\0');
    Require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,input.data(),length,result.data(),size)==size,"UI text conversion failed");return result;
}
struct WindowModel {std::wstring title,status,details;std::map<Action,bool> buttons;};
inline WindowModel MakeWindowModel(const Snapshot& s){
    WindowModel m;m.title=L"形态键 · SDK现场对照";
    m.status=L"模式："+Wide(s.reference.mode.empty()?"未识别":s.reference.mode)+L"    案例 "+std::to_wstring(s.caseIndex+1)+L" / "+std::to_wstring(s.caseCount)+L"    步骤 "+std::to_wstring(s.stepIndex+1)+L" / "+std::to_wstring(s.stepCount)+L"\r\n";
    switch(s.phase){
    case Phase::NotReady:m.status+=L"未就绪：等待匹配的身体模型、完整键表及资源身份。启动不会自动施加案例。";break;
    case Phase::NotStarted:m.status+=s.zeroConfirmed?L"全部动态键归零已确认，来源保持。当前案例未开始，点击“开始本例”才施加。":L"本例未开始。点击“开始本例”才施加下列预览权重。";break;
    case Phase::WaitingApply:m.status+=L"已提交，正在等待核心报告应用。当前操作未完成。";break;
    case Phase::Holding:m.status+=L"核心报告已应用。保持当前权重，等待你在游戏与Blender副本中观察。";break;
    case Phase::WaitingZero:m.status+=L"已提交全部动态键归零，等待核心确认；归零未确认前不切换案例。";break;
    case Phase::Covered:m.status+=L"被其他来源影响，尚未确认本工具全部权重生效；待归零的操作仍为归零未确认。";break;
    case Phase::Interrupted:m.status+=L"操作已中断，归零未确认。旧案例不会自动重放；重新就绪后需手动开始。";break;
    case Phase::Ending:m.status+=L"正在结束：等待全部动态键归零确认，随后释放自身来源。";break;
    case Phase::Ended:m.status+=L"已结束：结束前归零已确认，自身来源已释放。其他来源可能随后恢复控制。";break;
    }
    if(!s.evidenceComplete)m.status+=L"\r\n操作记录不完整：已停止新案例及非零步骤，仍可归零／结束。";
    if(!s.error.empty())m.status+=L"\r\n"+Wide(s.error);
    size_t fixed=0;for(const auto& row:s.rows)fixed+=row.fixed?1u:0u;
    m.details=Wide(s.caseId+" · "+s.caseTitle)+L"\r\n固定 "+std::to_wstring(fixed)+L" / 变化 "+std::to_wstring(s.rows.size()-fixed)+L"    实际目标键数 "+std::to_wstring(s.reference.liveIds.size())+L"\r\n";
    m.details+=s.started?L"本例目标权重（应用情况以上方状态为准）\r\n":L"本例权重预览（尚未开始时不会自动施加）\r\n";
    for(const auto& row:s.rows)m.details+=std::wstring(row.fixed?L"固定  ":L"变化  ")+Wide(Number(row.weight))+L"    "+Wide(row.source)+L"\r\n    ID: "+Wide(row.id)+L"\r\n";
    m.details+=L"\r\n组合尾步：5个变化键回到0时，25个固定键仍为0.5；“全部归零”才清除本目标所有动态键。\r\n切到Blender或最小化不会自动推进步骤或释放来源。画面结论由你观察并另行记录。";
    for(auto a:{Action::Begin,Action::PreviousCase,Action::NextCase,Action::PreviousStep,Action::NextStep,Action::RepeatStep,Action::ZeroAll,Action::End})m.buttons[a]=false;
    if(!s.finished){m.buttons[Action::End]=true;m.buttons[Action::ZeroAll]=!s.ending&&s.observation.result==MHM_OK;}
    const bool available=!s.finished&&!s.ending&&!s.pending&&s.evidenceComplete&&s.observation.result==MHM_OK;
    if(available&&(s.phase==Phase::Holding||s.phase==Phase::NotStarted)){
        m.buttons[Action::PreviousCase]=s.caseIndex>0;m.buttons[Action::NextCase]=s.caseIndex+1<s.caseCount;
        m.buttons[Action::Begin]=s.phase==Phase::NotStarted&&!s.started;
        if(s.phase==Phase::Holding&&s.started){m.buttons[Action::PreviousStep]=s.stepIndex>0;m.buttons[Action::NextStep]=s.stepIndex+1<s.stepCount;m.buttons[Action::RepeatStep]=true;}
    }
    return m;
}
int RunValidationWindow(HINSTANCE,const Catalog&,const MhmApi&,IdentityQuery,const std::filesystem::path& logDirectory);
}
