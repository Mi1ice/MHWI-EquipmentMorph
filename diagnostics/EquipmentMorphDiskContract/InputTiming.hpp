#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
namespace inputtiming {
enum class Route : unsigned {Other,Original,Guarded};
enum class Phase {Wait,Held,Read};
struct Totals {
    std::uint64_t calls=0,samples=0,invalid=0,total=0,wait=0,held=0,read=0,maxTotal=0,maxWait=0;
    bool saturated=false;
};
using Window=std::array<Totals,3*6*3>;
// All access to metrics is under the runtime gate, including snapshot/reset.
inline Window metrics{};
inline size_t Index(unsigned kind,unsigned part,Route route){return (kind-1)*18+part*3+static_cast<unsigned>(route);}
inline void Add(std::uint64_t& value,std::uint64_t delta,bool& saturated){
    if(delta>UINT64_MAX-value){value=UINT64_MAX;saturated=true;}else value+=delta;
}
struct Sample {
    std::uint64_t start=0,last=0,wait=0,held=0,read=0;
    Phase phase=Phase::Wait;bool valid=true;
    explicit Sample(std::uint64_t now):start(now),last(now){}
    void Move(Phase next,std::uint64_t now){
        if(now<last)valid=false;
        if(valid){auto& sum=phase==Phase::Wait?wait:phase==Phase::Held?held:read;sum+=now-last;}
        last=now;phase=next;
    }
    void Record(Totals& target,std::uint64_t now){
        Move(phase,now);
        if(!valid){Add(target.invalid,1,target.saturated);return;}
        const auto elapsed=now-start;Add(target.samples,1,target.saturated);
        Add(target.total,elapsed,target.saturated);Add(target.wait,wait,target.saturated);
        Add(target.held,held,target.saturated);Add(target.read,read,target.saturated);
        target.maxTotal=std::max(target.maxTotal,elapsed);target.maxWait=std::max(target.maxWait,wait);
    }
};
// Pseudorandom sampling avoids locking measurement to a repeating mesh order.
struct Sampler {
    std::uint32_t state=1;
    bool Next(){if(!state)state=1;state^=state<<13;state^=state>>17;state^=state<<5;return (state&63)==0;}
};
#ifdef MHM_DISABLE_INPUT_TIMING
inline constexpr bool enabled=false;
#else
inline constexpr bool enabled=true;
#endif
inline thread_local std::array<Sampler,3> samplers={Sampler{0x1234567u^GetCurrentThreadId()},Sampler{0x7654321u^GetCurrentThreadId()},Sampler{0x13579BDu^GetCurrentThreadId()}};
inline std::uint64_t Clock(){LARGE_INTEGER value{};return QueryPerformanceCounter(&value)?static_cast<std::uint64_t>(value.QuadPart):0;}
inline std::optional<std::uint64_t> Begin(unsigned kind){
    if constexpr(enabled)if(kind>=1&&kind<=3&&samplers[kind-1].Next()){
        const auto now=Clock();if(now)return now;
    }
    return {};
}
class Trace {
    unsigned kind,part=5;Route route=Route::Other;std::optional<Sample> sample;
    void Move(Phase next){if(sample)sample->Move(next,Clock());}
public:
    // Construct AFTER acquiring the unique_lock; destruction records BEFORE
    // that lock is released. No logging, allocation or second lock occurs here.
    Trace(unsigned value,std::optional<std::uint64_t> start):kind(value){if(start){sample.emplace(*start);Move(Phase::Held);}}
    ~Trace(){
        if constexpr(enabled)if(kind>=1&&kind<=3){auto& target=metrics[Index(kind,part,route)];Add(target.calls,1,target.saturated);if(sample)sample->Record(target,Clock());}
    }
    void Part(unsigned value){part=value<5?value:5;}
    void Path(Route value){route=value;}
    void Reading(){Move(Phase::Read);}
    void Waiting(){Move(Phase::Wait);}
    void Acquired(){Move(Phase::Held);}
};
inline Window Take(){const auto window=metrics;metrics={};return window;}
struct WindowClock {
    std::uint64_t last=0;
    std::optional<std::uint64_t> Due(std::uint64_t now){
        if(now<last||now-last<1000)return {};
        const auto elapsed=now-last;last=now;return elapsed;
    }
};
template<class Log> void Write(Log& log,const Window& window,std::uint64_t elapsedMs,std::uint64_t frequency){
    constexpr const char* names[]={"other","original","guarded"};
    for(unsigned kind=1;kind<=3;kind++)for(unsigned part=0;part<6;part++)for(unsigned route=0;route<3;route++){
        const auto& s=window[Index(kind,part,static_cast<Route>(route))];if(!s.calls)continue;
        char line[1100]{};const auto n=std::snprintf(line,sizeof(line),"{\"event\":\"input_timing_window\",\"kind\":%u,\"part\":%u,\"route\":\"%s\",\"window_ms\":%llu,\"qpc_frequency\":%llu,\"nominal_sample_interval\":64,\"calls\":%llu,\"samples\":%llu,\"invalid_samples\":%llu,\"total_qpc\":%llu,\"wait_qpc\":%llu,\"held_qpc\":%llu,\"read_qpc\":%llu,\"max_total_qpc\":%llu,\"max_wait_qpc\":%llu,\"saturated\":%s}\n",kind,part,names[route],elapsedMs,frequency,s.calls,s.samples,s.invalid,s.total,s.wait,s.held,s.read,s.maxTotal,s.maxWait,s.saturated?"true":"false");
        if(n<0||static_cast<size_t>(n)>=sizeof(line)||!log.Write(line))throw std::runtime_error("Input timing log");
    }
}
}
