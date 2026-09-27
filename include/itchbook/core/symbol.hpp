#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace itchbook {

// Fixed-width, right space-padded ticker exactly as it appears on the wire.
class Symbol {
 public:
  static constexpr std::size_t kWidth = 8;

  constexpr Symbol() noexcept { chars_.fill(' '); }

  [[nodiscard]] static constexpr Symbol from(std::string_view text) noexcept {
    Symbol symbol;
    for (std::size_t i = 0; i < kWidth && i < text.size(); ++i) {
      symbol.chars_[i] = text[i];
    }
    return symbol;
  }

  [[nodiscard]] static Symbol from_wire(const std::byte* data) noexcept {
    Symbol symbol;
    std::memcpy(symbol.chars_.data(), data, kWidth);
    return symbol;
  }

  [[nodiscard]] constexpr std::string_view view() const noexcept {
    std::size_t length = kWidth;
    while (length > 0 && chars_[length - 1] == ' ') {
      --length;
    }
    return {chars_.data(), length};
  }

  [[nodiscard]] constexpr const std::array<char, kWidth>& raw() const noexcept { return chars_; }

  friend constexpr bool operator==(const Symbol&, const Symbol&) = default;

 private:
  std::array<char, kWidth> chars_{};
};

}  // namespace itchbook
