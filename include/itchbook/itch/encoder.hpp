#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "itchbook/itch/codec.hpp"
#include "itchbook/itch/endian.hpp"
#include "itchbook/itch/messages.hpp"

namespace itchbook::itch {

// Serialises messages into the length-prefixed on-disk format. Used by the synthetic feed
// generator, tests, and benchmarks.
class Encoder {
 public:
  void reserve(std::size_t bytes) { buffer_.reserve(bytes); }

  template <typename Message>
  void write(const Message& message) {
    constexpr std::size_t kSize = Codec<Message>::kSize;
    const std::size_t offset = buffer_.size();
    buffer_.resize(offset + kLengthPrefix + kSize);
    store_be(buffer_.data() + offset, static_cast<std::uint16_t>(kSize));
    Codec<Message>::encode(message, buffer_.data() + offset + kLengthPrefix);
  }

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return buffer_; }
  [[nodiscard]] std::vector<std::byte> release() noexcept { return std::exchange(buffer_, {}); }
  void clear() noexcept { buffer_.clear(); }

 private:
  std::vector<std::byte> buffer_;
};

}  // namespace itchbook::itch
