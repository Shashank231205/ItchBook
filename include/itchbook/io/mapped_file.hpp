#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

namespace itchbook {

// Read-only memory mapping of a whole file (POSIX). The parser works directly on the mapped
// pages, so replaying a multi-gigabyte capture involves no read() copies.
class MappedFile {
 public:
  // Throws std::system_error if the file cannot be opened or mapped.
  explicit MappedFile(const std::filesystem::path& path);
  ~MappedFile();

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return {static_cast<const std::byte*>(data_), size_};
  }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

 private:
  void unmap() noexcept;

  void* data_{nullptr};
  std::size_t size_{0};
};

}  // namespace itchbook
