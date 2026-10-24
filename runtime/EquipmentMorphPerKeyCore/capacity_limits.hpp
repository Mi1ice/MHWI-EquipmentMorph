#pragma once
#include <cstdint>

namespace mhm::capacity {
// Experimental guards for the candidate; performance budgets are measured separately.
inline constexpr uint32_t targets=5;
inline constexpr uint32_t keys_per_target=1024;
inline constexpr uint32_t sources=32;
inline constexpr uint32_t batch_updates=targets*keys_per_target;
inline constexpr uint32_t events=256;
}
