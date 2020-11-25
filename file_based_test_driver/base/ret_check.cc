//
// Copyright 2018 ZetaSQL Authors
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

#include "file_based_test_driver/base/ret_check.h"

#include <string>

#include "absl/status/status.h"
#include "file_based_test_driver/base/logging.h"
#include "file_based_test_driver/base/source_location.h"
#include "file_based_test_driver/base/status_builder.h"

namespace file_based_test_driver_base {
namespace internal_ret_check {

StatusBuilder RetCheckFailSlowPath(SourceLocation location) {
  return InternalErrorBuilder(location).EmitStackTrace()
         << "FILE_BASED_TEST_DRIVER_RET_CHECK failure (" << location.file_name()
         << ":" << location.line() << ") ";
}

StatusBuilder RetCheckFailSlowPath(SourceLocation location,
                                   std::string* condition) {
  std::unique_ptr<std::string> cleanup(condition);
  return RetCheckFailSlowPath(location) << *condition << " ";
}

StatusBuilder RetCheckFailSlowPath(SourceLocation location,
                                   const char* condition) {
  return RetCheckFailSlowPath(location) << condition << " ";
}

StatusBuilder RetCheckFailSlowPath(SourceLocation location,
                                   const char* condition,
                                   const absl::Status& status) {
  return RetCheckFailSlowPath(location)
         << condition << " returned " << status << " ";
}

}  // namespace internal_ret_check
}  // namespace file_based_test_driver_base
