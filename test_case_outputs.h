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
#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_OUTPUTS_H_
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_OUTPUTS_H_

#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "test_case_mode.h"
#include "base/map_util.h"
#include "base/status_macros.h"

namespace file_based_test_driver {

// TestCaseOutputs represents the outputs of one text-based test case (such as
// the ones processed by RunTestCasesFromFiles in file_based_test_driver.h).
// It supports serializing/parsing a TestCaseOutputs to/from its text format.
//
// Inside a TestCaseOutputs we have the following hierarchy:
// mode
//   -> result type
//       -> output
//
// 'mode' can either be 'all modes', which is represented by an empty string, or
// a specific mode value. Each mode can have a distinct set of 'result type'
// values. Empty result type is allowed, meaning it's the main output.
//
// TestCaseOutputs can have at most one output for each ('mode', 'result type')
// combination. If an 'all modes' output (mode = '') exists, mode specific
// outputs cannot be added for the same result type. Similarly, if a mode
// specific output exists for a result type, the 'all modes' output cannot be
// added for the same result type.
//
// TestCaseOutputs example:
// mode A
//   -> "" # main result type
//         -> main result type output
//   -> result type 1
//         -> output A1
// mode B
//   -> "" # main result type
//        -> main result type output
//   -> result type 2
//        -> output B2
// ""  # all modes
//   -> result type 3
//        -> all mode output for result type 3
//
// The text format of a TestCaseOutputs contains the combined outputs.
// If the outputs of multiple modes are same for a 'result type', they are
// combined into one output string. The first line of a combined output
// contains the 'result type' and the modes of the output, if they are not
// empty.
//
// Text output of the above example:
//
// <>[mode A][mode B]        # result type = ""
// main result type output
// --
// <result type 1>[mode A]
// output A1
// --
// <result type 2>[mode B]
// output B2
// --
// <result type 3>         # all modes: mode = ""
// all mode output for result type 3
//
class TestCaseOutputs {
 public:
  TestCaseOutputs() = default;
  ~TestCaseOutputs() = default;

  TestCaseOutputs(const TestCaseOutputs&) = default;
  TestCaseOutputs& operator=(const TestCaseOutputs&) = default;

  bool operator==(const TestCaseOutputs& other) const {
    return outputs_ == other.outputs_ &&
           disabled_modes_ == other.disabled_modes_ &&
           possible_modes_ == other.possible_modes_;
  }

  // Parses the test outputs from 'parts'. Each string in 'parts' contains
  // one output. The output's result type and test modes may be specified in
  // its first line. The patterns are:
  // 1. Both result type and test modes are specified, e.g.,
  //    <result type>[test mode 1][test mode 2]
  //    my test output
  // 2. Only test modes are specified. This is the main result (result type="")
  //    of the test case, e.g.,
  //    <>[test mode 1][test mode 2]
  //    my test output
  // 3. Only result type is specified. The output for all modes are same, e.g.,
  //    <result type>
  //    my test output
  // 4. no result type or test modes are specified. This means the main result
  //    is same for all test modes, e.g.,
  //    my test output
  // 5. A part may also contain all the possible modes for this test. This will
  //    be a one line output with prefix 'Possible Modes:' followed by a list of
  //    test modes, e.g.,
  //    Possible Modes: [possible mode 1][possible mode 2]
  absl::Status ParseFrom(const std::vector<std::string>& parts);

  // Returns outputs that are combined by result type and test modes. This is
  // the opposite operation of 'ParseFrom'.
  // If 'include_possible_modes' is true and 'possible_modes_' is not empty,
  // combined_outputs[0] will contain all the possible test modes with prefix
  // "Possible Modes:", e.g.,
  // 'Possible Modes: [possible mode 1][possible mode 2]'.
  absl::Status GetCombinedOutputs(
      bool include_possible_modes,
      std::vector<std::string>* combined_outputs) const;

