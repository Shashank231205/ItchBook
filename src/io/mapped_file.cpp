#include "itchbook/io/mapped_file.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <system_error>
#include <utility>

namespace itchbook {

namespace {

[[noreturn]] void throw_errno(const std::string& what) {
  throw std::system_error(errno, std::generic_category(), what);
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
  ~FileDescriptor() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_;
};

}  // namespace

MappedFile::MappedFile(const std::filesystem::path& path) {
  const FileDescriptor file{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
  if (file.get() < 0) {
    throw_errno("open " + path.string());
  }

  struct stat info {};
  if (::fstat(file.get(), &info) != 0) {
    throw_errno("fstat " + path.string());
  }
  size_ = static_cast<std::size_t>(info.st_size);
  if (size_ == 0) {
    return;
  }

  void* address = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, file.get(), 0);
  if (address == MAP_FAILED) {
    throw_errno("mmap " + path.string());
  }
  data_ = address;
  ::madvise(data_, size_, MADV_SEQUENTIAL);
}

MappedFile::~MappedFile() { unmap(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this != &other) {
    unmap();
    data_ = std::exchange(other.data_, nullptr);
    size_ = std::exchange(other.size_, 0);
  }
  return *this;
}

void MappedFile::unmap() noexcept {
  if (data_ != nullptr) {
    ::munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
  }
}

}  // namespace itchbook
