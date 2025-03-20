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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_FILE_UTIL_OSS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_FILE_UTIL_OSS_H_

#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/base/path.h"

namespace file_based_test_driver::internal {

//
// Notice, open source version of file_util does not support
// pattern matching, and will return a result for a single filespec
// which is a regular file.
// This could be improved.
//
inline absl::Status NullFreeString(absl::string_view str,
                                   std::string* out_str) {
  if (str.find('\0') != absl::string_view::npos) {
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("filename contains null characters: ", str));
  }
  *out_str = std::string(str);
  return absl::OkStatus();
}

inline absl::Status Match(absl::string_view filespec,
                          std::vector<std::string>* file_names) {
  // Because we are using a c api, check for in-string nulls.
  std::string filespec_str;
  if (absl::Status status = NullFreeString(filespec, &filespec_str);
      !status.ok()) {
    return status;
  }

  struct stat status;
  if (stat(filespec.data(), &status) != 0) {
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Could not find: ", filespec));
  } else if (S_ISREG(status.st_mode) == 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("File is not regular: ", filespec));
  }

  file_names->push_back(std::string(filespec));
  return absl::OkStatus();
}

inline absl::Status GetContents(absl::string_view filename,
                                std::string* file_contents) {
  // Because we are using a c api, check for in-string nulls.
  std::string filename_str;
  if (absl::Status status = NullFreeString(filename, &filename_str);
      !status.ok()) {
    return status;
  }

  struct stat status;
  if (stat(filename.data(), &status) != 0) {
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Could not find", filename));
  } else if (S_ISREG(status.st_mode) == 0) {
    return absl::Status(absl::StatusCode::kFailedPrecondition,
                        absl::StrCat("File is not regular", filename));
  }

  std::ifstream stream(std::string(filename), std::ifstream::in);
  if (!stream) {
    // Could be a wider range of reasons.
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Unable to open: ", filename));
  }
  stream.seekg(0, stream.end);
  int length = stream.tellg();
  stream.seekg(0, stream.beg);
  file_contents->clear();
  file_contents->resize(length);
  stream.read(&(*file_contents)[0], length);
  stream.close();
  return absl::OkStatus();
}

inline absl::Status SetContents(absl::string_view filename,
                                absl::string_view file_contents) {
  // Because we are using a c api, check for in-string nulls.
  std::string filename_str;
  if (absl::Status status = NullFreeString(filename, &filename_str);
      !status.ok()) {
    return status;
  }
  std::ofstream stream(filename_str, std::ofstream::out);

  if (!stream) {
    // Could be a wider range of reasons.
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrCat("Unable to open: ", filename));
  }
  stream.write(file_contents.data(), file_contents.size());
  stream.close();
  return absl::OkStatus();
}

// The path in blaze where we expect to find inputs like `.test` files.
inline std::string TestSrcRootDir() {
  return file_based_test_driver_base::JoinPath(
      getenv("TEST_SRCDIR"), getenv("TEST_WORKSPACE"),
      "file_based_test_driver");
}

// The path in bazel where we expect to find inputs like `.test` files.
inline std::string TestTmpDir() { return getenv("TEST_TMPDIR"); }

class RegisteredTempFile {
 public:
  static std::string RootDir() { return TestTmpDir(); }
  RegisteredTempFile(absl::string_view filename, absl::string_view contents)
      : filename_(file_based_test_driver_base::JoinPath(RootDir(), filename)),
        should_delete_(false) {
    std::string filename_str;
    struct stat file_stat;
    if (!NullFreeString(filename_, &filename_str).ok()) {
      LOG(FATAL)
          << "RegisteredTempFile: Illegal filename contains null characters: "
          << filename_;
    }
    if (stat(filename_str.c_str(), &file_stat) != 0) {
      // LOG(FATAL) << "RegisteredTempFile: File already exists: " <<
      // filename_str;
    }
    if (absl::Status s = SetContents(filename_str, contents); !s.ok()) {
      LOG(FATAL) << "RegisteredTempFile: Unable to set contents: " << s;
    }
    should_delete_ = true;
  }
  ~RegisteredTempFile() {
    if (should_delete_) {
      should_delete_ = false;
      remove(filename_.c_str());
    }
  }
  std::string filename() const { return filename_; }

 private:
  std::string filename_;
  bool should_delete_;
};

inline absl::Status RecursivelyCreateDir(absl::string_view dirname) {
  if (dirname.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Failed to create directory with empty name");
  } else if (dirname == "/") {
    // Assume the root directory always exists
    return absl::OkStatus();
  }

  // Because we are using a c api, check for in-string nulls.
  std::string dirname_str;
  if (absl::Status status = NullFreeString(dirname, &dirname_str);
      !status.ok()) {
    return status;
  }

  struct stat status;
  if (stat(dirname_str.c_str(), &status) == 0) {
    // File exists ...
    if (S_ISDIR(status.st_mode)) {
      // ... and it is a directory, great!
      return absl::OkStatus();
    } else {
      // ... but it is not a directory, bad.
      return absl::Status(absl::StatusCode::kFailedPrecondition,
                          absl::StrCat("Could not find: ", dirname));
    }
  }

  absl::string_view parent =
      file_based_test_driver_base::SplitPath(dirname).first;
  absl::Status parent_status = RecursivelyCreateDir(parent);
  if (!parent_status.ok()) {
    return parent_status;
  }

  if (mkdir(dirname_str.data(), /*mode=*/S_IRWXU | S_IRWXG | S_IRWXO) == 0) {
    return absl::OkStatus();
  }
  const int mkdir_errno = errno;
  if (mkdir_errno == EPERM || mkdir_errno == EACCES) {
    return absl::Status(
        absl::StatusCode::kPermissionDenied,
        absl::StrCat("Failed to create directory due to permissions: ",
                     dirname));
  }
  return absl::Status(absl::StatusCode::kInvalidArgument,
                      absl::StrCat("Failed to create directory : ", dirname,
                                   " errno=", strerror(mkdir_errno)));
}

inline std::vector<std::string> FileLines(absl::string_view filepath) {
  std::string contents;
  if (GetContents(filepath, &contents).ok()) {
    return absl::StrSplit(contents, "\n");
  }
  return {};
}

}  // namespace file_based_test_driver::internal

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_FILE_UTIL_OSS_H_
