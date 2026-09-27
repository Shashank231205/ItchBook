#pragma once

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace itchbook::cli {

// Minimal "<positional...> [--flag] [--option value]" parser; positionals come first.
class Args {
 public:
  Args(int argc, char** argv) : args_(argv + 1, argv + argc) {}

  [[nodiscard]] bool flag(std::string_view name) const {
    return std::ranges::find(args_, name) != args_.end();
  }

  template <typename T>
  [[nodiscard]] std::optional<T> option(std::string_view name) const {
    const auto it = std::ranges::find(args_, name);
    if (it == args_.end()) {
      return std::nullopt;
    }
    if (std::next(it) == args_.end()) {
      throw std::invalid_argument(std::format("{} requires a value", name));
    }
    const std::string_view text = *std::next(it);
    if constexpr (std::is_same_v<T, std::string_view>) {
      return text;
    } else {
      T value{};
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument(std::format("invalid value '{}' for {}", text, name));
      }
      return value;
    }
  }

  [[nodiscard]] std::vector<std::string_view> positional() const {
    std::vector<std::string_view> out;
    for (const std::string_view arg : args_) {
      if (arg.starts_with("--")) {
        break;
      }
      out.push_back(arg);
    }
    return out;
  }

 private:
  std::vector<std::string_view> args_;
};

template <typename... Ts>
void print(std::format_string<Ts...> format, Ts&&... values) {
  std::fputs(std::format(format, std::forward<Ts>(values)...).c_str(), stdout);
}

}  // namespace itchbook::cli
