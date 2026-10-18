#pragma once
#include <atomic>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
namespace drawfilter {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
struct Owners {
    std::atomic<std::uint64_t> bits{0};
    static std::uint64_t Bits(std::uintptr_t owner) noexcept {
        auto hash=static_cast<std::uint64_t>(owner);
        hash=(hash^(hash>>30))*0xbf58476d1ce4e5b9ull;
        hash=(hash^(hash>>27))*0x94d049bb133111ebull;hash^=hash>>31;
        const auto first=static_cast<unsigned>(hash&63);auto second=static_cast<unsigned>((hash>>6)&63);
        if(second==first)second=(second+1)&63;
        return (std::uint64_t{1}<<first)|(std::uint64_t{1}<<second);
    }
    // Caller holds gate. Publish BEFORE the authoritative owner routes. Never
    // remove bits: old owners, collisions and saturation only fall back to the
    // existing locked proof. This filter cannot authorize a replacement.
    void Publish(const std::array<std::uintptr_t,5>& owners) noexcept {
        std::uint64_t added=0;for(const auto owner:owners)if(owner)added|=Bits(owner);
        bits.fetch_or(added,std::memory_order_release);
    }
    bool MayMatch(std::uintptr_t owner)const noexcept {
        if(!owner)return true; // Keep the original invalid-owner path.
        const auto observed=bits.load(std::memory_order_acquire),wanted=Bits(owner);
        return (observed&wanted)==wanted;
    }
};
inline Owners owners;
inline std::atomic<std::uint64_t> reported{0};
inline thread_local unsigned pending=0;
// Avoid replacing the mutex hotspot with one shared atomic increment per draw.
// Report completed batches; each thread retains at most 255 unreported calls.
inline void Bypassed() noexcept {
    if(++pending<256)return;pending=0;
    auto value=reported.load(std::memory_order_relaxed);
    while(value!=UINT64_MAX){
        const auto next=value>UINT64_MAX-256?UINT64_MAX:value+256;
        if(reported.compare_exchange_weak(value,next,std::memory_order_relaxed))return;
    }
}
inline bool Reject(std::uintptr_t owner) noexcept {
#ifdef MHM_DISABLE_DRAW_FILTER
    (void)owner;return false;
#else
    if(owners.MayMatch(owner))return false;
    Bypassed();return true;
#endif
}
template<class Log> void Write(Log& log,std::uint64_t elapsedMs){
    const auto count=reported.load(std::memory_order_relaxed);if(!count)return;
    char line[400]{};const auto n=std::snprintf(line,sizeof(line),"{\"event\":\"draw_filter_window\",\"window_ms\":%llu,\"bypassed_calls_reported\":%llu,\"batch_size\":256,\"unreported_per_thread_max\":255,\"mask_set_bits\":%u,\"counter_saturated\":%s}\n",
        elapsedMs,count,static_cast<unsigned>(std::popcount(owners.bits.load(std::memory_order_relaxed))),count==UINT64_MAX?"true":"false");
    if(n<0||static_cast<size_t>(n)>=sizeof(line)||!log.Write(line))throw std::runtime_error("Draw filter log");
}
}
