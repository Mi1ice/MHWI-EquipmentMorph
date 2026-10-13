#define wWinMain UnusedPanelEntry
#include "panel.cpp"
#undef wWinMain
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <fstream>

int main(){try {
    unsigned checks=0;auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);checks++;};
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"MHWIEquipmentMorphPanelTest";RegisterClassW(&cls);
    window=CreateWindowExW(0,cls.lpszClassName,L"MHWI-EquipmentMorph · 形态键控制",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,0,0,880,550,nullptr,nullptr,instance,nullptr);
    require(window!=nullptr,"create actual panel window");KillTimer(window,1);
    snapshot.targetCount=2;snapshot.keyCount=3;snapshot.source=10;
    snapshot.targets[0].info.handle=11;snapshot.targets[0].info.part=MHM_BODY;snapshot.targets[0].info.key_count=2;strcpy_s(snapshot.targets[0].info.model_id,"synthetic/body");snapshot.targets[0].count=2;
    snapshot.targets[1].info.handle=22;snapshot.targets[1].info.part=MHM_ARMS;snapshot.targets[1].info.key_count=1;strcpy_s(snapshot.targets[1].info.model_id,"synthetic/arms");snapshot.targets[1].first=2;snapshot.targets[1].count=1;
    for(uint32_t i=0;i<3;i++){auto& k=snapshot.keys[i];k.target=i<2?11:22;k.index=i<2?i:0;k.info.maximum=1;strcpy_s(k.info.id,i==0?"Example_A":i==1?"Example_B":"Example_C");}
    Populate(true);
    require(SendMessageW(combo,CB_GETCOUNT,0,0)==2&&ListView_GetItemCount(list)==2&&!IsWindowEnabled(slider),"dynamic part/key discovery and read-only slider");
    SetWindowTextW(edit,L"0.13724681");SendMessageW(window,WM_COMMAND,Apply,0);
    require(queue.size()==1&&queue.back().request.action==Action::Set&&queue.back().request.weight==.13724681f,"arbitrary typed continuous weight");queue.clear();
    auto& key=snapshot.keys[0];key.owned=1;key.contribution=.13724681f;key.state.requested=.13724681f;key.state.source=10;Populate(false);
    require(IsWindowEnabled(slider)&&SendMessageW(check,BM_GETCHECK,0,0)==BST_CHECKED,"acknowledged takeover enables slider");
    SendMessageW(slider,TBM_SETPOS,TRUE,731);SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(slider));
    require(queue.size()==1&&queue.back().request.weight==.731f,"slider sends continuous target");
    SendMessageW(slider,TBM_SETPOS,TRUE,650);SendMessageW(window,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(slider));
    require(queue.size()==1&&queue.back().request.weight==.65f,"pending slider updates coalesce");
    SendMessageW(check,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(Own,BN_CLICKED),0);
    require(queue.size()==2&&queue.back().request.action==Action::Drop,"per-key release remains ordered after slider");
    SendMessageW(window,WM_COMMAND,ReleaseAll,0);require(queue.size()==1&&queue.front().request.action==Action::Release,"release all cancels queued gestures");queue.clear();
    SendMessageW(combo,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(Equipment,CBN_SELCHANGE),0);
    require(selectedTarget==22&&ListView_GetItemCount(list)==1&&Selected()->info.id[8]=='C',"equipment selection uses current target handle");
    SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);Populate(false);
    require(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0)!=0,"polling preserves open equipment selection");SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
    snapshot.targets[1].info.handle=33;snapshot.keys[2].target=33;Populate(false);
    require(selectedTarget==11&&selectedKey==0&&!snapshot.keys[2].owned,"replaced target selection has no ownership replay");
    snapshot.keys[0].state.applied=.13724681f;SetWindowTextW(status,L"软件窗口检查 · 仅使用合成示例，无游戏连接");
    Populate(false);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    RECT rect{};GetWindowRect(window,&rect);const int width=rect.right-rect.left,height=rect.bottom-rect.top;
    const auto screen=GetDC(window),memory=CreateCompatibleDC(screen);BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=width;bitmap.bmiHeader.biHeight=-height;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;const auto image=CreateDIBSection(screen,&bitmap,DIB_RGB_COLORS,&pixels,nullptr,0);const auto old=SelectObject(memory,image);
    require(PrintWindow(window,memory,0)!=FALSE,"render actual controls to artifact");
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);const auto output=std::filesystem::path(path).parent_path()/L"control-panel-preview.bmp";
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+static_cast<DWORD>(width*height*4);
    std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&bitmap.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<const char*>(pixels),width*height*4);file.close();
    SelectObject(memory,old);DeleteObject(image);DeleteDC(memory);ReleaseDC(window,screen);DestroyWindow(window);DeleteObject(font);
    std::printf("PASS: %u native UI checks; synthetic metadata only\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
