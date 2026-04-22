#pragma once

/**
 * @file PSRAMAllocator.h
 * @brief Header-only C++ allocator that prefers PSRAM allocation on ESP32
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

#include "RAMAllocator.h"
#include "esp_heap_caps.h"

namespace tinygpu {

/**
 * @class PSRAMAllocator
 * @brief STL-compatible allocator that prefers PSRAM allocation with fallback
 *
 * PSRAMAllocator is a custom C++ allocator that attempts to allocate memory in
 * external PSRAM (using heap_caps_aligned_calloc with MALLOC_CAP_SPIRAM) and
 * falls back to regular heap allocation if PSRAM allocation fails.
 *
 * This allocator is particularly useful for ESP32 platforms with external PSRAM
 * where large buffers (like video frames) should be stored in PSRAM to preserve
 * internal SRAM for other uses. For small, frequently-accessed buffers that
 * need fast access, consider using RAMAllocator instead.
 *
 * @tparam T The type of objects to allocate
 *
 * @note On ESP-IDF platforms, uses heap_caps_aligned_calloc for PSRAM
 * allocation
 * @note On Arduino platforms, the heap_caps functions are shimmed to standard
 * malloc/free
 * @note Memory is zero-initialized (calloc behavior)
 * @note Provides proper alignment for the allocated type
 *
 * Example usage:
 * @code
 * #include "PSRAMAllocator.h"
 *
 * // Use PSRAMAllocator with std::vector for large buffers
 * std::vector<uint8_t, PSRAMAllocator<uint8_t>> largeBuffer;
 * largeBuffer.resize(1024 * 1024);  // Large buffer preferably in PSRAM
 *
 * // Or use the convenience alias
 * PSVec<uint8_t> videoFrame;    // Large video frame data in PSRAM
 * videoFrame.resize(640 * 480 * 3);
 *
 * // For small buffers in fast RAM, use RAMAllocator (separate header)
 * #include "RAMAllocator.h"
 * RAMVec<uint16_t> lookupTable; // Small lookup table in fast RAM
 * @endcode
 */
template <typename T = uint8_t>
class PSRAMAllocator {
 public:
  /** @brief The type of objects this allocator can allocate */
  using value_type = T;

  /**
   * @brief Default constructor
   *
   * Constructs a PSRAMAllocator. No initialization needed.
   */
  PSRAMAllocator() noexcept {}

  /**
   * @brief Copy constructor from different allocator type
   *
   * Allows construction from PSRAMAllocator of different type U.
   * Required for STL allocator requirements.
   *
   * @tparam U The type parameter of the source allocator
   * @param other Source allocator (unused)
   */
  template <class U>
  PSRAMAllocator(const PSRAMAllocator<U>&) noexcept {}

  /**
   * @brief Allocate memory for n objects of type T
   *
   * Attempts to allocate memory in the following priority order:
   * 1. PSRAM using heap_caps_aligned_calloc (if available)
   * 2. Aligned allocation using aligned_alloc (C11)
   * 3. Standard calloc as fallback
   *
   * All allocated memory is zero-initialized.
   *
   * @param n Number of objects of type T to allocate
   * @return Pointer to allocated memory
   * @throws std::bad_alloc if allocation fails
   *
   * @note Memory is aligned to alignof(T)
   * @note On ESP32 with PSRAM, prefers external PSRAM allocation
   */
  T* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    // total bytes
    std::size_t bytes = n * sizeof(T);
    // Prefer PSRAM: use heap_caps_aligned_calloc if available. Request
    // alignment equal to alignof(T).
    void* p = nullptr;
#if defined(MALLOC_CAP_SPIRAM)
    // heap_caps_aligned_calloc(alignment, nmemb, size, caps)
    p = heap_caps_aligned_calloc(alignof(T), n, sizeof(T),
                                 (unsigned)MALLOC_CAP_SPIRAM);
#endif
    if (!p) {
      // Fallback: try aligned allocation where available
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
      // aligned_alloc requires size to be multiple of alignment
      std::size_t align = alignof(T);
      std::size_t alloc_size = bytes;
      if (align > 1) {
        std::size_t rem = alloc_size % align;
        if (rem) alloc_size += (align - rem);
      }
      p = aligned_alloc(align, alloc_size);
      if (p) {
        // zero initialize
        std::memset(p, 0, bytes);
      } else
#endif
      {
        // last resort: calloc
        p = std::calloc(n, sizeof(T));
      }
    }

    if (!p) throw std::bad_alloc();
    return static_cast<T*>(p);
  }

  /**
   * @brief Deallocate memory previously allocated by this allocator
   *
   * Frees memory that was allocated by allocate(). Uses heap_caps_free
   * which works for both PSRAM and regular heap allocations.
   *
   * @param p Pointer to memory to deallocate (can be nullptr)
   * @param n Number of objects (ignored, but required by allocator interface)
   *
   * @note Safe to call with nullptr
   * @note The size parameter n is ignored as heap_caps_free doesn't need it
   */
  void deallocate(T* p, std::size_t /*n*/) noexcept {
    if (!p) return;
    heap_caps_free(p);
  }

  /**
   * @brief Rebind allocator to different type
   *
   * Provides the rebind mechanism required by STL allocators.
   * Allows containers to allocate different types using the same allocator.
   *
   * @tparam U The type to rebind to
   */
  template <class U>
  struct rebind {
    /** @brief The rebound allocator type */
    typedef PSRAMAllocator<U> other;
  };
};

/**
 * @brief Convenience type alias for vectors using PSRAMAllocator
 *
 * PSVec provides a shorter way to declare vectors that use PSRAMAllocator.
 *
 * @tparam T The element type of the vector
 *
 * Example usage:
 * @code
 * PSVec<uint8_t> buffer;  // Instead of std::vector<uint8_t,
 * PSRAMAllocator<uint8_t>> buffer.resize(1024);
 * @endcode
 */
template <typename T>
using PSVec = std::vector<T, PSRAMAllocator<T>>;

}  // namespace esp_h264

/**
 * @brief Equality operator for PSRAMAllocator instances
 *
 * All PSRAMAllocator instances are considered equal regardless of their
 * template type parameter, as required by STL allocator requirements.
 *
 * @tparam T First allocator's value type
 * @tparam U Second allocator's value type
 * @param lhs First allocator
 * @param rhs Second allocator
 * @return true Always returns true
 */
template <class T, class U>
inline bool operator==(const esp_h264::PSRAMAllocator<T>&,
                       const esp_h264::PSRAMAllocator<U>&) noexcept {
  return true;
}

/**
 * @brief Inequality operator for PSRAMAllocator instances
 *
 * All PSRAMAllocator instances are considered equal, so inequality
 * always returns false.
 *
 * @tparam T First allocator's value type
 * @tparam U Second allocator's value type
 * @param lhs First allocator
 * @param rhs Second allocator
 * @return false Always returns false
 */
template <class T, class U>
inline bool operator!=(const esp_h264::PSRAMAllocator<T>&,
                       const esp_h264::PSRAMAllocator<U>&) noexcept {
  return false;
}