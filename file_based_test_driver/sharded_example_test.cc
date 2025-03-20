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

// Example for how to use file_based_test_driver in combination with
// TestCaseOptions. The operation being tested is to sum a bunch of comma
// separated numbers. The example TestCaseOptions enable post-processing on the
// output.
//
// Note that this example uses a one-file-per-test approach. This is the
// preferred setup because of its simplicity. If you choose to run multiple test
// files in a single test, you must create a separate TestCaseOptions object for
// each file that is tested. If you don't, then overrides of the default option
// values may "leak" between files.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "file_based_test_driver/base/path.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/file_based_test_driver.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"

namespace {

std::string TestFilePath();

// The test fixture
class ExampleTest
    : public ::testing::TestWithParam<file_based_test_driver::TestCaseHandle> {
 public:
  //
  // OPTION NAMES
  //

  // If set, doubles the result.
  static inline constexpr absl::string_view kDoubleResultOption =
      "double_result";

  // Adds an amount to the result. Processed after doubling.
  static inline constexpr absl::string_view kAddAmountOption = "add_amount";

  // String Prefix to print before the result.
  static inline constexpr absl::string_view kResultPrefixOption =
      "result_prefix";

  // If set, flags the test suite to ignore the result.
  static inline constexpr absl::string_view kIgnoreThisTestOption =
      "ignore_this_test";

  static void SetUpTestSuite() {
    options_ = new file_based_test_driver::TestCaseOptions();
    options_->RegisterBool(kDoubleResultOption, false);
    options_->RegisterInt64(kAddAmountOption, 0);
    options_->RegisterString(kResultPrefixOption, "The result is:");
    options_->RegisterBool(kIgnoreThisTestOption, false);

    runner_ = file_based_test_driver::RunnerForFile(TestFilePath()).release();
  }

  static void TearDownTestSuite() {
    delete options_;
    delete runner_;
  }

  // Callback for running a single test case. This example sums a series of
  // comma separated integers, and applies some transformations on it based on
  // test options. Test inputs that start with an empty line are copied to the
  // output with an added *second* line saying "INSERTED SECOND LINE".
  static void RunExampleTestCase(
      absl::string_view test_case,
      file_based_test_driver::RunTestCaseResult* test_result) {
    // Parse and strip off the test case's options.
    std::string test_case_without_options = std::string(test_case);
    const absl::Status options_status =
        options_->ParseTestCaseOptions(&test_case_without_options);
    if (!options_status.ok()) {
      // For bad test cases, prefer to return an error in the output instead
      // of crashing.
      test_result->AddTestOutput(absl::StrCat(
          "ERROR: Failed to parse options: ", options_status.ToString()));
      return;
    }

    // Ignore? Then return straight away. The test driver will copy the entire
    // test case verbatim.
    if (options_->GetBool(kIgnoreThisTestOption)) {
      test_result->set_ignore_test_output(true);
      return;
    }

    // Special case: anything starting with \n gets special treatment. (This is
    // used to show how blank lines are handled using escaping.)
    if (absl::StartsWith(test_case, "\n")) {
      // Return the test output through <test_result>.
      test_result->AddTestOutput(
          absl::StrCat("\nINSERTED SECOND LINE\n", test_case.substr(1)));
      return;
    }

    // The actual test. A real test would of course call into some other
    // function that is to be tested.
    const std::vector<std::string> number_strings =
        absl::StrSplit(test_case_without_options, absl::ByAnyChar(",;"),
                       absl::SkipWhitespace());
    int64_t sum = 0;
    for (const std::string& number_string : number_strings) {
      int64_t number = 0;
      if (!absl::SimpleAtoi(number_string, &number)) {
        // For bad test cases, prefer to return an error in the output instead
        // of crashing.
        test_result->AddTestOutput(
            absl::StrCat("ERROR: Failed to parse ", number_string));
        return;
      }
      sum += number;
    }
    if (options_->GetBool(kDoubleResultOption)) {
      sum *= 2;
    }
    sum += options_->GetInt64(kAddAmountOption);
    const std::string result_string =
        absl::StrCat(options_->GetString(kResultPrefixOption), " ", sum);

    // Return the test output through <test_result>.
    test_result->AddTestOutput(result_string);
  }

 protected:
  static file_based_test_driver::TestCaseOptions* options_;
  static file_based_test_driver::TestFileRunner* runner_;
};

file_based_test_driver::TestCaseOptions* ExampleTest::options_ = nullptr;
file_based_test_driver::TestFileRunner* ExampleTest::runner_ = nullptr;

inline std::string TestFilePath() {
  return file_based_test_driver_base::JoinPath(
      getenv("TEST_SRCDIR"), getenv("TEST_WORKSPACE"),
      "file_based_test_driver", "example_no_alternations.test");
}

TEST_P(ExampleTest, RunTest) {
  runner_->RunTestCase(GetParam(), &ExampleTest::RunExampleTestCase);
}

INSTANTIATE_TEST_SUITE_P(
    ExampleTest, ExampleTest,
    testing::ValuesIn(file_based_test_driver::ShardedTestsInFile(
        TestFilePath(),
        [](const file_based_test_driver::TestCaseInput& input) {
          return absl::StartsWith(input.text(), "[default");
        })),
    testing::PrintToStringParamName());

}  // anonymous namespace



