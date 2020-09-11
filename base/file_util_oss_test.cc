//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "base/file_util_oss.h"

#include <sys/stat.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "base/status_matchers.h"

namespace file_based_test_driver::internal {

using ::file_based_test_driver::testing::StatusIs;

TEST(FileUtilTest, NullFreeString) {
  std::string str;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(NullFreeString("abcd0", &str));
  EXPECT_EQ(str, "abcd0");
  constexpr absl::string_view v("\0123\0", 5);
  EXPECT_EQ(v.size(), 5);
  str.clear();
  EXPECT_THAT(NullFreeString(v, &str),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(str, "");
}

TEST(FileUtilTest, MatchFailsOnMissingFile) {
  std::vector<std::string> filenames;
  EXPECT_THAT(Match("/file/does/not/exist", &filenames),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_TRUE(filenames.empty());
}

TEST(FileUtilTest, MatchFailsOnWrongTypeOfFile) {
  std::vector<std::string> filenames;
  EXPECT_THAT(Match("/dev/null", &filenames),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(filenames.empty());

  // Directory exists, but is not a regular file.
  EXPECT_THAT(Match(getenv("TEST_SRCDIR"), &filenames),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(filenames.empty());
}

TEST(FileUtilTest, MatchSucceed) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/base/file_util_oss_test.input_file");

  std::vector<std::string> filenames;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(Match(filespec, &filenames));
  ASSERT_EQ(filenames.size(), 1);
  EXPECT_EQ(filenames[0], filespec);
}

TEST(FileUtilTest, GetContentsFailsOnMissingFile) {
  std::string contents;
  EXPECT_THAT(GetContents("/file/does/not/exist", &contents),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_TRUE(contents.empty());
}

TEST(FileUtilTest, GetContentsFailsOnWrongTypeOfFile) {
  // Directory exists, but is not a regular file.
  std::string contents;
  EXPECT_THAT(GetContents(getenv("TEST_SRCDIR"), &contents),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(contents.empty());
}

TEST(FileUtilTest, GetContentsSucceed) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/base/file_util_oss_test.input_file");

  std::string contents;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(GetContents(filespec, &contents));
  EXPECT_FALSE(contents.empty());
  EXPECT_EQ(contents, R"(This file
loads super

great!
)");
}

TEST(FileUtilTest, SetContentsSucceed) {
  const std::string filespec =
      absl::StrCat(TestTmpDir(), "/SetContentsSucceed.file");

  const std::string contents = R"(great multi
  line file. Well
  read!!

  )";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(SetContents(filespec, contents));
  std::string actual_contents;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(GetContents(filespec, &actual_contents));
  EXPECT_EQ(contents, actual_contents);
}

static bool DirectoryExists(const std::string& dirname) {
  struct stat status;
  return stat(dirname.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

TEST(FileUtilTest, RecursivelyCreateDirSucceedOnExistingDirectory) {
  const std::string filespec = absl::StrCat(TestTmpDir());
  EXPECT_TRUE(DirectoryExists(filespec));
  FILE_BASED_TEST_DRIVER_EXPECT_OK(RecursivelyCreateDir(filespec));
  EXPECT_TRUE(DirectoryExists(filespec));
}

TEST(FileUtilTest, RecursivelyCreateDirSucceed) {
  const std::string filespec = absl::StrCat(TestTmpDir(), "/CreateDirSucceed");
  EXPECT_FALSE(DirectoryExists(filespec));
  FILE_BASED_TEST_DRIVER_EXPECT_OK(RecursivelyCreateDir(filespec));
  EXPECT_TRUE(DirectoryExists(filespec));
}

TEST(FileUtilTest, RecursivelyCreateDirSucceedRecursive) {
  const std::string filespec =
      absl::StrCat(TestTmpDir(), "/a/b/c/RecursivelyCreateDirSucceedRecursive");
  EXPECT_FALSE(DirectoryExists(filespec));
  FILE_BASED_TEST_DRIVER_EXPECT_OK(RecursivelyCreateDir(filespec));
  EXPECT_TRUE(DirectoryExists(filespec));
}

TEST(FileUtilTest, CreateDirThenSetAndGetContents) {
  const std::string dirname =
      absl::StrCat(TestTmpDir(), "/x/y/z/CreateDirThenSetAndGetContents");
  const std::string filename = absl::StrCat(dirname, "/the.file");
  EXPECT_FALSE(DirectoryExists(dirname));
  FILE_BASED_TEST_DRIVER_EXPECT_OK(RecursivelyCreateDir(dirname));
  EXPECT_TRUE(DirectoryExists(dirname));
  std::string contents = "CreateDirThenSetAndGetContents file contents";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(SetContents(filename, contents));
  std::string actual_contents;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(GetContents(filename, &actual_contents));
  EXPECT_EQ(contents, actual_contents);
}

TEST(FileUtilTest, RecursivelyCreateDirSucceedsOnRootDir) {
  const std::string filespec = "/";
  EXPECT_TRUE(DirectoryExists(filespec));
  FILE_BASED_TEST_DRIVER_EXPECT_OK(RecursivelyCreateDir(filespec));
  EXPECT_TRUE(DirectoryExists(filespec));
}

TEST(FileUtilTest, RecursivelyCreateDirFailsOnEmpty) {
  const std::string filespec = "";
  EXPECT_FALSE(DirectoryExists(filespec));
  EXPECT_THAT(RecursivelyCreateDir(filespec),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(DirectoryExists(filespec));
}

TEST(FileUtilTest, RecursivelyCreateDirFailsOnReadOnlyDir) {
  const std::string read_only_dir =
      absl::StrCat(TestTmpDir(), "/RecursivelyCreateDirReadOnlyDir");
  // Create a read-only parent
  FILE_BASED_TEST_DRIVER_CHECK_EQ(0, mkdir(read_only_dir.c_str(),
                    /*mode=*/S_IRUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH |
                        S_IXOTH));
  const std::string filespec =
      absl::StrCat(read_only_dir, "/RecursivelyCreateDirFailsOnReadOnlyDir");
  EXPECT_FALSE(DirectoryExists(filespec));
  EXPECT_THAT(RecursivelyCreateDir(filespec),
              StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_FALSE(DirectoryExists(filespec));
  // Make sure to mark the directory writable so the build system can
  // clean it up afterward.
  FILE_BASED_TEST_DRIVER_CHECK_EQ(0,
           chmod(read_only_dir.c_str(), /*mode=*/S_IRWXU | S_IRWXG | S_IRWXO));
}

TEST(FileUtilTest, RecursivelyCreateDirFailsOnExistingRegularFile) {
  const std::string filespec =
      absl::StrCat(TestTmpDir(), "/SetContentsSucceed.file");

  const std::string contents = R"(great multi
  line file. Well
  read!!

  )";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(SetContents(filespec, contents));
  EXPECT_FALSE(DirectoryExists(filespec));
  EXPECT_THAT(RecursivelyCreateDir(filespec),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_FALSE(DirectoryExists(filespec));
}

TEST(FileUtilTest, RegisterTempFileSetsContents) {
  const std::string contents = "RegisterTempFileSetsContents Contents";
  RegisteredTempFile tmp_file("RegisterTempFileSetsContentsFile", contents);
  std::string actual_contents;
  FILE_BASED_TEST_DRIVER_EXPECT_OK(GetContents(tmp_file.filename(), &actual_contents));
  EXPECT_EQ(contents, actual_contents);
}

TEST(FileUtilTest, RegisterTempFileDeletesFile) {
  std::string filename;
  {
    RegisteredTempFile tmp_file("RegisterTempFileDeletesFile", "contents");
    filename = tmp_file.filename();
  }
  struct stat status;
  EXPECT_NE(stat(filename.c_str(), &status), 0);
}

TEST(FileUtilTest, FileLines) {
  const std::string filespec = absl::StrCat(TestTmpDir(), "/FileLines.file");

  const std::string contents = R"(1.
2.
3.
4.)";
  FILE_BASED_TEST_DRIVER_EXPECT_OK(SetContents(filespec, contents));
  std::string actual_contents;
  EXPECT_THAT(FileLines(filespec),
              ::testing::ElementsAre("1.", "2.", "3.", "4."));
}

}  // namespace file_based_test_driver::internal
