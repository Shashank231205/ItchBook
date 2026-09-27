#include "itchbook/io/mapped_file.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace itchbook {
namespace {

class MappedFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("itchbook_" +
             std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
  }
  void TearDown() override { std::filesystem::remove(path_); }

  void write(const std::string& contents) const {
    std::ofstream(path_, std::ios::binary) << contents;
  }

  std::filesystem::path path_;
};

TEST_F(MappedFileTest, MapsFileContents) {
  write("hello itch");
  const MappedFile file(path_);
  ASSERT_EQ(file.size(), 10U);
  EXPECT_EQ(static_cast<char>(file.bytes()[0]), 'h');
  EXPECT_EQ(static_cast<char>(file.bytes()[9]), 'h');
}

TEST_F(MappedFileTest, EmptyFileYieldsEmptySpan) {
  write("");
  const MappedFile file(path_);
  EXPECT_TRUE(file.bytes().empty());
}

TEST_F(MappedFileTest, MoveTransfersOwnership) {
  write("abc");
  MappedFile first(path_);
  const MappedFile second(std::move(first));
  EXPECT_EQ(second.size(), 3U);
  EXPECT_EQ(first.size(), 0U);  // NOLINT(bugprone-use-after-move)
}

TEST(MappedFile, ThrowsForMissingFile) {
  EXPECT_THROW(MappedFile("/nonexistent/itchbook.itch"), std::system_error);
}

}  // namespace
}  // namespace itchbook
