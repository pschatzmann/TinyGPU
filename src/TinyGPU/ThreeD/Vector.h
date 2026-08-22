#pragma once
#include <vector>

namespace tinygpu {

#if TINYGPU_USE_PSRAM_ALLOCATOR
// Type alias for Vector using PSRAMAllocator
template <typename T>
using Vector = std::vector<T, PSRAMAllocator<T>>;
#else
// Fallback to standard vector if PSRAMAllocator is not used
template <typename T>
using Vector = std::vector<T>;
#endif

}  // namespace tinygpu