  // Adds a test output for 'test_mode' and 'result_type'. Only one output can
  // be added for each ('test_mode', 'result_type') combination. If 'output'
  // does not end with a newline, this function adds one.
  //
  // 'test_mode' cannot be empty when adding outputs.
  // However when adding an output for 'result_type', if an 'all modes'
  // (test_mode="") output for 'result_type' already exists in this
  // TestCaseOutputs,this function will return error.
  absl::Status RecordOutput(const TestCaseMode& test_mode,
                            absl::string_view result_type,
                            absl::string_view output);

  // Disables a test mode.
  // Existing outputs for the disabled mode will be removed.
  absl::Status DisableTestMode(const TestCaseMode& disabled_mode);

  // Returns the disabled test modes.
  const TestCaseMode::UnorderedSet& disabled_modes() const {
    return disabled_modes_;
  }

  // Sets the possible modes for this test. The modes in 'possible_modes' cannot
  // be empty. 'possible_modes' must include all the modes that are already
  // in 'outputs_'.
  absl::Status SetPossibleModes(
      const std::initializer_list<TestCaseMode>& possible_modes);
  absl::Status SetPossibleModes(TestCaseMode::Set possible_modes);

  const TestCaseMode::Set& possible_modes() const { return possible_modes_; }

  bool IsEmpty() const { return outputs_.empty(); }

  // Merges a list of actual TestCaseOutputs into an expected TestCaseOutputs.
  // The TestCaseOutputs in 'actual_outputs' should not contain 'all modes'
  // outputs. 'expected_outputs' may contain 'all modes' outputs.
  //
  // The merging works like this:
  // 1) Collect all the test modes mentioned in 'expected_outputs' and
  //    'actual_outputs' (if 'possible_modes_' is set, this is
  //    'possible_modes_') and the disabled modes
  //    mentioned in 'actual_outputs'.
  //    If multiple actual outputs have 'possible_modes_', they must be same.
  //    If any of the actual outputs specifies 'possible_modes_', only these
  //    modes will be added to the test mode list ('test_modes').
  //    Remove the disabled modes from the test mode list ('test_modes').
  // 2) Copy 'expected_outputs' into 'merged_outputs' and remove all the outputs
  //    for disabled modes from 'merged_outputs'.
  // 3) Break out the 'all modes' output in 'merged_outputs' into modes in
  //    'test_modes'.
  // 4) For each TestCaseOutputs X in 'actual_outputs'
  //      For each mode in the X, if it's not disabled:
  //        Replace the outputs for that mode in 'merged_outputs' with the
  //        outputs from X.
  // 5) Regenerate the 'all modes' output in 'merged_outputs', i.e., for a
  //    result type, if all outputs are same for the modes in 'test_modes',
  //    remove the mode specific outputs and add an 'all modes' output.
  static absl::Status MergeOutputs(
      const TestCaseOutputs& expected_outputs,
      const std::vector<TestCaseOutputs>& actual_outputs,
      TestCaseOutputs* merged_outputs);

 private:
  // Represents the output of a single Mode output. This contains a mapping
  // of results_type to the actual text output for the test for that mode and
  // result_type.
  class ModeResults {
   public:
    bool operator==(const ModeResults& other) const {
      return result_type_to_output_ == other.result_type_to_output_;
    }

    // Removes the associated output for 'result_type'. Returns false if
    // no output has been added for 'result_type'.
    ABSL_MUST_USE_RESULT bool RemoveResultType(absl::string_view result_type) {
      auto it = result_type_to_output_.find(result_type);
      if (it == result_type_to_output_.end()) return false;

      result_type_to_output_.erase(it);
      return true;
    }

    // Fetches the text for 'result_type' and returns it in 'output'.
    // Returns false if the given 'result_type' doesn't exist.
    ABSL_MUST_USE_RESULT bool GetOutputForResultType(
        absl::string_view result_type, std::string* output) const {
      auto it = result_type_to_output_.find(result_type);
      if (it == result_type_to_output_.end()) return false;

      *output = it->second;
      return true;
    }

