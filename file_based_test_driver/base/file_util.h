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

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_FILE_UTIL_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_FILE_UTIL_H_

// This file abstract file system interactions so they can be more easily
// implemented in the open source version. The API is documented here for
// convenience.
//
#include "file_based_test_driver/base/file_util_oss.h"

namespace file_based_test_driver::internal {

//
// Match()
//
// Return a list of files that match the given filespec. Notice, the open source
// implementation does not support matching multiple files (i.e. the vector
// will be empty, or size 1.
//
// `filespec` must not contain '\0'.
//
// absl::Status Match(absl::string_view filespec,
//                    std::vector<std::string>* file_names);

//
// GetContents()
//
// Read the entire contents of `filename` into `file_contents`.
//
// `filename` must not contain '\0'.
//
// absl::Status GetContents(absl::string_view filename,
//                          std::string* file_contents);

//
// SetContents()
//
// Overwrite the contents of `filename` with `file_contents`.
//
// `filename` must not contain '\0'.
//
// absl::Status GetContents(absl::string_view filename,
//                          std::string* file_contents);
//

//
// class RegisteredTempFile
//
// Create and initialize a temporary file with contents, deleting it after
// the object leaves the stack.
//
// class RegisteredTempFile {
//   RegisteredTempFile(absl::string_view filename, absl::string_view contents);
// };

// RecursivelyCreateDir()
//
// Ensures that `dirname` exists and is a directory by creating any parent
// directories as necessary.  If `dirname` is already a directory, returns
// absl::OkStatus().
//
// This does not necessarily ensure adequate permissions to write to the given
// directory (for instance, if it already exists, but is read-only).
//
// `dirname` must not contain '\0'.
//
// absl::Status RecursivelyCreateDir(absl::string_view dirname);
//

// FileLines()
//
// Reads the contents of `filename` and returns object that can iterate through
// it, broken at newline boundaries.
//
// The return type is not defined, and should not be relied on, but it is
// guarnateed that it can be iterated in a range loop.
//
// `filename` must not contain '\0'.
//
// undefined_iterable_type<std:string> FileLines(absl::string_view filename)
//

// TestSrcRootDir()
//
// The path in blaze/bazel where we expect to find inputs like `.test` files.
//
// std::string TestSrcRootDir();
//

// TestTmpDir()
//
// An absolute path to a directory where we can write temporary files.
//
// std::string TestTmpDir();
//

}  // namespace file_based_test_driver::internal
#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_FILE_UTIL_H_
