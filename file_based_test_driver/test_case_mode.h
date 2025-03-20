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
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_MODE_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_MODE_H_

#include <cstddef>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace file_based_test_driver {

// Represents a single mode of execution in TestCaseOutputs.
class TestCaseMode {
 public:
  class LessThan {
   public:
    bool operator()(const TestCaseMode& a, const TestCaseMode& b) const {
      return a.mode_parts_ < b.mode_parts_;
    }
  };
  class JoinFormatter {
   public:
    void operator()(std::string* out, const TestCaseMode& mode) const {
      absl::StrAppend(out, mode.ToString());
    }
  };
  using Set = std::set<TestCaseMode, LessThan>;
  using UnorderedSet = absl::node_hash_set<TestCaseMode>;
  template <class T>
  using Map = std::map<TestCaseMode, T, LessThan>;
  template <class T>
  using UnorderedMap = absl::node_hash_map<TestCaseMode, T>;

  static absl::StatusOr<TestCaseMode> Create(
      std::vector<std::string> mode_parts);
  // Create a description of a test, which is a space-separated list of
  // mode-parts.
  static absl::StatusOr<TestCaseMode> Create(absl::string_view description);

  TestCaseMode() = default;

  std::string ToString() const;
  bool empty() const { return mode_parts_.empty(); }

  bool operator==(const TestCaseMode& other) const {
    return mode_parts_ == other.mode_parts_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const TestCaseMode& test_case_mode) {
    return H::combine(std::move(h), test_case_mode.mode_parts_);
  }

  static absl::StatusOr<std::vector<TestCaseMode>> ParseModes(
      absl::string_view modes_string);
  static std::string CollapseModes(const TestCaseMode::Set& modes);

 private:
  std::vector<std::string> mode_parts_;
};

inline std::ostream& operator<<(std::ostream& o, const TestCaseMode& m) {
  return o << m.ToString();
}

}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_MODE_H_
