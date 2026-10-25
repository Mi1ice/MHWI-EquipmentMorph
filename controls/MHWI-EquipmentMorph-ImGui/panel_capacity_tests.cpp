#include "panel_session_tests.hpp"
#include "panel_control_tests.hpp"
#include "panel_preset_tests.hpp"
#include "panel_storage_tests.hpp"
#include <cstring>
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("stage required");
    if(!std::strcmp(argv[1],"Session"))paneltest::SessionTests();
    else if(!std::strcmp(argv[1],"Control"))paneltest::ControlTests();
    else if(!std::strcmp(argv[1],"Preset"))paneltest::PresetTests();
    else if(!std::strcmp(argv[1],"Storage"))paneltest::StorageTests();
    else throw std::runtime_error("stage capability not implemented");
    std::printf("PASS: %u panel capacity checks\n",paneltest::checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
