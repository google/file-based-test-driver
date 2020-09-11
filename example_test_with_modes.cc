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

// Example for how to use file_based_test_driver to run tests that support test
// modes. The operation being tested is to sum a bunch of comma separated
// numbers (similar to example_test.cc). We use mode_a_results/mode_b_results to
// overwrite the results for different test modes.

#include <string>
#include <vector>

#include "base/path.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/status_matchers.h"
#include "file_based_test_driver.h"
#include "test_case_mode.h"
#include "test_case_options.h"
#include "base/status.h"
#include "base/status_macros.h"

namespace {

using file_based_test_driver::RunTestCaseWithModesResult;
using file_based_test_driver::TestCaseMode;

// The test fixture
class ExampleTestWithModes : public ::testing::Test {
 public:
  //
  // OPTION NAMES
  //
  // String Prefix to print before the result.
  static inline constexpr absl::string_view kResultPrefixOption =
      "result_prefix";

  // If set, flags the test suite to ignore the result.
  static inline constexpr absl::string_view kIgnoreThisTestOption =
      "ignore_this_test";

  // If set, return the specified results for the test mode. The format is
  // <result type1>:<output1>,<result type2>:<output2>,...
  static inline constexpr absl::string_view kModeAResultOption =
      "mode_a_results";
  static inline constexpr absl::string_view kModeBResultOption =
      "mode_b_results";

  ExampleTestWithModes() {
    options_.RegisterString(kResultPrefixOption, "The result is: ");
    options_.RegisterBool(kIgnoreThisTestOption, false);
    options_.RegisterString(kModeAResultOption, "");
    options_.RegisterString(kModeBResultOption, "");
  }

  // Callback for running a single test case. This example sums a series of
  // comma separated integers, and applies some transformations on it based on
  // test options. Test inputs that start with an empty line are copied to the
  // output with an added *second* line saying "INSERTED SECOND LINE".
  void RunExampleTestCase(const TestCaseMode& test_mode,
                          absl::string_view test_mode_option_name,
                          absl::string_view test_case,
                          RunTestCaseWithModesResult* test_result) {
    // Parse and strip off the test case's options.
    std::string test_case_without_options = std::string(test_case);
    const absl::Status options_status =
        options_.ParseTestCaseOptions(&test_case_without_options);
    if (!options_status.ok()) {
      // For bad test cases, prefer to return an error in the output instead
      // of crashing.
      FILE_BASED_TEST_DRIVER_CHECK_OK(test_result->mutable_test_case_outputs()->RecordOutput(
          TestCaseMode(test_mode), "ERROR",
          absl::StrCat("Failed to parse options: ",
                       options_status.ToString())));
      return;
    }

    // Ignore? Then return straight away. The test driver will copy the entire
    // test case verbatim.
    if (options_.GetBool(kIgnoreThisTestOption)) {
      test_result->set_ignore_test_output(true);
      return;
    }

    // Special case: anything starting with \n gets special treatment. (This is
    // used to show how blank lines are handled using escaping.)
    if (absl::StartsWith(test_case, "\n")) {
      // Return the test output through <test_result>.
      FILE_BASED_TEST_DRIVER_CHECK_OK(test_result->mutable_test_case_outputs()->RecordOutput(
          TestCaseMode(test_mode), "" /* result_type */,
          absl::StrCat("\nINSERTED SECOND LINE\n", test_case.substr(1))));
      return;
    }

    // The actual test. A real test would of course call into some other
    // function that is to be tested.
    const std::vector<std::string> number_strings =
        absl::StrSplit(test_case_without_options, ",", absl::SkipEmpty());
    int64_t sum = 0;
    for (const std::string& number_string : number_strings) {
      int64_t number = 0;
      if (!absl::SimpleAtoi(number_string, &number)) {
        // For bad test cases, prefer to return an error in the output instead
        // of crashing.
        FILE_BASED_TEST_DRIVER_CHECK_OK(test_result->mutable_test_case_outputs()->RecordOutput(
            TestCaseMode(test_mode), "ERROR",
            absl::StrCat("Failed to parse ", number_string)));
        return;
      }
      sum += number;
    }

    const std::string mode_result = options_.GetString(test_mode_option_name);

    // Return the outputs specified in the options.
    const absl::node_hash_map<std::string, std::string> mode_results =
        ExtractOutputs(mode_result);
    for (const auto& result : mode_results) {
      FILE_BASED_TEST_DRIVER_CHECK_OK(test_result->mutable_test_case_outputs()->RecordOutput(
                   TestCaseMode(test_mode), result.first, result.second));
    }
    // Return the default main output (the sum) if it's not specified in the
    // options.
    if (!file_based_test_driver_base::ContainsKey(mode_results, "")) {
      const std::string result_string =
          absl::StrCat(options_.GetString(kResultPrefixOption), sum);
      FILE_BASED_TEST_DRIVER_CHECK_OK(test_result->mutable_test_case_outputs()->RecordOutput(
                   TestCaseMode(test_mode), "", result_string));
    }
  }

 protected:
  file_based_test_driver::TestCaseOptions options_;
  std::string test_dir_;

 private:
  // Extracts the outputs from the option.
  absl::node_hash_map<std::string, std::string> ExtractOutputs(
      const std::string& result_str) {
    absl::node_hash_map<std::string, std::string> result_type_to_output_map;
    const std::vector<std::string> result_list =
        absl::StrSplit(result_str, ",", absl::SkipEmpty());
    for (const std::string& result : result_list) {
      const std::vector<std::string> output =
          absl::StrSplit(result, ":", absl::SkipEmpty());
      if (output.size() == 1) {
        file_based_test_driver_base::InsertIfNotPresent(&result_type_to_output_map, "", output[0]);
      } else if (output.size() == 2) {
        file_based_test_driver_base::InsertIfNotPresent(&result_type_to_output_map, output[0],
                                output[1]);
      }
    }
    return result_type_to_output_map;
  }
};

inline std::string TestDir() {
  return file_based_test_driver_base::AddSlash(
      file_based_test_driver_base::JoinPath(
          getenv("TEST_SRCDIR"), "com_google_file_based_test_driver"));
}

// Run test cases from example_with_modes.test.
TEST_F(ExampleTestWithModes, RunExampleTestModeA) {
  const std::string filespec =
      file_based_test_driver_base::JoinPath(TestDir(), "example_with_modes.test");
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
      TestCaseMode mode,
      TestCaseMode::Create(std::vector<std::string>({"MODE", "A"})));
  EXPECT_TRUE(RunTestCasesWithModesFromFiles(
      filespec, absl::bind_front(&ExampleTestWithModes::RunExampleTestCase,
                                 this, mode, kModeAResultOption)));
}

TEST_F(ExampleTestWithModes, RunExampleTestModeB) {
  const std::string filespec =
      file_based_test_driver_base::JoinPath(TestDir(), "example_with_modes.test");
  FILE_BASED_TEST_DRIVER_ASSERT_OK_AND_ASSIGN(
      TestCaseMode mode,
      TestCaseMode::Create(std::vector<std::string>({"MODE", "B"})));
  EXPECT_TRUE(RunTestCasesWithModesFromFiles(
      filespec, absl::bind_front(&ExampleTestWithModes::RunExampleTestCase,
                                 this, mode, kModeBResultOption)));
}

TEST_F(ExampleTestWithModes, CountTestCasesInFiles) {
  const std::string filespec =
      file_based_test_driver_base::JoinPath(TestDir(), "example_with_modes.test");
  EXPECT_EQ(file_based_test_driver::CountTestCasesInFiles(filespec), 13);
}

}  // anonymous namespace
