#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace itchbook::itch {

template <std::unsigned_integral T>
[[nodiscard]] constexpr T byteswap(T value) noexcept {
  if constexpr (sizeof(T) == 1) {
    return value;
  } else if constexpr (sizeof(T) == 2) {
    return __builtin_bswap16(value);
  } else if constexpr (sizeof(T) == 4) {
    return __builtin_bswap32(value);
  } else {
    static_assert(sizeof(T) == 8);
    return __builtin_bswap64(value);
  }
}

template <std::unsigned_integral T>
[[nodiscard]] inline T load_be(const std::byte* src) noexcept {
  T value;
  std::memcpy(&value, src, sizeof(T));
  if constexpr (std::endian::native == std::endian::little) {
    value = byteswap(value);
  }
  return value;
}

template <std::unsigned_integral T>
inline void store_be(std::byte* dst, T value) noexcept {
  if constexpr (std::endian::native == std::endian::little) {
    value = byteswap(value);
  }
  std::memcpy(dst, &value, sizeof(T));
}

// ITCH timestamps are 6-byte big-endian nanoseconds since midnight.
[[nodiscard]] inline std::uint64_t load_be48(const std::byte* src) noexcept {
  return (std::uint64_t{load_be<std::uint16_t>(src)} << 32U) | load_be<std::uint32_t>(src + 2);
}

inline void store_be48(std::byte* dst, std::uint64_t value) noexcept {
  store_be(dst, static_cast<std::uint16_t>(value >> 32U));
  store_be(dst + 2, static_cast<std::uint32_t>(value));
}

}  // namespace itchbook::itch
