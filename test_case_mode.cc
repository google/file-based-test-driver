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
#include "test_case_mode.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "re2/re2.h"
#include "base/status_macros.h"
#include "base/statusor.h"

namespace file_based_test_driver {

// static
file_based_test_driver_base::StatusOr<TestCaseMode> TestCaseMode::Create(
    std::vector<std::string> mode_parts) {
  for (const std::string& part : mode_parts) {
    if (part.empty()) {
      return ::file_based_test_driver_base::FailedPreconditionErrorBuilder()
             << "Multi-part modes cannot contain empty strings";
    }
    static LazyRE2 whitespace = {"\\s"};
    if (RE2::PartialMatch(part, *whitespace)) {
      return ::file_based_test_driver_base::FailedPreconditionErrorBuilder()
             << "Multi-part modes cannot contain spaces";
    }
    static LazyRE2 literal_star = {"\\*"};
    if (RE2::PartialMatch(part, *literal_star)) {
      return ::file_based_test_driver_base::FailedPreconditionErrorBuilder()
             << "Multi-part modes cannot contain literal stars (*)";
    }
  }
  TestCaseMode mode;
  mode.mode_parts_ = std::move(mode_parts);
  return std::move(mode);
}

// static
file_based_test_driver_base::StatusOr<TestCaseMode> TestCaseMode::Create(
    absl::string_view description) {
  std::vector<std::string> mode_parts =
      absl::StrSplit(std::string(description), ' ');
  return Create(std::move(mode_parts));
}

std::string TestCaseMode::ToString() const {
  return absl::StrJoin(mode_parts_, " ");
}

// static
file_based_test_driver_base::StatusOr<std::vector<TestCaseMode>> TestCaseMode::ParseModes(
    absl::string_view modes_string) {
  std::vector<TestCaseMode> test_modes;

  while (!modes_string.empty()) {
    modes_string = absl::StripLeadingAsciiWhitespace(modes_string);
    if (modes_string.empty()) break;
    if (!absl::ConsumePrefix(&modes_string, "[")) {
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "A test mode must be enclosed in [] but got: ";
    }
    absl::string_view::size_type index = modes_string.find_first_of("]");
    if (index == absl::string_view::npos) {
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "A test mode must be enclosed in [] but got: ";
    }
    std::string mode_name(modes_string.substr(0, index));
    if (mode_name.empty()) {
      return ::file_based_test_driver_base::UnknownErrorBuilder().LogError()
             << "Found empty test mode enclosed in []:\n";
    }
    FILE_BASED_TEST_DRIVER_ASSIGN_OR_RETURN(TestCaseMode mode,
                     TestCaseMode::Create(mode_name));
    modes_string.remove_prefix(index + 1);
    test_modes.emplace_back(std::move(mode));
  }

  return test_modes;
}

// static
std::string TestCaseMode::CollapseModes(const TestCaseMode::Set& modes) {
  if (modes.size() == 1 && modes.begin()->empty()) {
    return "";
  }
  return absl::StrCat(
      "[", absl::StrJoin(modes, "][", TestCaseMode::JoinFormatter()), "]");
}

}  // namespace file_based_test_driver
