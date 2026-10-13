#pragma once
#include <cstdint>
namespace facesmooth {
class HighResolutionClock {
    std::uint64_t origin=0,frequency=0,previous=0;
    bool initialized=false,valid=true;
public:
    bool Start(std::uint64_t qpc,std::uint64_t hz) {
        if(initialized||!valid||qpc==0||hz==0||hz>UINT64_MAX/1000)return false;
        origin=previous=qpc;frequency=hz;initialized=true;return true;
    }
    bool Read(std::uint64_t qpc,std::uint64_t& milliseconds) {
        if(!initialized||!valid)return false;
        if(qpc<previous){valid=false;return false;}
        previous=qpc;const auto elapsed=qpc-origin;
        const auto seconds=elapsed/frequency;
        const auto fraction=(elapsed%frequency)*1000/frequency;
        if(seconds>(UINT64_MAX-fraction)/1000){valid=false;return false;}
        milliseconds=seconds*1000+fraction;return true;
    }
};
class SampleGate {
    std::uint64_t lastSubmission=0;
    bool initialized=false;
public:
    bool Reset(std::uint64_t now){if(initialized)return false;lastSubmission=now;initialized=true;return true;}
    bool Due(std::uint64_t now)const{return initialized&&now>=lastSubmission&&now-lastSubmission>=16;}
    // Call only after acceptance. An unavailable upload does not move this clock.
    bool Submitted(std::uint64_t now){if(!Due(now))return false;lastSubmission=now;return true;}
};
}