    // Adds 'output' as text for the given 'result_type' if it doesn't exist.
    // Returns true if 'output' was added.
    ABSL_MUST_USE_RESULT bool AddOutput(absl::string_view result_type,
                                        absl::string_view output) {
      return file_based_test_driver_base::InsertIfNotPresent(&result_type_to_output_,
                                     std::string(result_type),
                                     std::string(output));
    }

    bool empty() const { return result_type_to_output_.empty(); }

    absl::node_hash_map<std::string, std::string>::const_iterator begin()
        const {
      return result_type_to_output_.begin();
    }

    absl::node_hash_map<std::string, std::string>::const_iterator end() const {
      return result_type_to_output_.end();
    }

   private:
    absl::node_hash_map<std::string, std::string> result_type_to_output_;
  };

  using TestModeOutputsMap = TestCaseMode::UnorderedMap<ModeResults>;

  // Adds a test output for 'test_mode' and 'result_type'. Only one output can
  // be added for each ('test_mode', 'result_type') combination.
  //
  // If 'test_mode' is not empty, returns error if an 'all modes' (test_mode="")
  // output already exists for the result type.
  // If 'test_mode' is empty (all modes), returns error if a mode specific
  // output exists for this result type.
  absl::Status AddOutputInternal(const TestCaseMode& test_mode,
                                 absl::string_view result_type,
                                 absl::string_view output);

  // Whether this TestCaseOutputs has 'all modes' outputs (empty test mode).
  bool HasAllModesResult() const;

  // Returns all the test modes mentioned in this TestCaseOutputs.
  TestCaseMode::UnorderedSet GetTestModes() const;

  // Breaks out the 'all modes' outputs into the modes in 'test_modes'.
  absl::Status BreakOutAllModesOutputs(
      const TestCaseMode::UnorderedSet& test_modes);

  // For each result type, if all outputs are same for the modes in
  // 'test_modes', removes the mode specific outputs and adds an 'all modes'
  // output. Returns error if there are outputs for modes other than those in
  // 'test_modes'.
  absl::Status GenerateAllModesOutputs(
      const TestCaseMode::UnorderedSet& test_modes);

  // For each mode M in 'outputs', if it is also in 'test_modes', replace
  // all the outputs for M in 'outputs_' with the outputs from 'outputs'.
  absl::Status InsertOrUpdateOutputsForTestModes(
      const TestCaseOutputs& outputs,
      const TestCaseMode::UnorderedSet& test_modes);

  // Returns an error if any of 'outputs_' contains a mode not in
  // 'possible_modes_'.
  absl::Status ValidatePossibleModes() const;

  // Mode specific test outputs
  // Maps from test mode to test outputs. Each test mode can have multiple
  // test outputs, identified by result types. Result
  // type for the main test output can be an empty string.
  // Test outputs that are same for all modes will be stored in outputs_[""].
  TestModeOutputsMap outputs_;

  // The test modes that are disabled by the test settings.
  TestCaseMode::UnorderedSet disabled_modes_;

  // All the possible modes for this test. When a test callback returns an
  // actual TestCaseOutputs, it can add all the possible modes for the test in
  // 'possible_modes_'. And we will use them to break the 'all modes' outputs
  // when merging the actual TestCaseOutputs with the expected outputs. The
  // actual TestCaseOutputs itself does not need to contain outputs for all the
  // possible modes.
  // If 'possible_modes_' is specified, it should contain all the test modes
  // that appear in 'outputs_'.
  // If 'possible_modes_' is empty, we will try to infer the possible
  // modes from the actual TestCaseOutputs and the expected TestCaseOutputs.
  // However we may not able to get the complete list if the test callback does
  // not run in all modes and the actual TestCaseOutputs contains partial
  // results. In this case the merged output may not be complete.
  TestCaseMode::Set possible_modes_;

  // Allow alternation builder to poke at some of the internals.
  friend class AlternationSetWithModes;
};

}  // namespace file_based_test_driver

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_TEST_CASE_OUTPUTS_H_
