#include "panel_session_tests.hpp"
#include <cstring>
int main(int argc,char** argv){try{
    if(argc!=2||std::strcmp(argv[1],"Session"))throw std::runtime_error("stage capability not implemented");
    paneltest::SessionTests();std::printf("PASS: %u panel capacity checks\n",paneltest::checks);return 0;
}catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}}
